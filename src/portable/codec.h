#pragma once
#include "win32hle.h"
int pod_codec_map(const char *path);
int pod_codec_probe(void);
int pod_codec_decode(const char *input, const char *output, unsigned first_frame);
extern const win32hle_shim pod_video_shims[];
void pod_video_pace(uint32_t dib);
