#include "glide_texture.h"
unsigned pod_glide_texture_bpp(unsigned fmt) {
    if(fmt==1||fmt==9||fmt>14||fmt==6||fmt==7)return 0;
    return fmt>=8?2:1;
}
int pod_glide_texture_extent(unsigned lod,unsigned aspect,unsigned *w,unsigned *h) {
    if(lod>8||aspect>6||!w||!h)return -1;
    *w=*h=256u>>lod;
    if(aspect<3)*h>>=3-aspect;else *w>>=aspect-3;
    if(!*w)*w=1;
    if(!*h)*h=1;
    return 0;
}
int pod_glide_texture_bytes(unsigned small,unsigned large,unsigned aspect,unsigned fmt,unsigned *bytes) {
    unsigned bpp=pod_glide_texture_bpp(fmt),total=0;
    if(!bytes||!bpp||small<large||small>8||aspect>6)return -1;
    for(unsigned l=large;l<=small;l++) {
        unsigned w,h;
        pod_glide_texture_extent(l,aspect,&w,&h);
        total+=(w*h*bpp+7)&~7u;
    }
    *bytes=total;
    return 0;
}
int pod_glide_texture_scales(unsigned aspect,float *s,float *t) {
    unsigned w,h;
    if(!s||!t||pod_glide_texture_extent(0,aspect,&w,&h))return -1;
    /* Coordinate range is that of a 256-texel longest edge, independent
     * of the selected base LOD; the shorter axis follows the aspect ratio. */
    *s=1.0f/w;*t=1.0f/h;
    return 0;
}
uint32_t pod_glide_texture_texel(unsigned value,unsigned fmt,const uint32_t palette[256]) {
    unsigned r=0,g=0,b=0,a=255;
    switch(fmt) {
    case 0: case 8:
        r=((value>>5)&7)*255/7;g=((value>>2)&7)*255/7;b=(value&3)*85;
        if(fmt==8)a=value>>8;
        break;
    case 2: r=g=b=255;a=value;break;
    case 3: r=g=b=value;break;
    case 4: r=g=b=(value&15)*17;a=(value>>4)*17;break;
    case 5: case 14:
        return (palette[value&255]&0xffffff)|((fmt==14?value>>8:255)<<24);
    case 10:
        r=(value>>11)*255/31;g=((value>>5)&63)*255/63;b=(value&31)*255/31;
        break;
    case 11:
        r=((value>>10)&31)*255/31;g=((value>>5)&31)*255/31;b=(value&31)*255/31;
        a=value&32768?255:0;break;
    case 12:
        r=((value>>8)&15)*17;g=((value>>4)&15)*17;b=(value&15)*17;a=(value>>12)*17;
        break;
    case 13: r=g=b=value&255;a=value>>8;break;
    default:return 0;
    }
    return a<<24|r<<16|g<<8|b;
}
