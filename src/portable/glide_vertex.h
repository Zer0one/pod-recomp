#pragma once
#include "glide_gpu.h"
/* Decode the Glide 2 default (shared FBI/TMU0 W) vertex layout. Returns
 * zero, or the one-based index of a nonfinite attribute actually consumed. */
unsigned pod_glide_vertex_decode(const pod_glide_state *state,
                                const float guest[12],pod_glide_vertex *vertex);
int pod_glide_uses_texture(const pod_glide_state *state);
