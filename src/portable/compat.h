#pragma once
#include "win32hle.h"
/* Call once per process; HLE currently has global machine state. */
/* desktop_bits=0 retains HLE defaults; the Glide build requires a 16-bit
 * virtual desktop before it opens its Voodoo display. */
void pod_compat_register(int videos, unsigned desktop_bits);
void pod_compat_bind(void);
