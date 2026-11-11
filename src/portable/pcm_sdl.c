/* Standalone output adapter. A frontend may link another implementation of
 * pcm_output.h; waveOut/MCI policy never owns SDL devices. */
#include <SDL2/SDL.h>
#include <stdlib.h>
#include "pcm_output.h"
struct pod_pcm_output { SDL_AudioDeviceID device; };
pod_pcm_output *pod_pcm_open(pod_pcm_format format) {
    if ((format.bits!=8 && format.bits!=16) || format.channels<1 ||
        format.channels>2 || format.rate<4000 || format.rate>192000) {
        SDL_SetError("unsupported PCM format"); return NULL;
    }
    if (SDL_InitSubSystem(SDL_INIT_AUDIO)) return NULL;
    pod_pcm_output *output=calloc(1,sizeof *output);
    if (!output) { SDL_OutOfMemory(); SDL_QuitSubSystem(SDL_INIT_AUDIO); return NULL; }
    SDL_AudioSpec want={0};
    want.freq=(int)format.rate; want.channels=(Uint8)format.channels;
    want.format=format.bits==8?AUDIO_U8:AUDIO_S16LSB; want.samples=512;
    output->device=SDL_OpenAudioDevice(NULL,0,&want,NULL,0);
    if (!output->device) { free(output); SDL_QuitSubSystem(SDL_INIT_AUDIO); return NULL; }
    return output;
}
int pod_pcm_write(pod_pcm_output *output,const void *bytes,uint32_t size) {
    return output?SDL_QueueAudio(output->device,bytes,size):-1;
}
uint32_t pod_pcm_queued(pod_pcm_output *output) { return output?SDL_GetQueuedAudioSize(output->device):0; }
void pod_pcm_pause(pod_pcm_output *output,int paused) { if (output) SDL_PauseAudioDevice(output->device,paused); }
void pod_pcm_clear(pod_pcm_output *output) { if (output) SDL_ClearQueuedAudio(output->device); }
void pod_pcm_close(pod_pcm_output *output) { if (output) { SDL_CloseAudioDevice(output->device); free(output); SDL_QuitSubSystem(SDL_INIT_AUDIO); } }
const char *pod_pcm_driver(void) { const char *driver=SDL_GetCurrentAudioDriver(); return driver?driver:"none"; }
const char *pod_pcm_error(void) { return SDL_GetError(); }
