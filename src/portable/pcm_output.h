#pragma once
#include <stdint.h>
/* Frontend-owned PCM queues. No guest memory, callbacks, SDL types or clock.
 * Open paused; accept exact unsigned 8-bit / signed little-endian 16-bit PCM.
 * write() copies bytes before returning, or fails without consuming them.
 * queued() counts unconsumed bytes, including while paused; clear() discards
 * them. Consuming samples must never invoke guest code: service pump does it.
 * Calls are serialized by the owning frontend/guest-machine lock. Each open
 * is independent (effects and CD can run at different rates). */
typedef struct pod_pcm_output pod_pcm_output;
typedef struct { unsigned rate,channels,bits; } pod_pcm_format;
pod_pcm_output *pod_pcm_open(pod_pcm_format format);
int pod_pcm_write(pod_pcm_output *output,const void *bytes,uint32_t size);
uint32_t pod_pcm_queued(pod_pcm_output *output);
void pod_pcm_pause(pod_pcm_output *output,int paused);
void pod_pcm_clear(pod_pcm_output *output);
void pod_pcm_close(pod_pcm_output *output);
const char *pod_pcm_driver(void);
const char *pod_pcm_error(void);
