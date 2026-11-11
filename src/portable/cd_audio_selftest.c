/* Entirely synthetic CUE/PCM: no user disc, catalog or lifted functions. */
#include <SDL2/SDL.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "host.h"
#include "cd_audio.h"
static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr,"FAIL %d: %s\n",__LINE__,#c); failures++; } } while(0)
static void proc(void) { RET(A32(1)==0x81,4); }
const recomp_dispatch_entry_t recomp_dispatch_table[]={{0x90000000,proc}};
const uint32_t recomp_dispatch_count=1;
static uint32_t params,device;
static uint32_t mci(uint32_t msg,uint32_t flags) { uint32_t a[4]={device,msg,flags,params}; return hle_call_guest(hle_resolve("mciSendCommandA"),4,a); }
static uint32_t status(uint32_t item,uint32_t track,uint32_t flags) {
    MEM32(params+8)=item; MEM32(params+12)=track;
    CHECK(mci(0x814,0x100|flags)==0); return MEM32(params+4);
}
static uint32_t notification(uint32_t hwnd) {
    uint32_t *msg=hle_alloc(28),a[5]={hle_va(msg),hwnd,0x3b9,0x3b9,1};
    uint32_t result=hle_call_guest(hle_resolve("PeekMessageA"),5,a)?msg[2]:0;
    hle_free(msg); return result;
}
static void fixture(const char *dir,int big) {
    char p[1024]; uint8_t raw[2352*8]; memset(raw,0x7f,2352*2);
    for (size_t i=2352*2;i<sizeof raw;i+=4) {
        int16_t l=1234,r=-2345; memcpy(raw+i,&l,2); memcpy(raw+i+2,&r,2);
    }
    if (big) for (size_t i=0;i<sizeof raw;i+=2) { uint8_t v=raw[i]; raw[i]=raw[i+1]; raw[i+1]=v; }
    snprintf(p,sizeof p,"%s/track 1.bin",dir); FILE *f=fopen(p,"wb"); CHECK(f!=NULL);
    if (f) { CHECK(fwrite(raw,1,2352*4,f)==2352*4); fclose(f); }
    snprintf(p,sizeof p,"%s/track 2.bin",dir); f=fopen(p,"wb"); CHECK(f!=NULL);
    if (f) { CHECK(fwrite(raw,1,sizeof raw,f)==sizeof raw); fclose(f); }
    snprintf(p,sizeof p,"%s/test.cue",dir); f=fopen(p,"w"); CHECK(f!=NULL);
    if (f) { fprintf(f,"FILE \"track 1.bin\" BINARY\r\n TRACK 01 MODE1/2352\r\n INDEX 01 00:00:00\r\nFILE \"track 2.bin\" %s\r\n TRACK 02 AUDIO\r\n INDEX 00 00:00:00\r\n INDEX 01 00:00:02\r\n",big?"MOTOROLA":"BINARY"); fclose(f); }
}
int main(void) {
    win32hle_register(pod_cd_shims); recomp_host_init();
    char dir[]="/tmp/pod-cd-test-XXXXXX"; CHECK(mkdtemp(dir)!=NULL);
    char cue[1024],capture[1024]; snprintf(cue,sizeof cue,"%s/test.cue",dir); snprintf(capture,sizeof capture,"%s/capture.wav",dir);
    uint32_t cls=hle_guest_strdup("CD synthetic"),*wc=hle_alloc(40);
    wc[1]=0x90000000; wc[9]=cls; uint32_t va=hle_va(wc);
    CHECK(hle_call_guest(hle_resolve("RegisterClassA"),1,&va)!=0);
    uint32_t wa[12]={0,cls,cls,0x10000000,0,0,320,200,0,0,0,0};
    uint32_t hwnd=hle_call_guest(hle_resolve("CreateWindowExA"),12,wa); CHECK(hwnd!=0);
    params=hle_va(hle_alloc(64));
    for (int big=0;big<2;big++) {
        fixture(dir,big); CHECK(pod_cd_init(cue,capture)==0); CHECK(pod_cd_available());
        device=0; MEM32(params+8)=516; CHECK(mci(0x803,0x3100)==0); device=MEM32(params+4); CHECK(device==0xcd);
        MEM32(params+4)=10; CHECK(mci(0x80d,0x400)==0);
        CHECK(status(1,2,0x10)==(6u<<16)); /* stored pregap is excluded */
        CHECK(status(2,2,0x10)==2); CHECK(status(3,0,0)==2);
        MEM32(params)=hwnd; MEM32(params+4)=2; MEM32(params+8)=2u|(3u<<24);
        CHECK(mci(0x806,13)==0); CHECK(mci(0x809,0)==0);
        CHECK(status(4,0,0)==529); uint32_t paused=status(2,0,0); SDL_Delay(30);
        CHECK(status(2,0,0)==paused); CHECK(mci(0x855,0)==0);
        uint32_t start=SDL_GetTicks();
        while (status(4,0,0)==526 && SDL_GetTicks()-start<1500) { SDL_Delay(5); pod_cd_pump(); }
        CHECK(status(4,0,0)==525); CHECK(notification(hwnd)==1);
        MEM32(params)=hwnd; MEM32(params+4)=2; MEM32(params+8)=2u|(6u<<24);
        CHECK(mci(0x806,13)==0); CHECK(mci(0x808,0)==0); CHECK(notification(hwnd)==4);
        CHECK(mci(0x804,0)==0); pod_cd_shutdown();
        FILE *f=fopen(capture,"rb"); CHECK(f!=NULL);
        if (f) { uint8_t header[44]; int16_t samples[2]={0}; CHECK(fread(header,1,44,f)==44); CHECK(!memcmp(header,"RIFF",4)); CHECK(fread(samples,1,4,f)==4); CHECK(samples[0]==1234 && samples[1]==-2345); fclose(f); }
    }
    hle_call_guest(hle_resolve("DestroyWindow"),1,&hwnd);
    hle_free(hle_ptr(params)); hle_free(wc); hle_free(hle_ptr(cls)); SDL_Quit();
    const char *names[]={"track 1.bin","track 2.bin","test.cue","capture.wav"};
    for (unsigned i=0;i<4;i++) { char path[1024]; snprintf(path,sizeof path,"%s/%s",dir,names[i]); unlink(path); }
    rmdir(dir); printf("CD selftest: %d failures\n",failures); return failures!=0;
}
