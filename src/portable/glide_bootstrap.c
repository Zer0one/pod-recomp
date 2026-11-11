#include "glide_texture.h"
#include "glide_vertex.h"
/* Glide 2 guest ABI and game-facing state. The GPU backend accepts native,
 * owned data only. No SDL window or Metal type belongs in this service. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "glide_bootstrap.h"
#include "glide_gpu.h"
static unsigned target_width=640,target_height=480;
#define W target_width
#define H target_height
#define TMU_BYTES (2u*1024u*1024u)
#define CACHE 128u
static pod_glide_gpu *gpu;
static pod_glide_state state;
static uint8_t *tmu,*pixels,*presentation,*lfb_storage[2];
static uint16_t *lfb[2];
static int locked[2],write_lock[2],window_open,texture_combine_supported=1;
static uint32_t palette[256],epoch,palette_epoch;
static uint64_t next_swap,triangles,frames;
static uint64_t surface_generation;
static float gamma_value=1;
static uint8_t gamma_table[256];
static unsigned current_address,current_info[4],next_cache;
static struct {unsigned address,info[4],epoch,palette_epoch,valid;} cache[CACHE];
static float fl(uint32_t x){float f;memcpy(&f,&x,4);return f;}
static void check(int result){if(result)hle_fatal("Glide GPU operation failed");}
static void one(unsigned index,unsigned maximum){if(A32(0)>maximum)hle_fatal("Glide state %u out of range (%u)",index,A32(0));state.p[index]=A32(0);}
static void defaults(void) {
    gamma_value=1;
    memset(&state,0,sizeof state);state.p[GS_WIDTH]=W;state.p[GS_HEIGHT]=H;
    state.p[GS_RIGHT]=W;state.p[GS_BOTTOM]=H;state.p[GS_DEPTH_CMP]=3;
    state.p[GS_COLOR_FUNC]=1;state.p[GS_ALPHA_SOURCE]=1;state.p[GS_TEX_FUNC]=1;
    state.p[GS_SRC_RGB]=state.p[GS_SRC_ALPHA]=4;
    state.f[0]=state.f[1]=state.f[2]=state.f[3]=1;
    state.f[4]=state.f[5]=1.0f/256;
    for(unsigned i=0;i<256;i++)gamma_table[i]=(uint8_t)i;
}
static void init(void) {
    fprintf(stderr,"[glide] Init thread=%u\n",hle_thread_id());
    if(!gpu){char error[256];gpu=pod_glide_gpu_open(W,H,error,sizeof error);if(!gpu)hle_fatal("Glide GPU: %s",error);
        tmu=calloc(1,TMU_BYTES);pixels=malloc(W*H*4);if(!tmu||!pixels)hle_fatal("Glide allocation failed");defaults();}
    RET(0,0);
}
static void close_window(void) {
    fprintf(stderr,"[glide] WinClose thread=%u\n",hle_thread_id());
    surface_generation++;next_swap=0;
    window_open=0;hle_gdi_window_surface(NULL,0,0,0,0);
    if(presentation){hle_free(presentation);presentation=NULL;}
    for(unsigned i=0;i<2;i++){if(lfb_storage[i])hle_free(lfb_storage[i]);lfb_storage[i]=NULL;lfb[i]=NULL;locked[i]=0;}
    RET(0,0);
}
static void shutdown_glide(void) {
    /* Do not call a RET-bearing shim from another shim. */
    fprintf(stderr,"[glide] Shutdown thread=%u frames=%llu\n",hle_thread_id(),(unsigned long long)frames);
    surface_generation++;next_swap=0;
    hle_gdi_window_surface(NULL,0,0,0,0);window_open=0;
    if(presentation)hle_free(presentation);presentation=NULL;
    for(unsigned i=0;i<2;i++){if(lfb_storage[i])hle_free(lfb_storage[i]);lfb_storage[i]=NULL;lfb[i]=NULL;locked[i]=0;}
    pod_glide_gpu_close(gpu);gpu=NULL;free(tmu);tmu=NULL;free(pixels);pixels=NULL;
    memset(cache,0,sizeof cache);RET(0,0);
}
static void query_hardware(void) {
    uint32_t a=A32(0);if(!gpu||!a)RET(0,1);
    memset(hle_ptr(a),0,148);MEM32(a)=1;MEM32(a+8)=4;MEM32(a+12)=2;
    MEM32(a+16)=1;MEM32(a+24)=1;MEM32(a+28)=2;RET(1,1);
}
static void select_board(void){if(A32(0))hle_fatal("unsupported Glide board %u",A32(0));if(!gpu)hle_fatal("Glide board selection without initialized service (thread=%u)",hle_thread_id());RET(0,1);}
static void open_window(void) {
    fprintf(stderr,"[glide] WinOpen resolution=%u refresh=%u color=%u origin=%u buffers=%u aux=%u\n",A32(1),A32(2),A32(3),A32(4),A32(5),A32(6));
    if(!gpu||window_open||A32(3)!=0||A32(4)!=0||A32(5)!=2||A32(6)>1)RET(0,7);
    unsigned nw,nh;
    switch(A32(1)){case 3:nw=512;nh=384;break;case 7:nw=640;nh=480;break;case 8:nw=800;nh=600;break;default:RET(0,7);}
    if(nw!=W||nh!=H){
        pod_glide_gpu_close(gpu);char error[256];gpu=pod_glide_gpu_open(nw,nh,error,sizeof error);
        if(!gpu)hle_fatal("Glide display mode: %s",error);
        W=nw;H=nh;memset(cache,0,sizeof cache);state.p[GS_TEXTURE]=0;
    }
    free(pixels);pixels=malloc(W*H*4);if(!pixels)hle_fatal("Glide readback allocation");
    presentation=hle_alloc(W*H*4);
    for(unsigned i=0;i<2;i++){lfb_storage[i]=hle_alloc(W*H*2+8192);if(!lfb_storage[i])hle_fatal("LFB allocation failed");lfb[i]=(uint16_t *)(lfb_storage[i]+4096);}
    if(!presentation)hle_fatal("presentation allocation failed");
    surface_generation++;next_swap=0;
    window_open=1;defaults();check(pod_glide_gpu_clear(gpu,0xff000000));
    hle_gdi_window_surface(presentation,W,H,W*4,32);
    uint32_t a[7]={A32(0),0,0,0,W,H,0x14};hle_call_guest(hle_resolve("SetWindowPos"),7,a);
    RET(1,7);
}
static void width(void){RET(window_open?W:0,0);}
static void height(void){RET(window_open?H:0,0);}
/* Glide 2's grSstControl succeeds without touching hardware when its window
 * is not initialized. Late activation messages also arrive during teardown. */
static void control(void){RET(!window_open||(A32(0)>=1&&A32(0)<=4),1);}
static void publish(void) {
    if(!window_open)return;
    check(pod_glide_gpu_read_buffer(gpu,0,presentation,W*H*4));
    if(gamma_value!=1)for(size_t i=0;i<W*H*4;i+=4)for(unsigned c=0;c<3;c++)presentation[i+c]=gamma_table[presentation[i+c]];
    hle_gdi_window_touched();
}
static void swap(void) {
    if(!window_open)hle_fatal("Glide swap without an open window");
    unsigned interval=A32(0);if(interval>4)hle_fatal("unsupported swap interval %u",interval);
    struct timespec ts;clock_gettime(CLOCK_MONOTONIC,&ts);
    uint64_t now=(uint64_t)ts.tv_sec*1000000000ull+ts.tv_nsec,period=1000000000ull/60*(interval?interval:1);
    if(!next_swap||now>next_swap+period)next_swap=now;next_swap+=period;
    uint64_t due=next_swap,generation=surface_generation;hle_block_begin();
    while(now<due){uint64_t d=due-now;ts.tv_sec=d/1000000000ull;ts.tv_nsec=d%1000000000ull;nanosleep(&ts,NULL);clock_gettime(CLOCK_MONOTONIC,&ts);now=(uint64_t)ts.tv_sec*1000000000ull+ts.tv_nsec;}
    hle_block_end();
    /* Close/reopen can run while this call gives up the guest machine. Its
     * old frame must not swap a replacement context or touch freed storage. */
    if(!window_open||generation!=surface_generation)RET(0,1);
    check(pod_glide_gpu_swap(gpu));publish();frames++;
    if(frames%120==0)fprintf(stderr,"[glide] frames=%llu triangles=%llu\n",(unsigned long long)frames,(unsigned long long)triangles);
    RET(0,1);
}
static void clear_buffer(void){if(!window_open)hle_fatal("Glide clear without an open window");check(pod_glide_gpu_clear_depth(gpu,(A32(0)&0xffffff)|(A32(1)&255)<<24,(A32(2)&65535)/65535.0f));RET(0,3);}
static void depth_mode(void){one(GS_DEPTH_MODE,2);fprintf(stderr,"[glide] depth mode=%u\n",A32(0));RET(0,1);}
static void depth_cmp(void){one(GS_DEPTH_CMP,7);RET(0,1);}
static void depth_mask(void){one(GS_DEPTH_WRITE,1);RET(0,1);}
static void key_mode(void){one(GS_KEY_MODE,1);RET(0,1);}
static void key_color(void){state.p[GS_KEY_COLOR]=A32(0);RET(0,1);}
static void fog_mode(void){one(GS_FOG_MODE,3);RET(0,1);}
static void fog_color(void){state.p[GS_FOG_COLOR]=A32(0);RET(0,1);}
static void fog_table(void){if(!A32(0))hle_fatal("null fog table");for(unsigned i=0;i<64;i++)state.f[8+i]=MEM8(A32(0)+i)/255.0f;RET(0,1);}
static void constant_color(void){uint32_t c=A32(0);state.f[0]=((c>>16)&255)/255.f;state.f[1]=((c>>8)&255)/255.f;state.f[2]=(c&255)/255.f;state.f[3]=(c>>24)/255.f;RET(0,1);}
static void blend_state(void){for(unsigned i=0;i<4;i++){unsigned v=A32(i);if(v>7&&v!=15)hle_fatal("unsupported blend factor %u",v);state.p[GS_SRC_RGB+i]=v;}RET(0,4);}
static void clip(void){state.p[GS_LEFT]=A32(0)<W?A32(0):W;state.p[GS_TOP]=A32(1)<H?A32(1):H;state.p[GS_RIGHT]=A32(2)<W?A32(2):W;state.p[GS_BOTTOM]=A32(3)<H?A32(3):H;RET(0,4);}
static void color_combine(void){if((A32(0)>9&&A32(0)!=16)||(A32(1)>5&&A32(1)<8)||A32(1)>13||A32(2)>2||A32(3)>2||A32(4)>1)hle_fatal("unsupported color combine");for(unsigned i=0;i<5;i++)state.p[GS_COLOR_FUNC+i]=A32(i);RET(0,5);}
static void utility_color(void){unsigned f=A32(0);state.p[GS_COLOR_INVERT]=0;state.p[GS_COLOR_LOCAL]=0;
    if(f==0){state.p[GS_COLOR_FUNC]=0;}
    else if(f==1||f==2||f==3){state.p[GS_COLOR_FUNC]=1;state.p[GS_COLOR_LOCAL]=f==1;}
    else if(f==4){state.p[GS_COLOR_FUNC]=3;state.p[GS_COLOR_FACTOR]=8;state.p[GS_COLOR_OTHER]=1;}
    else if(f==5||f==6||f==7){state.p[GS_COLOR_FUNC]=3;state.p[GS_COLOR_FACTOR]=1;state.p[GS_COLOR_LOCAL]=f==5;state.p[GS_COLOR_OTHER]=1;}
    else hle_fatal("unsupported utility color combine %u",f);RET(0,1);
}
static void alpha_source(void){one(GS_ALPHA_SOURCE,3);RET(0,1);}
static void alpha_lighting(void){if(A32(0))hle_fatal("alpha-controlled RGB lighting not implemented");RET(0,1);}
static void gamma_correction(void){float g=fl(A32(0));if(!isfinite(g)||g<0.1f||g>10)hle_fatal("invalid gamma");gamma_value=g;for(unsigned i=0;i<256;i++)gamma_table[i]=(uint8_t)lroundf(powf(i/255.f,1/g)*255);RET(0,1);}
static void tmu_zero(void){if(A32(0))hle_fatal("only TMU0 is exposed");}
static void tex_clamp(void){tmu_zero();if(A32(1)>1||A32(2)>1)hle_fatal("unsupported texture clamp");state.p[GS_CLAMP_S]=A32(1);state.p[GS_CLAMP_T]=A32(2);RET(0,3);}
static void tex_filter(void){tmu_zero();if(A32(1)>1||A32(2)>1)hle_fatal("unsupported texture filter");state.p[GS_MIN_FILTER]=A32(1);state.p[GS_MAG_FILTER]=A32(2);RET(0,3);}
static void tex_mipmap(void){tmu_zero();if(A32(1)>2||A32(2))hle_fatal("mip dithering/trilinear blend unsupported");state.p[GS_MIPMAP]=A32(1);RET(0,3);}
static void tex_utility(void){tmu_zero();unsigned f=A32(1);texture_combine_supported=f<=3||f==10;state.p[GS_TEX_FUNC]=f;state.p[GS_TEX_INVERT]=state.p[GS_TEX_ALPHA_INVERT]=0;RET(0,2);}
static void tex_combine(void){tmu_zero();texture_combine_supported=A32(1)==1&&A32(3)==1;state.p[GS_TEX_FUNC]=1;state.p[GS_TEX_INVERT]=A32(5);state.p[GS_TEX_ALPHA_INVERT]=A32(6);RET(0,7);}
static unsigned bpp(unsigned fmt){unsigned n=pod_glide_texture_bpp(fmt);if(!n)hle_fatal("unsupported texture format %u",fmt);return n;}
static void dimensions(unsigned lod,unsigned aspect,unsigned *w,unsigned *h){if(pod_glide_texture_extent(lod,aspect,w,h))hle_fatal("invalid texture dimensions");}
static unsigned texture_size(unsigned small,unsigned large,unsigned aspect,unsigned fmt){unsigned n;if(pod_glide_texture_bytes(small,large,aspect,fmt,&n))hle_fatal("invalid mip chain/format");return n;}
static void tex_mem(void){RET(texture_size(A32(0),A32(1),A32(2),A32(3)),4);}
static void tex_min(void){tmu_zero();RET(0,1);}
static void tex_table(void){tmu_zero();if(A32(1)!=2||!A32(2))hle_fatal("unsupported texture table %u",A32(1));memcpy(palette,hle_ptr(A32(2)),sizeof palette);palette_epoch++;RET(0,3);}
static void tex_download(void){tmu_zero();unsigned address=A32(1),info=A32(3);if(A32(2)!=3||!info)hle_fatal("split mip chain unsupported");unsigned small=MEM32(info),large=MEM32(info+4),aspect=MEM32(info+8),fmt=MEM32(info+12);unsigned size=texture_size(small,large,aspect,fmt);if(address>TMU_BYTES||size>TMU_BYTES-address||!MEM32(info+16))hle_fatal("texture memory bounds");const uint8_t *src=hle_ptr(MEM32(info+16));for(unsigned l=large;l<=small;l++){unsigned w,h;dimensions(l,aspect,&w,&h);unsigned n=w*h*bpp(fmt);memcpy(tmu+address,src,n);src+=n;address+=(n+7)&~7u;}epoch++;RET(0,4);}
static uint32_t decode(unsigned value,unsigned fmt){return pod_glide_texture_texel(value,fmt,palette);}
static void bind_texture(void){
    if(!state.p[GS_TEXTURE])return;
    unsigned slot=CACHE;
    for(unsigned i=0;i<CACHE;i++)if(cache[i].valid&&cache[i].address==current_address&&!memcmp(cache[i].info,current_info,sizeof current_info)&&cache[i].epoch==epoch&&cache[i].palette_epoch==palette_epoch){slot=i;break;}
    if(slot==CACHE){slot=next_cache++%CACHE;unsigned small=current_info[0],large=current_info[1],aspect=current_info[2],fmt=current_info[3];unsigned size=texture_size(small,large,aspect,fmt);if(current_address>TMU_BYTES||size>TMU_BYTES-current_address)hle_fatal("texture source bounds");unsigned address=current_address,w,h;dimensions(large,aspect,&w,&h);uint32_t *rgba=malloc(size*4+64);if(!rgba)hle_fatal("texture staging allocation");size_t count=0;
        for(unsigned l=large;l<=small;l++){unsigned x,y;dimensions(l,aspect,&x,&y);for(unsigned i=0;i<x*y;i++){unsigned v=tmu[address+i*bpp(fmt)];if(bpp(fmt)==2)v|=(unsigned)tmu[address+i*2+1]<<8;rgba[count++]=decode(v,fmt);}address+=(x*y*bpp(fmt)+7)&~7u;}
        check(pod_glide_gpu_texture(gpu,slot,w,h,small-large+1,rgba,count*4));free(rgba);
        cache[slot].address=current_address;memcpy(cache[slot].info,current_info,sizeof current_info);cache[slot].epoch=epoch;cache[slot].palette_epoch=palette_epoch;cache[slot].valid=1;
    }state.p[GS_TEXTURE]=slot+1;
    if(pod_glide_texture_scales(current_info[2],&state.f[4],&state.f[5]))hle_fatal("texture coordinate aspect");
}
static void tex_source(void){tmu_zero();if(A32(2)!=3||!A32(3))hle_fatal("unsupported texture source");current_address=A32(1);for(unsigned i=0;i<4;i++)current_info[i]=MEM32(A32(3)+i*4);state.p[GS_TEXTURE]=1;bind_texture();RET(0,4);}
static void primitive(unsigned n,int lines){
    if(!window_open)hle_fatal("Glide draw without an open window");
    if(state.p[GS_FOG_MODE]>1)hle_fatal("Glide table/Z fog not yet implemented (mode=%u)",state.p[GS_FOG_MODE]);
    if(pod_glide_uses_texture(&state)&&!texture_combine_supported)hle_fatal("unsupported TMU combine state");
    int textured=pod_glide_uses_texture(&state);
    if(textured){if(!state.p[GS_TEXTURE])hle_fatal("textured draw without a source");bind_texture();}
    pod_glide_state draw_state=state;
    if(!textured){draw_state.p[GS_KEY_MODE]=0;draw_state.p[GS_TEXTURE]=0;}
    pod_glide_vertex v[3];
    for(unsigned i=0;i<n;i++) {
        uint32_t a=A32(i);if(!a)hle_fatal("null Glide vertex");
        float f[12];memcpy(f,hle_ptr(a),sizeof f);
        unsigned bad=pod_glide_vertex_decode(&state,f,&v[i]);
        if(bad)hle_fatal("nonfinite active Glide vertex attribute %u (depth=%u color=%u/%u/%u alpha=%u)",bad-1,state.p[GS_DEPTH_MODE],state.p[GS_COLOR_FUNC],state.p[GS_COLOR_LOCAL],state.p[GS_COLOR_OTHER],state.p[GS_ALPHA_SOURCE]);
    }
    if(triangles==0)fprintf(stderr,"[glide] first primitive: ooz=%g oow=%g sow=%g tow=%g q=%g depthMode=%u compare=%u\n",v[0].ooz,v[0].oow,v[0].sow,v[0].tow,v[0].q,state.p[GS_DEPTH_MODE],state.p[GS_DEPTH_CMP]);
    check(pod_glide_gpu_draw(gpu,&draw_state,v,n,lines));if(!lines)triangles++;
}
static void triangle(void){primitive(3,0);RET(0,3);}
static void line(void){primitive(2,1);RET(0,2);}
static uint32_t *read_buffer(unsigned b){check(pod_glide_gpu_read_buffer(gpu,b,pixels,W*H*4));return (uint32_t *)pixels;}
static void lfb_lock(void){unsigned type=A32(0),b=A32(1),mode=A32(2),info=A32(5);if(!window_open||b>1||locked[b]||type&~0x11u||mode!=0||A32(3)!=0||A32(4)||!info||MEM32(info)<20)RET(0,6);uint32_t *src=read_buffer(b);for(unsigned i=0;i<W*H;i++){uint32_t c=src[i];lfb[b][i]=((c>>19)&31)<<11|((c>>10)&63)<<5|((c>>3)&31);}locked[b]=1;write_lock[b]=type&1;MEM32(info+4)=hle_va(lfb[b]);MEM32(info+8)=W*2;MEM32(info+12)=0;MEM32(info+16)=0;RET(1,6);}
static void lfb_unlock(void){unsigned b=A32(1);if(b>1||!locked[b]||(A32(0)&1)!=(unsigned)write_lock[b])RET(0,2);if(write_lock[b]){uint32_t *dst=(uint32_t *)pixels;for(unsigned i=0;i<W*H;i++)dst[i]=decode(lfb[b][i],10);check(pod_glide_gpu_write_buffer(gpu,b,pixels,W*H*4));if(!b)publish();}locked[b]=0;RET(1,2);}
static void lfb_region(void){unsigned b=A32(0),x=A32(1),y=A32(2),fmt=A32(3),w=A32(4),h=A32(5),stride=A32(6),bytes=fmt==4?3:fmt==5?4:2;if(!window_open||b>1||fmt>5||fmt==3||x>W||y>H||w>W-x||h>H-y||stride<w*bytes||!A32(7))RET(0,8);uint32_t *dst=read_buffer(b);const uint8_t *src=hle_ptr(A32(7));for(unsigned j=0;j<h;j++)for(unsigned i=0;i<w;i++){unsigned v=0;memcpy(&v,src+j*stride+i*bytes,bytes);uint32_t c=fmt==0?decode(v,10):fmt==1?decode(v|32768,11):fmt==2?decode(v,11):fmt==4?v|0xff000000u:v;dst[(y+j)*W+x+i]=c;}check(pod_glide_gpu_write_buffer(gpu,b,pixels,W*H*4));if(!b)publish();RET(1,8);}
const win32hle_shim pod_glide_bootstrap_shims[]={
 {"_grGlideInit@0",init},{"_grGlideShutdown@0",shutdown_glide},{"_grSstQueryHardware@4",query_hardware},{"_grSstSelect@4",select_board},
 {"_grSstWinOpen@28",open_window},{"_grSstWinClose@0",close_window},{"_grSstScreenWidth@0",width},{"_grSstScreenHeight@0",height},{"_grSstControl@4",control},
 {"_grDepthBufferMode@4",depth_mode},{"_grDepthBufferFunction@4",depth_cmp},{"_grDepthMask@4",depth_mask},{"_grBufferClear@12",clear_buffer},{"_grBufferSwap@4",swap},
 {"_grDrawTriangle@12",triangle},{"_grDrawLine@8",line},{"_grClipWindow@16",clip},{"_grAlphaBlendFunction@16",blend_state},
 {"_grChromakeyMode@4",key_mode},{"_grChromakeyValue@4",key_color},{"_grConstantColorValue@4",constant_color},{"_grColorCombine@20",color_combine},
 {"_guColorCombineFunction@4",utility_color},{"_guAlphaSource@4",alpha_source},{"_grAlphaControlsITRGBLighting@4",alpha_lighting},
 {"_grGammaCorrectionValue@4",gamma_correction},{"_grFogMode@4",fog_mode},{"_grFogColorValue@4",fog_color},{"_grFogTable@4",fog_table},
 {"_grTexClampMode@12",tex_clamp},{"_grTexFilterMode@12",tex_filter},{"_grTexMipMapMode@12",tex_mipmap},
 {"_grTexCombineFunction@8",tex_utility},{"_guTexCombineFunction@8",tex_utility},{"_grTexCombine@28",tex_combine},
 {"_grTexCalcMemRequired@16",tex_mem},{"_grTexMinAddress@4",tex_min},{"_grTexDownloadMipMap@16",tex_download},{"_grTexDownloadTable@12",tex_table},{"_grTexSource@16",tex_source},
 {"_grLfbLock@24",lfb_lock},{"_grLfbUnlock@8",lfb_unlock},{"_grLfbWriteRegion@32",lfb_region},{NULL,NULL}
};
