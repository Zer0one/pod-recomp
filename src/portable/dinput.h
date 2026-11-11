#pragma once
#include "win32hle.h"
extern const win32hle_shim pod_dinput_shims[];
/* PC/AT set-1 plus DIK extended bit; no frontend-specific key enum.
 * Invoke with the guest-machine lock held, including release on focus loss. */
void pod_keyboard_dik(unsigned key, int down);
void pod_keyboard_release(void);
