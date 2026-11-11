#pragma once
#include <stdint.h>
/* Glide 2 format/layout policy, independent of guest memory and GPU APIs. */
unsigned pod_glide_texture_bpp(unsigned format); /* zero for unsupported formats */
int pod_glide_texture_extent(unsigned lod,unsigned aspect,unsigned *w,unsigned *h);
int pod_glide_texture_bytes(unsigned small,unsigned large,unsigned aspect,
                           unsigned format,unsigned *bytes);
int pod_glide_texture_scales(unsigned aspect,float *s,float *t);
uint32_t pod_glide_texture_texel(unsigned value,unsigned format,const uint32_t palette[256]);
