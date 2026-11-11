/* Offscreen native Metal implementation. Guest ABI and window ownership live
 * outside this module. Commands own their native data through GPU completion. */
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "glide_gpu.h"
#include "glide_shaders.h"
_Static_assert(sizeof(pod_glide_state)==448,"Glide state ABI");
_Static_assert(sizeof(pod_glide_vertex)==48,"Glide vertex ABI");
#define TEX_SLOTS 128
#define BATCH_VERTICES 4096
struct pod_glide_gpu {
    id<MTLDevice> device; id<MTLCommandQueue> queue;
    id<MTLTexture> color[2],depth,textures[TEX_SLOTS],white;
    id<MTLLibrary> library;
    NSMutableDictionary *pipelines;
    id<MTLDepthStencilState> depths[16]; id<MTLSamplerState> samplers[16];
    id<MTLCommandBuffer> commands; id<MTLRenderCommandEncoder> encoder;
    pod_glide_vertex vertices[BATCH_VERTICES]; unsigned count;
    pod_glide_state state; int lines; unsigned width,height;
};
static MTLBlendFactor blend(unsigned n,int src) {
    switch(n) {
      case 0:return MTLBlendFactorZero;case 1:return MTLBlendFactorSourceAlpha;
      case 2:return src?MTLBlendFactorDestinationColor:MTLBlendFactorSourceColor;
      case 3:return MTLBlendFactorDestinationAlpha;case 4:return MTLBlendFactorOne;
      case 5:return MTLBlendFactorOneMinusSourceAlpha;
      case 6:return src?MTLBlendFactorOneMinusDestinationColor:MTLBlendFactorOneMinusSourceColor;
      case 7:return MTLBlendFactorOneMinusDestinationAlpha;default:return MTLBlendFactorSourceAlphaSaturated;
    }
}
static int begin(pod_glide_gpu *g,int clear,uint32_t argb,float z) {
    if(!g->commands)g->commands=[[g->queue commandBuffer] retain];
    if(!g->commands)return -1;
    MTLRenderPassDescriptor *p=[MTLRenderPassDescriptor renderPassDescriptor];
    p.colorAttachments[0].texture=g->color[1];
    p.colorAttachments[0].loadAction=clear?MTLLoadActionClear:MTLLoadActionLoad;
    p.colorAttachments[0].storeAction=MTLStoreActionStore;
    p.colorAttachments[0].clearColor=MTLClearColorMake(((argb>>16)&255)/255.,((argb>>8)&255)/255.,(argb&255)/255.,(argb>>24)/255.);
    p.depthAttachment.texture=g->depth;p.depthAttachment.loadAction=clear?MTLLoadActionClear:MTLLoadActionLoad;
    p.depthAttachment.storeAction=MTLStoreActionStore;p.depthAttachment.clearDepth=z;
    g->encoder=[[g->commands renderCommandEncoderWithDescriptor:p] retain];
    return g->encoder?0:-1;
}
static id<MTLRenderPipelineState> pipeline(pod_glide_gpu *g,const pod_glide_state *s) {
    unsigned key=s->p[16]|s->p[17]<<4|s->p[18]<<8|s->p[19]<<12;
    NSNumber *k=[NSNumber numberWithUnsignedInt:key];
    id<MTLRenderPipelineState> result=[g->pipelines objectForKey:k];
    if(result)return result;
    if([g->pipelines count]>=64)return nil;
    MTLRenderPipelineDescriptor *d=[[[MTLRenderPipelineDescriptor alloc] init] autorelease];
    d.vertexFunction=[g->library newFunctionWithName:@"glide_vs"];
    d.fragmentFunction=[g->library newFunctionWithName:@"glide_fs"];
    d.colorAttachments[0].pixelFormat=MTLPixelFormatBGRA8Unorm;
    d.depthAttachmentPixelFormat=MTLPixelFormatDepth32Float;
    d.colorAttachments[0].blendingEnabled=key!=0x0404;
    d.colorAttachments[0].sourceRGBBlendFactor=blend(s->p[16],1);
    d.colorAttachments[0].destinationRGBBlendFactor=blend(s->p[17],0);
    d.colorAttachments[0].sourceAlphaBlendFactor=blend(s->p[18],1);
    d.colorAttachments[0].destinationAlphaBlendFactor=blend(s->p[19],0);
    NSError *e=nil;result=[g->device newRenderPipelineStateWithDescriptor:d error:&e];
    [d.vertexFunction release];[d.fragmentFunction release];
    if(!result){fprintf(stderr,"[glide-metal] pipeline: %s\n",[[e description] UTF8String]);return nil;}
    [g->pipelines setObject:result forKey:k];[result release];return result;
}
static int batch(pod_glide_gpu *g) {
    if(!g->count)return 0;
    const pod_glide_state *s=&g->state;
    if(s->p[22]<=s->p[20]||s->p[23]<=s->p[21]){g->count=0;return 0;}
    if(!g->encoder&&begin(g,0,0,1))return -1;
    id<MTLRenderPipelineState> p=pipeline(g,s);if(!p)return -1;
    unsigned dk=s->p[2]?(s->p[3]&7)|((s->p[14]!=0)<<3):7;
    if(!g->depths[dk]) {
        MTLDepthStencilDescriptor *d=[[[MTLDepthStencilDescriptor alloc] init] autorelease];
        static MTLCompareFunction cmps[]={MTLCompareFunctionNever,MTLCompareFunctionLess,MTLCompareFunctionEqual,MTLCompareFunctionLessEqual,MTLCompareFunctionGreater,MTLCompareFunctionNotEqual,MTLCompareFunctionGreaterEqual,MTLCompareFunctionAlways};
        d.depthCompareFunction=cmps[dk&7];d.depthWriteEnabled=(dk&8)!=0;
        g->depths[dk]=[g->device newDepthStencilStateWithDescriptor:d];
    }
    unsigned sk=s->p[24]|s->p[25]<<1|s->p[26]<<2|s->p[27]<<3;
    if(!g->samplers[sk]) {
        MTLSamplerDescriptor *d=[[[MTLSamplerDescriptor alloc] init] autorelease];
        d.minFilter=s->p[24]?MTLSamplerMinMagFilterLinear:MTLSamplerMinMagFilterNearest;
        d.magFilter=s->p[25]?MTLSamplerMinMagFilterLinear:MTLSamplerMinMagFilterNearest;
        d.mipFilter=MTLSamplerMipFilterNearest;
        d.sAddressMode=s->p[26]?MTLSamplerAddressModeClampToEdge:MTLSamplerAddressModeRepeat;
        d.tAddressMode=s->p[27]?MTLSamplerAddressModeClampToEdge:MTLSamplerAddressModeRepeat;
        g->samplers[sk]=[g->device newSamplerStateWithDescriptor:d];
    }
    id<MTLBuffer> vertices=[g->device newBufferWithBytes:g->vertices length:g->count*sizeof *g->vertices options:MTLResourceStorageModeShared];
    if(!vertices||!g->depths[dk]||!g->samplers[sk]){[vertices release];return -1;}
    [g->encoder setRenderPipelineState:p];[g->encoder setDepthStencilState:g->depths[dk]];
    [g->encoder setCullMode:MTLCullModeNone];
    [g->encoder setScissorRect:(MTLScissorRect){s->p[20],s->p[21],s->p[22]-s->p[20],s->p[23]-s->p[21]}];
    [g->encoder setVertexBuffer:vertices offset:0 atIndex:0];
    [g->encoder setVertexBytes:s length:sizeof *s atIndex:1];
    [g->encoder setFragmentBytes:s length:sizeof *s atIndex:0];
    id<MTLTexture> tex=s->p[15]&&s->p[15]<=TEX_SLOTS?g->textures[s->p[15]-1]:g->white;
    if(!tex){[vertices release];return -1;}
    [g->encoder setFragmentTexture:tex atIndex:0];[g->encoder setFragmentSamplerState:g->samplers[sk] atIndex:0];
    [g->encoder drawPrimitives:g->lines?MTLPrimitiveTypeLine:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:g->count];
    [vertices release];g->count=0;return 0;
}
static int finish(pod_glide_gpu *g) {
    if(batch(g))return -1;
    if(g->encoder){[g->encoder endEncoding];[g->encoder release];g->encoder=nil;}
    if(!g->commands)return 0;
    [g->commands commit];[g->commands waitUntilCompleted];
    int bad=g->commands.status!=MTLCommandBufferStatusCompleted;
    if(bad)fprintf(stderr,"[glide-metal] GPU command failed: %s\n",g->commands.error?[[g->commands.error description] UTF8String]:"unknown GPU error");
    [g->commands release];g->commands=nil;return bad?-1:0;
}
void pod_glide_gpu_close(pod_glide_gpu *g) {
    if(!g)return;
    @autoreleasepool {
        if(finish(g)) {
            /* A rejected batch may leave an uncommitted encoder. Discard
             * its retained native objects before releasing the device. */
            if(g->encoder){[g->encoder endEncoding];[g->encoder release];}
            [g->commands release];
        }
        [g->color[0] release];[g->color[1] release];[g->depth release];
        for(unsigned i=0;i<TEX_SLOTS;i++)[g->textures[i] release];
        for(unsigned i=0;i<16;i++){[g->depths[i] release];[g->samplers[i] release];}
        [g->white release];[g->library release];[g->pipelines release];[g->queue release];[g->device release];free(g);
    }
}
pod_glide_gpu *pod_glide_gpu_open(unsigned w,unsigned h,char *error,size_t cap) {
    if(error&&cap)error[0]=0;
    if(!w||!h||w>4096||h>4096){if(error&&cap)snprintf(error,cap,"invalid dimensions");return NULL;}
    @autoreleasepool {
        pod_glide_gpu *g=calloc(1,sizeof *g);if(!g)return NULL;
        g->device=MTLCreateSystemDefaultDevice();g->queue=[g->device newCommandQueue];g->width=w;g->height=h;
        NSError *e=nil;g->library=[g->device newLibraryWithSource:[NSString stringWithUTF8String:pod_glide_shader] options:nil error:&e];
        MTLTextureDescriptor *d=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm width:w height:h mipmapped:NO];
        d.usage=MTLTextureUsageRenderTarget;d.storageMode=MTLStorageModeShared;
        g->color[0]=[g->device newTextureWithDescriptor:d];g->color[1]=[g->device newTextureWithDescriptor:d];
        d.pixelFormat=MTLPixelFormatDepth32Float;d.storageMode=MTLStorageModePrivate;g->depth=[g->device newTextureWithDescriptor:d];
        d=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm width:1 height:1 mipmapped:NO];
        d.storageMode=MTLStorageModeShared;d.usage=MTLTextureUsageShaderRead;g->white=[g->device newTextureWithDescriptor:d];
        uint32_t white=0xffffffff;[g->white replaceRegion:MTLRegionMake2D(0,0,1,1) mipmapLevel:0 withBytes:&white bytesPerRow:4];
        g->pipelines=[[NSMutableDictionary alloc] init];
        if(!g->device||!g->queue||!g->library||!g->color[0]||!g->color[1]||!g->depth||!g->white) {
            if(error&&cap)snprintf(error,cap,"Metal unavailable or shader error: %s",e?[[e description] UTF8String]:"no device/resources");
            pod_glide_gpu_close(g);return NULL;
        }
        if(pod_glide_gpu_clear(g,0xff000000)||pod_glide_gpu_swap(g)||pod_glide_gpu_clear(g,0xff000000)) {
            if(error&&cap)snprintf(error,cap,"initial GPU clear failed");pod_glide_gpu_close(g);return NULL;
        }
        fprintf(stderr,"[glide-metal] %ux%u color pair + depth; device=%s\n",w,h,[[g->device name] UTF8String]);return g;
    }
}
int pod_glide_gpu_clear_depth(pod_glide_gpu *g,uint32_t c,float z) {
    if(!g||!isfinite(z)||z<0||z>1)return -1;
    @autoreleasepool {
        if(batch(g))return -1;
        if(g->encoder){[g->encoder endEncoding];[g->encoder release];g->encoder=nil;}
        return begin(g,1,c,z);
    }
}
int pod_glide_gpu_clear(pod_glide_gpu *g,uint32_t c){return pod_glide_gpu_clear_depth(g,c,1);}
int pod_glide_gpu_read_buffer(pod_glide_gpu *g,unsigned b,void *pixels,size_t size) {
    if(!g||b>1||!pixels||size<(size_t)g->width*g->height*4)return -1;
    @autoreleasepool {if(finish(g))return -1;[g->color[b] getBytes:pixels bytesPerRow:g->width*4 fromRegion:MTLRegionMake2D(0,0,g->width,g->height) mipmapLevel:0];return 0;}
}
int pod_glide_gpu_read(pod_glide_gpu *g,void *p,size_t n){return pod_glide_gpu_read_buffer(g,1,p,n);}
int pod_glide_gpu_write_buffer(pod_glide_gpu *g,unsigned b,const void *p,size_t n) {
    if(!g||b>1||!p||n<(size_t)g->width*g->height*4)return -1;
    @autoreleasepool {if(finish(g))return -1;[g->color[b] replaceRegion:MTLRegionMake2D(0,0,g->width,g->height) mipmapLevel:0 withBytes:p bytesPerRow:g->width*4];return 0;}
}
int pod_glide_gpu_swap(pod_glide_gpu *g) {
    if(!g)return -1;@autoreleasepool {if(finish(g))return -1;id<MTLTexture> t=g->color[0];g->color[0]=g->color[1];g->color[1]=t;return 0;}
}
int pod_glide_gpu_texture(pod_glide_gpu *g,unsigned slot,unsigned w,unsigned h,unsigned levels,const uint32_t *pixels,size_t bytes) {
    if(!g||slot>=TEX_SLOTS||!pixels||!w||!h||w>256||h>256||!levels||levels>9)return -1;
    unsigned maximum=1;for(unsigned edge=w>h?w:h;edge>1;edge/=2)maximum++;
    if(levels>maximum)return -1;
    size_t need=0;unsigned x=w,y=h;for(unsigned i=0;i<levels;i++){need+=(size_t)x*y*4;x=x>1?x/2:1;y=y>1?y/2:1;}
    if(bytes<need)return -1;
    @autoreleasepool {
        if(finish(g))return -1;
        MTLTextureDescriptor *d=[MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm width:w height:h mipmapped:levels>1];
        d.mipmapLevelCount=levels;d.storageMode=MTLStorageModeShared;d.usage=MTLTextureUsageShaderRead;
        id<MTLTexture> t=[g->device newTextureWithDescriptor:d];if(!t)return -1;
        const uint8_t *p=(const uint8_t *)pixels;x=w;y=h;
        for(unsigned i=0;i<levels;i++){[t replaceRegion:MTLRegionMake2D(0,0,x,y) mipmapLevel:i withBytes:p bytesPerRow:x*4];p+=(size_t)x*y*4;x=x>1?x/2:1;y=y>1?y/2:1;}
        [g->textures[slot] release];g->textures[slot]=t;return 0;
    }
}
int pod_glide_gpu_draw(pod_glide_gpu *g,const pod_glide_state *s,const pod_glide_vertex *v,unsigned n,int lines) {
    if(!g||!s||!v||!n||n>BATCH_VERTICES||n%(lines?2:3)||s->p[0]!=g->width||s->p[1]!=g->height||
       s->p[20]>g->width||s->p[21]>g->height||s->p[22]>g->width||s->p[23]>g->height||s->p[3]>7||s->p[15]>TEX_SLOTS||
       s->p[24]>1||s->p[25]>1||s->p[26]>1||s->p[27]>1)return -1;
    if(s->p[GS_TEXTURE]&&!g->textures[s->p[GS_TEXTURE]-1])return -1;
    for(unsigned i=16;i<20;i++)if(s->p[i]>7&&s->p[i]!=15)return -1;
    @autoreleasepool {
        if(g->count&&(g->lines!=lines||memcmp(&g->state,s,sizeof *s)||g->count+n>BATCH_VERTICES))if(batch(g))return -1;
        g->state=*s;g->lines=lines;memcpy(g->vertices+g->count,v,n*sizeof *v);g->count+=n;return 0;
    }
}
