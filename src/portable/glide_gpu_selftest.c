/* Native GPU clear/readback, independent of POD assets or a window. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "glide_gpu.h"
#include "glide_vertex.h"
#include "glide_texture.h"
#include <math.h>
int main(void) {
    /* Constant RGB and iterated alpha must ignore garbage TMU fields. */
    pod_glide_state attributes={0};
    attributes.p[GS_COLOR_FUNC]=1;attributes.p[GS_COLOR_LOCAL]=1;
    attributes.p[GS_ALPHA_SOURCE]=1;attributes.p[GS_DEPTH_MODE]=2;
    float guest[12]={2,3,NAN,NAN,NAN,NAN,NAN,255,0.25f,NAN,NAN,NAN};
    pod_glide_vertex decoded;
    if(pod_glide_vertex_decode(&attributes,guest,&decoded)||decoded.q!=0.25f||decoded.sow!=0)return 1;
    attributes.p[GS_COLOR_FUNC]=3;attributes.p[GS_COLOR_OTHER]=1;
    attributes.p[GS_COLOR_FACTOR]=8;
    if(!pod_glide_uses_texture(&attributes)||pod_glide_vertex_decode(&attributes,guest,&decoded)!=10)return 1;
    guest[9]=12;guest[10]=8;
    if(pod_glide_vertex_decode(&attributes,guest,&decoded)||decoded.q!=guest[8])return 1;
    guest[0]=NAN;
    if(pod_glide_vertex_decode(&attributes,guest,&decoded)!=1)return 1;
    unsigned w,h,bytes;
    float scale_s,scale_t;
    if(pod_glide_texture_extent(0,0,&w,&h)||w!=256||h!=32)return 1;
    if(pod_glide_texture_extent(8,6,&w,&h)||w!=1||h!=1)return 1;
    if(pod_glide_texture_scales(0,&scale_s,&scale_t)||scale_s!=1.0f/256||scale_t!=1.0f/32)return 1;
    if(pod_glide_texture_scales(6,&scale_s,&scale_t)||scale_s!=1.0f/32||scale_t!=1.0f/256)return 1;
    if(pod_glide_texture_bytes(1,0,3,10,&bytes)||bytes!=163840)return 1;
    if(pod_glide_texture_bytes(8,8,0,10,&bytes)||bytes!=8)return 1;
    if(!pod_glide_texture_bytes(0,1,3,10,&bytes)||pod_glide_texture_bpp(1)!=0)return 1;
    uint32_t palette[256]={0};palette[7]=0x00123456;
    if(pod_glide_texture_texel(0xf800,10,palette)!=0xffff0000)return 1;
    if(pod_glide_texture_texel(0x83e0,11,palette)!=0xff00ff00)return 1;
    if(pod_glide_texture_texel(0x800f,12,palette)!=0x880000ff)return 1;
    if(pod_glide_texture_texel(0x407f,13,palette)!=0x407f7f7f)return 1;
    if(pod_glide_texture_texel(7,5,palette)!=0xff123456)return 1;
    if(pod_glide_texture_texel(0x8007,14,palette)!=0x80123456)return 1;
    char error[192];
    if (pod_glide_gpu_open(0,16,error,sizeof error)) return 1;
    pod_glide_gpu *gpu=pod_glide_gpu_open(32,16,error,sizeof error);
    if (!gpu) { fprintf(stderr,"SKIP: %s\n",error); return 77; }
    uint8_t image[32*16*4]; memset(image,0xa5,sizeof image);
    int failed=pod_glide_gpu_clear(gpu,0xff112233u)!=0;
    failed|=pod_glide_gpu_read(gpu,image,sizeof image-1)!=-1;
    failed|=image[0]!=0xa5;
    failed|=pod_glide_gpu_read(gpu,image,sizeof image)!=0;
    for (size_t i=0;i<sizeof image;i+=4)
        if (image[i]!=0x33 || image[i+1]!=0x22 || image[i+2]!=0x11 || image[i+3]!=0xff) failed=1;
    failed|=pod_glide_gpu_clear(gpu,0xffeeddccu)!=0;
    failed|=pod_glide_gpu_read(gpu,image,sizeof image)!=0;
    for (size_t i=0;i<sizeof image;i+=4)
        if (image[i]!=0xcc || image[i+1]!=0xdd || image[i+2]!=0xee || image[i+3]!=0xff) failed=1;
    pod_glide_state st={0};
    st.p[GS_WIDTH]=32;st.p[GS_HEIGHT]=16;st.p[GS_RIGHT]=32;st.p[GS_BOTTOM]=16;
    st.p[GS_DEPTH_MODE]=1;st.p[GS_DEPTH_CMP]=1;st.p[GS_DEPTH_WRITE]=1;
    st.p[GS_COLOR_FUNC]=1;st.p[GS_ALPHA_SOURCE]=1;st.p[GS_TEX_FUNC]=1;
    st.p[GS_SRC_RGB]=4;st.p[GS_SRC_ALPHA]=4;
    pod_glide_vertex tri[3]={{.x=1,.y=1,.ooz=10000,.r=255,.a=255},
        {.x=31,.y=1,.ooz=10000,.r=255,.a=255},{.x=1,.y=15,.ooz=10000,.r=255,.a=255}};
    failed|=pod_glide_gpu_clear(gpu,0xff000000)!=0;
    failed|=pod_glide_gpu_draw(gpu,&st,tri,3,0)!=0;
    for(unsigned i=0;i<3;i++){tri[i].ooz=50000;tri[i].r=0;tri[i].b=255;}
    failed|=pod_glide_gpu_draw(gpu,&st,tri,3,0)!=0;
    failed|=pod_glide_gpu_read(gpu,image,sizeof image)!=0;
    size_t center=(4*32+4)*4;
    if(image[center]!=0||image[center+2]!=255){fprintf(stderr,"FAIL depth-order triangle: %u,%u\n",image[center],image[center+2]);failed=1;}
    failed|=pod_glide_gpu_swap(gpu)!=0;
    failed|=pod_glide_gpu_clear(gpu,0xff0000ff)!=0;
    failed|=pod_glide_gpu_read_buffer(gpu,0,image,sizeof image)!=0;
    failed|=image[center+2]!=255;
    failed|=pod_glide_gpu_read_buffer(gpu,1,image,sizeof image)!=0;
    failed|=image[center]!=255||image[center+2]!=0;
    uint32_t texels[4]={0xffff0000,0xff00ff00,0xff0000ff,0xffffffff};
    failed|=pod_glide_gpu_texture(gpu,0,2,2,1,texels,sizeof texels)!=0;
    st.p[GS_DEPTH_MODE]=0;st.p[GS_TEXTURE]=1;st.p[GS_COLOR_FUNC]=3;
    st.p[GS_COLOR_FACTOR]=8;st.p[GS_COLOR_OTHER]=1;st.p[GS_ALPHA_SOURCE]=2;
    st.f[4]=st.f[5]=1;
    pod_glide_vertex quad[6]={{.x=0,.y=0,.q=1},{.x=32,.y=0,.sow=1,.q=1},
        {.x=0,.y=16,.tow=1,.q=1},{.x=32,.y=0,.sow=1,.q=1},
        {.x=32,.y=16,.sow=1,.tow=1,.q=1},{.x=0,.y=16,.tow=1,.q=1}};
    failed|=pod_glide_gpu_clear(gpu,0xff000000)!=0;
    failed|=pod_glide_gpu_draw(gpu,&st,quad,6,0)!=0;
    failed|=pod_glide_gpu_read(gpu,image,sizeof image)!=0;
    const unsigned probes[4]={4*32+8,4*32+24,12*32+8,12*32+24};
    for(unsigned i=0;i<4;i++){uint32_t pixel;memcpy(&pixel,image+probes[i]*4,4);if(pixel!=texels[i]){fprintf(stderr,"FAIL texture quadrant %u: %08X\n",i,pixel);failed=1;}}
    /* Rectangular Glide coordinates are measured relative to a 256-texel
     * longest edge. Exercise the service's aspect scale through the shader. */
    pod_glide_texture_scales(0,&st.f[4],&st.f[5]);
    for(unsigned i=0;i<6;i++){quad[i].sow*=256;quad[i].tow*=32;}
    failed|=pod_glide_gpu_draw(gpu,&st,quad,6,0)!=0;
    failed|=pod_glide_gpu_read(gpu,image,sizeof image)!=0;
    for(unsigned i=0;i<4;i++){uint32_t pixel;memcpy(&pixel,image+probes[i]*4,4);failed|=pixel!=texels[i];}
    failed|=pod_glide_gpu_texture(gpu,0,1,1,2,texels,sizeof texels)!=-1;
    failed|=pod_glide_gpu_clear_depth(gpu,0,NAN)!=-1;
    failed|=pod_glide_gpu_draw(gpu,&st,quad,4,0)!=-1;
    /* Shared reciprocal-W affects texture coordinates after interpolation. */
    st.f[4]=st.f[5]=1;
    for(unsigned i=0;i<6;i++) {
        quad[i].q=quad[i].x?4:1;
        quad[i].sow=quad[i].x?4:0;
        quad[i].tow=quad[i].y?quad[i].q:0;
    }
    failed|=pod_glide_gpu_draw(gpu,&st,quad,6,0)!=0;
    failed|=pod_glide_gpu_read(gpu,image,sizeof image)!=0;
    uint32_t projected;memcpy(&projected,image+(4*32+8)*4,4);
    failed|=projected!=0xff00ff00;
    /* Keyed red texels preserve the cleared destination. */
    st.p[GS_KEY_MODE]=1;st.p[GS_KEY_COLOR]=0xff0000;
    failed|=pod_glide_gpu_clear(gpu,0xff000000)!=0;
    failed|=pod_glide_gpu_draw(gpu,&st,quad,6,0)!=0;
    failed|=pod_glide_gpu_read(gpu,image,sizeof image)!=0;
    uint32_t keyed;memcpy(&keyed,image+(2*32+2)*4,4);
    failed|=keyed!=0xff000000;
    st.p[GS_KEY_MODE]=0;st.p[GS_TEXTURE]=0;st.p[GS_COLOR_FUNC]=1;
    st.p[GS_COLOR_LOCAL]=1;st.p[GS_ALPHA_SOURCE]=0;
    st.f[0]=1;st.f[1]=st.f[2]=0;st.f[3]=0.5f;
    st.p[GS_SRC_RGB]=1;st.p[GS_DST_RGB]=5;
    failed|=pod_glide_gpu_clear(gpu,0xff0000ff)!=0;
    failed|=pod_glide_gpu_draw(gpu,&st,tri,3,0)!=0;
    failed|=pod_glide_gpu_read(gpu,image,sizeof image)!=0;
    failed|=image[center]<126||image[center]>129||image[center+2]<126||image[center+2]>129;
    /* Larger reciprocal W represents the nearer surface. */
    st.p[GS_SRC_RGB]=4;st.p[GS_DST_RGB]=0;st.p[GS_COLOR_LOCAL]=0;
    st.p[GS_ALPHA_SOURCE]=1;st.p[GS_DEPTH_MODE]=2;
    failed|=pod_glide_gpu_clear(gpu,0xff000000)!=0;
    for(unsigned i=0;i<3;i++){tri[i].oow=4;tri[i].r=255;tri[i].b=0;}
    failed|=pod_glide_gpu_draw(gpu,&st,tri,3,0)!=0;
    for(unsigned i=0;i<3;i++){tri[i].oow=0.25f;tri[i].r=0;tri[i].b=255;}
    failed|=pod_glide_gpu_draw(gpu,&st,tri,3,0)!=0;
    failed|=pod_glide_gpu_read(gpu,image,sizeof image)!=0;
    failed|=image[center]!=0||image[center+2]!=255;
    /* Known exponent-band boundaries and a mantissa midpoint. Two clears
     * bracket each expected integer code, checking the fragment depth through
     * actual GPU comparisons rather than duplicating the shader formula. */
    const float reciprocal[4]={0.75f,0.5f,0.25f,1.0f/256};
    const unsigned codes[4]={2048,4096,8192,32768};
    for(unsigned test=0;test<4;test++) {
        for(unsigned i=0;i<3;i++){tri[i].oow=reciprocal[test];tri[i].r=255;tri[i].b=0;}
        for(unsigned side=0;side<2;side++) {
            float limit=(codes[test]+(side?0.5f:-0.5f))/65535.0f;
            failed|=pod_glide_gpu_clear_depth(gpu,0xff000000,limit)!=0;
            failed|=pod_glide_gpu_draw(gpu,&st,tri,3,0)!=0;
            failed|=pod_glide_gpu_read(gpu,image,sizeof image)!=0;
            if(image[center+2]!=(side?255:0)){
                fprintf(stderr,"FAIL W code %u side %u\n",codes[test],side);failed=1;
            }
        }
    }
    /* These nearby reciprocal values occupy one 12-bit mantissa bucket.
     * With LESS comparison, the second blue primitive must not replace red. */
    failed|=pod_glide_gpu_clear(gpu,0xff000000)!=0;
    for(unsigned i=0;i<3;i++){tri[i].oow=0.75f;tri[i].r=255;tri[i].b=0;}
    failed|=pod_glide_gpu_draw(gpu,&st,tri,3,0)!=0;
    for(unsigned i=0;i<3;i++){tri[i].oow=0.75001f;tri[i].r=0;tri[i].b=255;}
    failed|=pod_glide_gpu_draw(gpu,&st,tri,3,0)!=0;
    failed|=pod_glide_gpu_read(gpu,image,sizeof image)!=0;
    failed|=image[center]!=0||image[center+2]!=255;
    st.p[GS_TEXTURE]=128;
    failed|=pod_glide_gpu_draw(gpu,&st,tri,3,0)!=-1;
    pod_glide_gpu_close(gpu);
    fprintf(stderr,"Metal buffers/depth/textured triangles: %s\n",failed?"FAIL":"PASS");
    return failed;
}
