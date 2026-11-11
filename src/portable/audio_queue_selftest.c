/* Synthetic waveOut integration against a manually consumed PCM queue.
 * No output device or wall clock: guest completion follows consumption only. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "host.h"
#include "audio.h"
#include "pcm_output.h"
struct pod_pcm_output { unsigned paused,size; unsigned char bytes[4096]; };
static pod_pcm_output *active;
static int fail_open,fail_write;
pod_pcm_output *pod_pcm_open(pod_pcm_format f) {
    if (fail_open || f.rate!=44100 || f.channels!=2 || f.bits!=16) return NULL;
    active=calloc(1,sizeof *active); if (active) active->paused=1; return active;
}
int pod_pcm_write(pod_pcm_output *o,const void *data,uint32_t n) {
    if (fail_write || n>sizeof o->bytes-o->size) return -1;
    memcpy(o->bytes+o->size,data,n); o->size+=n; return 0;
}
uint32_t pod_pcm_queued(pod_pcm_output *o) { return o?o->size:0; }
void pod_pcm_pause(pod_pcm_output *o,int paused) { o->paused=paused; }
void pod_pcm_clear(pod_pcm_output *o) { o->size=0; }
void pod_pcm_close(pod_pcm_output *o) { free(o); active=NULL; }
const char *pod_pcm_driver(void) { return "manual-test"; }
const char *pod_pcm_error(void) { return "injected output failure"; }
static void consume(unsigned bytes) {
    if (active->paused) return;
    if (bytes>active->size) bytes=active->size;
    memmove(active->bytes,active->bytes+bytes,active->size-bytes); active->size-=bytes;
}
static int opened,done,closed,failures;
static uint32_t completed[4];
static void callback(void) {
    if (A32(1)==0x3bb) opened++;
    if (A32(1)==0x3bd) { if (done<4) completed[done]=A32(3); done++; }
    if (A32(1)==0x3bc) closed++;
    RET(0,5);
}
const recomp_dispatch_entry_t recomp_dispatch_table[]={{0x90000000,callback}};
const uint32_t recomp_dispatch_count=1;
#define CHECK(c) do { if (!(c)) { fprintf(stderr,"FAIL %d: %s\n",__LINE__,#c); failures++; } } while(0)
static uint32_t invoke(const char *name,int n,uint32_t *a) { return hle_call_guest(hle_resolve(name),n,a); }
int main(void) {
    win32hle_register(pod_audio_shims); recomp_host_init(); pod_audio_init(NULL);
    uint8_t *storage=hle_alloc(1024); uint32_t out=hle_va(storage),fmt=out+16,pos=out+48;
    uint32_t first=out+64,second=out+96,data=out+160;
    MEM16(fmt)=1; MEM16(fmt+2)=2; MEM32(fmt+4)=44100;
    MEM32(fmt+8)=176400; MEM16(fmt+12)=4; MEM16(fmt+14)=16;
    uint32_t a[6]={out,0xffffffffu,fmt,0x90000000,0,0x30000};
    fail_open=1; CHECK(invoke("waveOutOpen",6,a)==6); CHECK(MEM32(out)==0 && opened==0);
    uint32_t text=out+512,e[3]={6,text,64};
    CHECK(invoke("waveOutGetErrorTextA",3,e)==0);
    CHECK(strstr(hle_ptr(text),"unavailable")!=NULL);
    memset(hle_ptr(text),0xa5,16); e[2]=5;
    CHECK(invoke("waveOutGetErrorTextA",3,e)==0); CHECK(MEM8(text+4)==0 && MEM8(text+5)==0xa5);
    e[2]=1; CHECK(invoke("waveOutGetErrorTextA",3,e)==0); CHECK(MEM8(text)==0 && MEM8(text+1)=='u');
    e[0]=0xffff; e[1]=0; e[2]=0; CHECK(invoke("waveOutGetErrorTextA",3,e)==0);
    e[1]=text; e[2]=16; CHECK(invoke("waveOutGetErrorTextA",3,e)==9); CHECK(MEM8(text)==0);
    e[0]=6; e[1]=0; CHECK(invoke("waveOutGetErrorTextA",3,e)==11);
    fail_open=0; CHECK(invoke("waveOutOpen",6,a)==0); CHECK(opened==1);
    uint32_t wave=MEM32(out); CHECK(wave!=0 && active && !active->paused);
    a[0]=wave; CHECK(invoke("waveOutPause",1,a)==0);
    for (unsigned i=0;i<32;i++) MEM8(data+i)=(uint8_t)(i+1);
    for (unsigned i=0;i<2;i++) {
        uint32_t h=i?second:first; MEM32(h)=data; MEM32(h+4)=32;
        a[1]=h; a[2]=32; CHECK(invoke("waveOutPrepareHeader",3,a)==0);
    }
    a[1]=first; CHECK(invoke("waveOutWrite",3,a)==0);
    memset(hle_ptr(data),0,32); CHECK(active->bytes[0]==1 && active->bytes[31]==32);
    consume(32); pod_audio_pump(); CHECK(done==0 && pod_pcm_queued(active)==32);
    MEM32(pos)=2; a[1]=pos; a[2]=12; CHECK(invoke("waveOutGetPosition",3,a)==0); CHECK(MEM32(pos+4)==0);
    CHECK(invoke("waveOutRestart",1,a)==0); consume(16); pod_audio_pump(); CHECK(done==0);
    CHECK(invoke("waveOutGetPosition",3,a)==0); CHECK(MEM32(pos+4)==4);
    a[1]=second; a[2]=32; fail_write=1;
    CHECK(invoke("waveOutWrite",3,a)==7); CHECK(!(MEM32(second+16)&16));
    fail_write=0; CHECK(invoke("waveOutWrite",3,a)==0);
    consume(16); CHECK(done==0); pod_audio_pump(); CHECK(done==1 && completed[0]==first);
    CHECK((MEM32(first+16)&17)==1 && (MEM32(second+16)&16));
    a[1]=pos; a[2]=12; MEM32(pos)=2;
    CHECK(invoke("waveOutGetPosition",3,a)==0); CHECK(MEM32(pos+4)==8);
    CHECK(invoke("waveOutClose",1,a)==33); CHECK(closed==0);
    CHECK(invoke("waveOutReset",1,a)==0); CHECK(done==2 && completed[1]==second && active->size==0);
    CHECK(invoke("waveOutGetPosition",3,a)==0); CHECK(MEM32(pos+4)==0);
    pod_audio_pump(); CHECK(done==2);
    CHECK(invoke("waveOutClose",1,a)==0); CHECK(closed==1 && !active);
    pod_audio_shutdown(); hle_free(storage);
    printf("Manual PCM waveOut: %d failures\n",failures); return failures!=0;
}
