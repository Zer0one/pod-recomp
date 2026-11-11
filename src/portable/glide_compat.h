#pragma once
/* PODX3DFX.EXE lifecycle policy, separate from the reusable Glide ABI/backend.
 * Register before host initialization; bind original delegates afterwards. */
void pod_glide_compat_register(void);
void pod_glide_compat_bind(void);
