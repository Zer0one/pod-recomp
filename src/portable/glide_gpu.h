#pragma once
#include <stddef.h>
#include <stdint.h>
/* Native values only: no guest pointers, windows, HLE handles or Metal types. */
typedef struct pod_glide_gpu pod_glide_gpu;
#include "glide_state_layout.h"
typedef struct { uint32_t p[GS_PARAMETER_COUNT]; float f[GS_FLOAT_COUNT]; } pod_glide_state;
/* Screen coordinates; iterated colors; projective TMU coordinates. All
 * attributes interpolate in screen space before division in the fragment. */
typedef struct { float x,y,ooz,oow,r,g,b,a,sow,tow,q,pad; } pod_glide_vertex;
/* One caller serializes operations on a context. No frontend/window ownership.
 * Successful draw/upload calls copy caller data; it can be reused immediately.
 * Read/write/swap synchronize pending draws. Backend errors return -1; close
 * remains valid after an error. NULL open reports a bounded diagnostic string.
 * Buffer 0 is front, 1 is back; clear/draw target back. Pixels are packed BGRA8.
 * Texture slots are zero-based on upload, one-based in GS_TEXTURE (zero means
 * no texture). Mip levels are tightly concatenated BGRA from largest to smallest.
 * f[0..3] is constant RGBA, f[4..5] is the normalized texture-coordinate scale;
 * f[8..71] holds the decoded fog table (table fog is not implemented yet).
 */
pod_glide_gpu *pod_glide_gpu_open(unsigned width,unsigned height,char *error,size_t capacity);
int pod_glide_gpu_clear(pod_glide_gpu *gpu,uint32_t argb);
int pod_glide_gpu_clear_depth(pod_glide_gpu *gpu,uint32_t argb,float depth);
int pod_glide_gpu_read(pod_glide_gpu *gpu,void *bgra,size_t capacity);
int pod_glide_gpu_read_buffer(pod_glide_gpu *gpu,unsigned buffer,void *bgra,size_t capacity);
int pod_glide_gpu_write_buffer(pod_glide_gpu *gpu,unsigned buffer,const void *bgra,size_t capacity);
int pod_glide_gpu_swap(pod_glide_gpu *gpu);
int pod_glide_gpu_texture(pod_glide_gpu *gpu,unsigned slot,unsigned width,unsigned height,
                         unsigned levels,const uint32_t *bgra,size_t bytes);
int pod_glide_gpu_draw(pod_glide_gpu *gpu,const pod_glide_state *state,
                      const pod_glide_vertex *vertices,unsigned count,int lines);
void pod_glide_gpu_close(pod_glide_gpu *gpu);
