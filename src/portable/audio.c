/* POD's existing mixer supplies PCM; the frontend queue owns its own copy.
 * All guest state and callbacks are touched under the HLE machine lock, never
 * on an output/consumer thread. No game-derived code or data lives here. */
#include "pcm_output.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "audio.h"

enum { DONE=1, PREPARED=2, INQUEUE=16, MAX_HEADERS=256 };
static pod_pcm_output *device;
static pod_pcm_format spec;
static uint32_t handle, callback, instance, mode, volume=0xffffffffu;
static uint64_t submitted, total_submitted;
static struct { uint32_t header; uint64_t end; } pending[MAX_HEADERS];
static unsigned pending_count;
static int resetting;
static const char *capture_path;
static FILE *capture_file;
static uint32_t capture_bytes, capture_rate;
static uint16_t capture_channels, capture_bits;
static uint64_t non_silent;

static void le16(FILE *f,unsigned n) { fputc(n&255,f); fputc((n>>8)&255,f); }
static void le32(FILE *f,uint32_t n) { le16(f,n&65535); le16(f,n>>16); }
static void wave_header(void) {
    rewind(capture_file);
    fwrite("RIFF",1,4,capture_file); le32(capture_file,36+capture_bytes);
    fwrite("WAVEfmt ",1,8,capture_file); le32(capture_file,16);
    le16(capture_file,1); le16(capture_file,capture_channels);
    le32(capture_file,capture_rate);
    le32(capture_file,capture_rate*capture_channels*capture_bits/8);
    le16(capture_file,capture_channels*capture_bits/8); le16(capture_file,capture_bits);
    fwrite("data",1,4,capture_file); le32(capture_file,capture_bytes);
}
void pod_audio_init(const char *path) { capture_path=path; }
static void notify(uint32_t message,uint32_t header) {
    uint32_t cb=callback, h=handle, inst=instance;
    if (!cb) return;
    if (mode==0x30000) {
        uint32_t a[5]={h,message,inst,header,0};
        hle_call_guest(cb,5,a);
    } else if (mode==0x50000) hle_set_event(cb);
    else if (mode==0x10000) hle_post_message(cb,message,h,header);
    else if (mode==0x20000) {
        uint32_t a[4]={cb,message,h,header};
        hle_call_guest(hle_resolve("PostThreadMessageA"),4,a);
    }
}
static uint64_t played(void) {
    uint32_t queued=device?pod_pcm_queued(device):0;
    return submitted>=queued?submitted-queued:0;
}
static void retire(int all) {
    uint64_t position=played();
    while (pending_count && (all || pending[0].end<=position)) {
        uint32_t wh=pending[0].header;
        memmove(pending,pending+1,(--pending_count)*sizeof *pending);
        MEM32(wh+16)=(MEM32(wh+16)|DONE)&~INQUEUE;
        notify(0x3bd,wh); /* WOM_DONE */
    }
}
void pod_audio_pump(void) { if (device) retire(0); }
static int valid(uint32_t h) { return device && h==handle; }
static void open_wave(void) {
    uint32_t fmt=A32(2),flags=A32(5),ph=A32(0),cbmode=flags&0x70000;
    if (ph) MEM32(ph)=0;
    if (!fmt) RET(11,6);
    unsigned tag=MEM16(fmt),channels=MEM16(fmt+2),rate=MEM32(fmt+4),bits=MEM16(fmt+14);
    if (tag!=1 || (channels!=1 && channels!=2) || (bits!=8 && bits!=16) ||
        rate<4000 || rate>192000 || MEM16(fmt+12)!=channels*bits/8 ||
        MEM32(fmt+8)!=rate*channels*bits/8) RET(32,6); /* WAVERR_BADFORMAT */
    if (flags&1) RET(0,6); /* WAVE_FORMAT_QUERY does not open a device */
    if (!ph || (cbmode!=0 && cbmode!=0x10000 && cbmode!=0x20000 &&
        cbmode!=0x30000 && cbmode!=0x50000)) RET(11,6);
    if (device) RET(4,6); /* MMSYSERR_ALLOCATED: POD uses one output */
    spec=(pod_pcm_format){rate,channels,bits};
    device=pod_pcm_open(spec);
    if (!device) { fprintf(stderr,"[audio] open failed: %s\n",pod_pcm_error()); RET(6,6); }
    handle=hle_handle_alloc(HLE_H_OTHER,NULL);
    if (!handle) { pod_pcm_close(device); device=NULL; RET(7,6); }
    callback=A32(3); instance=A32(4); mode=cbmode; pending_count=0; submitted=0;
    volume=0xffffffffu;
    MEM32(ph)=handle;
    if (capture_path && !capture_file) {
        capture_file=fopen(capture_path,"wb");
        if (capture_file) {
            capture_rate=rate; capture_channels=channels; capture_bits=bits;
            wave_header();
        } else perror(capture_path);
    }
    fprintf(stderr,"[audio] PCM %u Hz %u-bit %u channels; driver=%s\n",
            rate,bits,channels,pod_pcm_driver());
    pod_pcm_pause(device,0);
    notify(0x3bb,0); RET(0,6); /* WOM_OPEN */
}
static void prepare(void) {
    if (!valid(A32(0))) RET(5,3);
    if (!A32(1) || A32(2)<32) RET(11,3);
    MEM32(A32(1)+16)|=PREPARED; RET(0,3);
}
static void unprepare(void) {
    if (!valid(A32(0))) RET(5,3);
    if (!A32(1) || A32(2)<32) RET(11,3);
    if (MEM32(A32(1)+16)&INQUEUE) RET(33,3);
    MEM32(A32(1)+16)&=~PREPARED; RET(0,3);
}
static void write_wave(void) {
    uint32_t wh=A32(1);
    if (!valid(A32(0))) RET(5,3);
    if (!wh || A32(2)<32) RET(11,3);
    uint32_t flags=MEM32(wh+16),n=MEM32(wh+4),p=MEM32(wh);
    if (!(flags&PREPARED)) RET(34,3);
    if ((flags&INQUEUE) || resetting) RET(33,3);
    if (!p || !n || n%(spec.channels*(spec.bits/8))) RET(11,3);
    /* Loop headers need separate ownership semantics; never silently ignore them. */
    if (flags&12) RET(8,3);
    retire(0);
    if (pending_count==MAX_HEADERS) RET(7,3);
    uint8_t *samples=malloc(n);
    if (!samples) RET(7,3);
    memcpy(samples,hle_ptr(p),n);
    unsigned bytes=spec.bits/8;
    for (uint32_t i=0;i<n/bytes;i++) {
        unsigned gain=(i%spec.channels)==1?volume>>16:volume&65535;
        int value;
        if (bytes==1) { value=(int)samples[i]-128; value=value*(int)gain/65535; samples[i]=(uint8_t)(value+128); }
        else { int16_t v; memcpy(&v,samples+i*2,2); value=(int)v*(int)gain/65535; v=(int16_t)value; memcpy(samples+i*2,&v,2); }
        if (value) non_silent++;
    }
    if (pod_pcm_write(device,samples,n)) { free(samples); RET(7,3); }
    if (capture_file && capture_rate==(unsigned)spec.rate && capture_channels==spec.channels &&
        capture_bits==bytes*8 && n<=64u*1024u*1024u-capture_bytes) {
        capture_bytes+=(uint32_t)fwrite(samples,1,n,capture_file);
    }
    free(samples);
    submitted+=n; total_submitted+=n;
    pending[pending_count].header=wh; pending[pending_count++].end=submitted;
    MEM32(wh+16)=(flags|INQUEUE)&~DONE;
    RET(0,3);
}
static void reset(void) {
    if (!valid(A32(0))) RET(5,1);
    resetting=1; pod_pcm_clear(device); retire(1); submitted=0; resetting=0;
    RET(0,1);
}
static void close_wave(void) {
    if (!valid(A32(0))) RET(5,1);
    retire(0); if (pending_count) RET(33,1);
    uint32_t closing=handle;
    pod_pcm_close(device); device=0;
    notify(0x3bc,0); hle_handle_close(closing); if (handle==closing) handle=0;
    RET(0,1);
}
static void pause_wave(void) { if (!valid(A32(0))) RET(5,1); pod_pcm_pause(device,1); RET(0,1); }
static void restart_wave(void) { if (!valid(A32(0))) RET(5,1); pod_pcm_pause(device,0); RET(0,1); }
static void position(void) {
    if (!valid(A32(0))) RET(5,3);
    uint32_t p=A32(1); if (!p || A32(2)<12) RET(11,3);
    uint64_t pos=played(); unsigned block=spec.channels*spec.bits/8;
    uint32_t type=MEM32(p),value;
    if (type==1) value=(uint32_t)(pos*1000/(block*spec.rate));
    else if (type==2) value=(uint32_t)(pos/block);
    else { type=4; value=(uint32_t)pos; }
    MEM32(p)=type; MEM32(p+4)=value; RET(0,3);
}
static void set_volume(void) { if (!valid(A32(0))) RET(5,2); volume=A32(1); RET(0,2); }
static void get_volume(void) { if (!valid(A32(0))) RET(5,2); if (!A32(1)) RET(11,2); MEM32(A32(1))=volume; RET(0,2); }
static void get_id(void) { if (!valid(A32(0))) RET(5,2); if (!A32(1)) RET(11,2); MEM32(A32(1))=0; RET(0,2); }
/* English host descriptions; no dependency on an open device or frontend.
 * Buffer/truncation contract:
 * https://learn.microsoft.com/windows/win32/api/mmeapi/nf-mmeapi-waveoutgeterrortext */
static void error_text(void) {
    uint32_t error=A32(0),buffer=A32(1),size=A32(2);
    if (!size) RET(0,3);
    if (!buffer) RET(11,3);
    MEM8(buffer)=0;
    const char *text=NULL;
    switch (error) {
    case 0:text="No audio error.";break;
    case 1:text="Audio output error.";break;
    case 4:text="Audio output is busy.";break;
    case 5:text="Invalid audio handle.";break;
    case 6:text="Audio output is unavailable.";break;
    case 7:text="Insufficient memory for audio.";break;
    case 8:text="Audio operation is unsupported.";break;
    case 11:text="Invalid audio parameter.";break;
    case 32:text="Unsupported PCM format.";break;
    case 33:text="Audio buffers are still playing.";break;
    case 34:text="Audio buffer is not prepared.";break;
    case 35:text="Synchronous audio mode is required.";break;
    default: if (error>21) RET(9,3);
    }
    if (text) snprintf(hle_ptr(buffer),size,"%s",text);
    else snprintf(hle_ptr(buffer),size,"Multimedia error %u.",error);
    RET(0,3);
}
static void caps(void) {
    if (!A32(1)) RET(11,3);
    uint8_t c[52]={0}; uint16_t channels=2; uint32_t formats=0xfff,support=9;
    memcpy(c+8,"POD PCM output",15); memcpy(c+40,&formats,4);
    memcpy(c+44,&channels,2); memcpy(c+48,&support,4);
    memcpy(APTR(1),c,A32(2)<sizeof c?A32(2):sizeof c); RET(0,3);
}
void pod_audio_shutdown(void) {
    if (device) { pod_pcm_close(device); device=0; }
    if (capture_file) { wave_header(); fclose(capture_file); capture_file=NULL; }
    fprintf(stderr,"[audio] submitted=%llu bytes; non-silent samples=%llu; capture=%u bytes\n",
            (unsigned long long)total_submitted,(unsigned long long)non_silent,capture_bytes);
}
const win32hle_shim pod_audio_shims[]={
    {"waveOutOpen",open_wave},{"waveOutWrite",write_wave},
    {"waveOutPrepareHeader",prepare},{"waveOutUnprepareHeader",unprepare},
    {"waveOutReset",reset},{"waveOutClose",close_wave},
    {"waveOutPause",pause_wave},{"waveOutRestart",restart_wave},
    {"waveOutGetPosition",position},{"waveOutSetVolume",set_volume},
    {"waveOutGetVolume",get_volume},{"waveOutGetID",get_id},
    {"waveOutGetDevCapsA",caps},{"waveOutGetErrorTextA",error_text},{NULL,NULL}
};
