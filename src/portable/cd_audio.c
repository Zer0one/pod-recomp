/* POD Gold's two-file data/audio CUE layout, served through MCI cdaudio.
 * CUE INDEX 01 is relative to its FILE, not to the concatenated disc.
 * Public MCI contract: https://learn.microsoft.com/windows/win32/multimedia/mci-status
 * CUE types/indexes: https://github.com/libyal/libodraw/blob/main/documentation/CUE%20sheet%20format.asciidoc
 * All file/sample data comes from the user's image; no disc data is embedded. */
#include "pcm_output.h"
#include <time.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <string.h>
#include <sys/stat.h>
#include "cd_audio.h"

enum { SECTOR=2352, SAMPLES=588, DEVICE=0xcd, STOP=525, PLAY=526, PAUSE=529 };
static FILE *source,*capture;
static pod_pcm_output *output;
static uint32_t data_frames,audio_index,audio_frames,format=2,mode=STOP,callback;
static uint64_t first_sample,end_sample,queued_samples,total_bytes;
static uint32_t capture_bytes;
static int ready,opened,big_endian;
static const char *capture_path;
static char music_file[PATH_MAX];
static void le16(FILE *f,unsigned n) { fputc(n&255,f); fputc((n>>8)&255,f); }
static void le32(FILE *f,uint32_t n) { le16(f,n&65535); le16(f,n>>16); }
static void capture_header(void) {
    rewind(capture); fwrite("RIFF",1,4,capture); le32(capture,36+capture_bytes);
    fwrite("WAVEfmt ",1,8,capture); le32(capture,16); le16(capture,1); le16(capture,2);
    le32(capture,44100); le32(capture,176400); le16(capture,4); le16(capture,16);
    fwrite("data",1,4,capture); le32(capture,capture_bytes);
}
static void notify(unsigned result) {
    uint32_t window=callback; callback=0;
    if (window) hle_post_message(window,0x3b9,result,DEVICE);
}
static uint64_t position(void) {
    uint64_t remaining=output?pod_pcm_queued(output)/4:0;
    return queued_samples>=remaining?queued_samples-remaining:first_sample;
}
static void halt(unsigned result) {
    first_sample=position(); queued_samples=first_sample;
    if (output) pod_pcm_clear(output);
    mode=STOP; notify(result);
}
int pod_cd_available(void) { return ready; }
int pod_cd_init(const char *cue_path,const char *capture_file) {
    if (source || output || capture || ready) return 1;
    opened=0; callback=0; capture_bytes=0; total_bytes=0;
    capture_path=capture_file;
    if (!cue_path) { fprintf(stderr,"[cd] no image: legacy silent CD emulation\n"); return 0; }
    char absolute[PATH_MAX],directory[PATH_MAX],line[PATH_MAX+128];
    if (!realpath(cue_path,absolute)) { perror(cue_path); return 1; }
    snprintf(directory,sizeof directory,"%s",absolute);
    char *slash=strrchr(directory,'/'); if (!slash) return 1; *slash=0;
    FILE *cue=fopen(absolute,"r"); if (!cue) return 1;
    char file[2][PATH_MAX]={{0}},type[32],kind[32]; uint32_t index[2]={0};
    unsigned files=0,tracks=0; int current=-1,valid=1,have_index[2]={0};
    while (fgets(line,sizeof line,cue)) {
        char name[PATH_MAX]; unsigned number,m,s,f;
        if (sscanf(line," FILE \"%1023[^\"]\" %31s",name,type)==2) {
            if (files>=2 || (strcmp(type,"BINARY") && strcmp(type,"MOTOROLA"))) { valid=0; break; }
            snprintf(file[files],sizeof file[files],"%s/%s",directory,name);
            if (files==1) big_endian=!strcmp(type,"MOTOROLA");
            files++;
        } else if (sscanf(line," TRACK %u %31s",&number,kind)==2) {
            if (tracks>=2 || number!=tracks+1 || files!=tracks+1 ||
                strcmp(kind,tracks?"AUDIO":"MODE1/2352")) { valid=0; break; }
            current=(int)tracks++;
        } else if (sscanf(line," INDEX %u %u:%u:%u",&number,&m,&s,&f)==4) {
            if (current<0 || s>=60 || f>=75 || m>99 || number>1) { valid=0; break; }
            if (number==1) { index[current]=(m*60+s)*75+f; have_index[current]=1; }
        } else {
            char word[32];
            if (sscanf(line," %31s",word)==1 && strcmp(word,"REM") && strcmp(word,"TITLE") && strcmp(word,"PERFORMER")) { valid=0; break; }
        }
    }
    fclose(cue);
    struct stat a,b;
    if (!valid || tracks!=2 || files!=2 || !have_index[0] || !have_index[1] || index[0] ||
        stat(file[0],&a) || stat(file[1],&b) || a.st_size<=0 || b.st_size<=0 ||
        a.st_size%SECTOR || b.st_size%SECTOR || a.st_size/SECTOR>UINT32_MAX || b.st_size/SECTOR>UINT32_MAX ||
        index[1]>=(uint64_t)b.st_size/SECTOR) {
        fprintf(stderr,"[cd] unsupported CUE: expected separate MODE1/2352 and AUDIO files with INDEX 01\n"); return 1;
    }
    data_frames=(uint32_t)(a.st_size/SECTOR); audio_index=index[1];
    audio_frames=(uint32_t)(b.st_size/SECTOR)-audio_index;
    snprintf(music_file,sizeof music_file,"%s",file[1]);
    ready=1; mode=STOP; format=2; first_sample=queued_samples=0;
    fprintf(stderr,"[cd] CUE ready: data=%u sectors; audio INDEX01=%u; length=%u sectors; %s-endian\n",
            data_frames,audio_index,audio_frames,big_endian?"big":"little");
    return 0;
}
static int ensure_output(void) {
    if (output) return 0;
    if (!source) source=fopen(music_file,"rb"); if (!source) return 1;
    output=pod_pcm_open((pod_pcm_format){44100,2,16});
    if (!output) { fprintf(stderr,"[cd] output failed: %s\n",pod_pcm_error()); return 1; }
    if (capture_path && !capture) { capture=fopen(capture_path,"wb"); if (capture) capture_header(); else perror(capture_path); }
    pod_pcm_pause(output,0);
    fprintf(stderr,"[cd] PCM 44100 Hz stereo; driver=%s\n",pod_pcm_driver());
    return 0;
}
void pod_cd_pump(void) {
    if (!ready || !output || mode!=PLAY) return;
    uint8_t bytes[SECTOR*8];
    while (queued_samples<end_sample && pod_pcm_queued(output)<44100) {
        uint64_t remaining=(end_sample-queued_samples)*4;
        size_t n=remaining<sizeof bytes?(size_t)remaining:sizeof bytes;
        if (fread(bytes,1,n,source)!=n) { fprintf(stderr,"[cd] source read failed\n"); halt(8); return; }
        if (big_endian) for (size_t i=0;i<n;i+=2) { uint8_t v=bytes[i]; bytes[i]=bytes[i+1]; bytes[i+1]=v; }
        if (pod_pcm_write(output,bytes,(uint32_t)n)) { halt(8); return; }
        if (capture && n<=64u*1024u*1024u-capture_bytes) capture_bytes+=(uint32_t)fwrite(bytes,1,n,capture);
        queued_samples+=n/4; total_bytes+=n;
    }
    if (queued_samples==end_sample && !pod_pcm_queued(output)) { first_sample=end_sample; mode=STOP; notify(1); }
}
static uint32_t msf(uint32_t sectors) { return sectors/(60*75) | ((sectors/75)%60)<<8 | (sectors%75)<<16; }
static uint32_t from_msf(uint32_t value) { return ((value&255)*60+((value>>8)&255))*75+((value>>16)&255); }
static uint32_t absolute_position(void) { return data_frames+audio_index+(uint32_t)(position()/SAMPLES); }
static uint32_t encode_position(uint32_t absolute) {
    if (format==0) return (uint32_t)((uint64_t)absolute*1000/75);
    if (format==3) return absolute;
    if (format==2) return msf(absolute);
    unsigned track=absolute>=data_frames+audio_index?2:1;
    uint32_t offset=track==2?absolute-data_frames-audio_index:absolute;
    return track | msf(offset)<<8;
}
static int decode_position(uint32_t value,uint32_t *absolute) {
    uint64_t result;
    if (format==0) result=(uint64_t)value*75/1000;
    else if (format==3) result=value;
    else if (format==2) result=from_msf(value);
    else {
        unsigned track=value&255;
        if (track==3 && !(value>>8)) { *absolute=data_frames+audio_index+audio_frames; return 0; }
        if (track!=1 && track!=2) return 1;
        result=(track==2?data_frames+audio_index:0)+from_msf(value>>8);
    }
    if (result>(uint64_t)data_frames+audio_index+audio_frames) return 1;
    *absolute=(uint32_t)result; return 0;
}
static void command(void) {
    uint32_t dev=A32(0),msg=A32(1),flags=A32(2),p=A32(3);
    if (msg==0x810 && (dev==516 || dev==DEVICE) && p && (flags&0x100)) {
        uint32_t item=MEM32(p+8); MEM32(p+4)=item==4?516:item==2 || item==7;
        RET(0,4);
    }
    if (msg==0x803 && p && (flags&0x2000)) {
        uint32_t type=MEM32(p+8);
        if ((flags&0x1000)?(type&65535)==516:type && !strcasecmp(hle_ptr(type),"cdaudio")) {
            opened=1; MEM32(p+4)=DEVICE; RET(0,4);
        }
    }
    if (dev!=DEVICE || !opened) RET(0x101,4);
    if (msg==0x80d) {
        if (flags&0x400) { if (!p) RET(0x111,4); uint32_t f=MEM32(p+4); if (f!=0 && f!=2 && f!=3 && f!=10) RET(0x112,4); format=f; }
        RET(0,4);
    }
    if (msg==0x814 && p && (flags&0x100)) {
        uint32_t item=MEM32(p+8),track=MEM32(p+12),r=0;
        pod_cd_pump();
        switch(item) {
        case 1: {
            uint32_t n=ready?(flags&0x10?track==1?data_frames:track==2?audio_frames:0:data_frames+audio_index+audio_frames):0;
            if ((flags&0x10) && track!=1 && track!=2) RET(0x11a,4);
            r=format==0?(uint32_t)((uint64_t)n*1000/75):format==3?n:msf(n); break;
        }
        case 2: {
            if ((flags&0x10) && track!=1 && track!=2) RET(0x11a,4);
            uint32_t n=(flags&0x10)?track==2?data_frames+audio_index:0:(flags&0x10000)?0:absolute_position();
            r=encode_position(n); break;
        }
        case 3:r=2;break;
        case 4:r=mode;break;
        case 5:case 7:r=1;break;
        case 6:r=format;break;
        case 8:r=ready && absolute_position()>=data_frames+audio_index?2:1;break;
        case 0x4001:r=track==2?1088:1089;break;
        default: RET(0x112,4);
        }
        MEM32(p+4)=r; RET(0,4);
    }
    if (msg==0x806) {
        if (!ready) { mode=PLAY; RET(0,4); } /* retained silent compatibility without an image */
        uint32_t from=absolute_position(),to=data_frames+audio_index+audio_frames;
        if (p && (flags&4) && decode_position(MEM32(p+4),&from)) RET(0x11a,4);
        if (p && (flags&8) && decode_position(MEM32(p+8),&to)) RET(0x11a,4);
        if (from<data_frames+audio_index || to<from) RET(0x11a,4);
        if (ensure_output()) RET(0x115,4);
        notify(2); pod_pcm_clear(output);
        first_sample=queued_samples=(uint64_t)(from-data_frames-audio_index)*SAMPLES;
        end_sample=(uint64_t)(to-data_frames-audio_index)*SAMPLES;
        if (fseeko(source,(off_t)((uint64_t)audio_index*SECTOR+first_sample*4),SEEK_SET)) RET(0x115,4);
        callback=(p && (flags&1))?MEM32(p):0; mode=PLAY;
        pod_pcm_pause(output,0); pod_cd_pump();
        fprintf(stderr,"[cd] play: from=%08X to=%08X format=%u; audio sector %u..%u\n",p?MEM32(p+4):0,p?MEM32(p+8):0,format,from-data_frames-audio_index,to-data_frames-audio_index);
        if (flags&2) while (mode==PLAY) { hle_block_begin(); struct timespec delay={0,1000000}; nanosleep(&delay,NULL); hle_block_end(); pod_cd_pump(); }
        RET(0,4);
    }
    if (msg==0x809) { if (output) pod_pcm_pause(output,1); mode=PAUSE; RET(0,4); }
    if (msg==0x855) { if (output) pod_pcm_pause(output,0); mode=PLAY; RET(0,4); }
    if (msg==0x808 || msg==0x804) { halt(4); if (msg==0x804) opened=0; RET(0,4); }
    if (msg==0x807) {
        uint32_t absolute=(flags&0x200)?data_frames+audio_index+audio_frames:data_frames+audio_index;
        if (!(flags&0x300) && (!p || decode_position(MEM32(p+4),&absolute))) RET(0x11a,4);
        if (absolute<data_frames+audio_index) RET(0x11a,4);
        halt(4); first_sample=queued_samples=(uint64_t)(absolute-data_frames-audio_index)*SAMPLES; RET(0,4);
    }
    RET(0x112,4);
}
void pod_cd_shutdown(void) {
    if (output) { pod_pcm_close(output); output=0; }
    if (source) { fclose(source); source=NULL; }
    if (capture) { capture_header(); fclose(capture); capture=NULL; }
    fprintf(stderr,"[cd] submitted=%llu bytes, capture=%u bytes\n",(unsigned long long)total_bytes,capture_bytes);
    ready=opened=0; mode=STOP;
}
const win32hle_shim pod_cd_shims[]={{"mciSendCommandA",command},{NULL,NULL}};
