#pragma once
#include "win32hle.h"
/* Call once per process; HLE currently has global machine state. */
void pod_compat_register(int videos);
void pod_compat_bind(void);
