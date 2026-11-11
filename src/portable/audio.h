#pragma once
#include "win32hle.h"
extern const win32hle_shim pod_audio_shims[];
void pod_audio_init(const char *capture_path);
void pod_audio_pump(void);
void pod_audio_shutdown(void);
