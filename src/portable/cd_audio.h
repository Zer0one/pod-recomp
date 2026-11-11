#pragma once
#include "win32hle.h"
extern const win32hle_shim pod_cd_shims[];
int pod_cd_init(const char *cue_path, const char *capture_path);
int pod_cd_available(void);
void pod_cd_pump(void);
void pod_cd_shutdown(void);
