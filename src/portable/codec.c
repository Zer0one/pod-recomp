/* Host the disc's already lifted codec through its public installable-driver
 * ABI. API constants/layouts: Microsoft Video Compression Manager reference,
 * https://learn.microsoft.com/windows/win32/multimedia/video-compression-manager-reference
 * No decoder algorithm or game-derived address is implemented here. */
#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "win32hle.h"
#include "pe_loader.h"
#include "pe_format.h"
#include "codec.h"
static pe_image image;
static uint32_t driver;
static int loaded;
int pod_codec_map(const char *path) {
    /* Before the first guest heap/stack allocation: the lift requires the
     * DLL's preferred base, so relocation is not an option. */
    return recomp_pe_map(path,&image);
}
static uint32_t exported(const char *name) {
    uint32_t nt=image.base+MEM32(image.base+60);
    uint32_t rva=MEM32(nt+24+96),bytes=MEM32(nt+24+100);
    if (!rva || rva>=image.span || bytes>image.span-rva) return 0;
    uint32_t e=image.base+rva,n=MEM32(e+24);
    if (n>65536) return 0;
    for (uint32_t i=0;i<n;i++) {
        uint32_t nm=MEM32(image.base+MEM32(e+32)+i*4);
        if (nm>=image.span || strcmp(hle_ptr(image.base+nm),name)) continue;
        uint16_t ordinal=MEM16(image.base+MEM32(e+36)+i*2);
        if (ordinal>=MEM32(e+20)) return 0;
        uint32_t target=MEM32(image.base+MEM32(e+28)+ordinal*4);
        if (target>=image.span || (target>=rva && target<rva+bytes)) return 0;
        return image.base+target;
    }
    return 0;
}
static uint32_t call(uint32_t id,uint32_t msg,uint32_t p1,uint32_t p2) {
    uint32_t a[5]={id,1,msg,p1,p2};
    return hle_call_guest(driver,5,a);
}
static uint32_t start(void) {
    if (!loaded) {
    /* This HLE represents both local and global handles by their heap VA.
     * Reuse its existing GlobalHandle implementation for the missing alias. */
    static win32hle_shim alias[2];
    for (const win32hle_shim *s=win32hle_kernel32;s->name;s++)
        if (!strcmp(s->name,"GlobalHandle")) {
            alias[0].name="LocalHandle"; alias[0].fn=s->fn;
            win32hle_register(alias); break;
        }
    hle_module_add("C:\\INSTALL\\IR41_32.DLL",image.base,image.resource_rva,image.resource_size,0);
    int unresolved=recomp_pe_bind(&image,hle_resolve_or_stub);
    driver=exported("DriverProc");
    if (!driver || !recomp_lookup(driver) || !recomp_lookup(image.entry)) return 0;
    uint32_t a[3]={image.base,1,0};
    uint32_t ok=hle_call_guest(image.entry,3,a);
    fprintf(stderr,"[codec] DllMain=%u DriverProc=%08X unresolved=%d\n",ok,driver,unresolved);
    if (!ok) return 0;
    uint32_t load=call(0,1,0,0),enable=call(0,2,0,0);
    fprintf(stderr,"[codec] DRV_LOAD=%u DRV_ENABLE=%u\n",load,enable);
    loaded=1;
    }
    uint32_t *open=hle_alloc(36); open[0]=36;
    open[1]=0x63646976; open[2]=0x31347669; open[4]=2; /* vidc / iv41 / decompress */
    uint32_t id=call(0,3,0,hle_va(open));
    fprintf(stderr,"[codec] instance=%08X error=%u\n",id,open[5]);
    hle_free(open);
    return id;
}
static void stop(uint32_t id) {
    call(id,4,0,0);
}
static void unload(void) {
    if (!loaded) return;
    call(0,5,0,0); call(0,6,0,0);
    uint32_t a[3]={image.base,0,0}; hle_call_guest(image.entry,3,a); loaded=0;
}
int pod_codec_probe(void) {
    uint32_t id=start(); if (!id) return 1;
    stop(id); unload();
    fprintf(stderr,"[codec] probe passed (initialization only, no decoded frames)\n");
    return 0;
}

/* A bounded RIFF reader for the offline decode comparison. The in-game VfW
 * player is not bypassed or replaced by this diagnostic. */
typedef struct { const uint8_t *data; uint32_t size,flags; } packet;
typedef struct {
    const uint8_t *format,*header; uint32_t format_size;
    packet *packets; unsigned count,capacity;
    uint8_t *pcm; uint32_t pcm_bytes; SDL_AudioSpec spec;
    uint8_t wave_format[18];
} audio_stream;
typedef struct {
    audio_stream audio[16];
    const uint8_t *format,*header; uint32_t format_size;
    packet *packets; unsigned count,capacity,streams;
    int video;
} avi;
static uint32_t u32(const uint8_t *p) { uint32_t v; memcpy(&v,p,4); return v; }
static int video_chunk(const uint8_t *p,int stream) {
    return p[0]>='0' && p[0]<='9' && p[1]>='0' && p[1]<='9' &&
        (p[0]-'0')*10+p[1]-'0'==stream && p[2]=='d' && (p[3]=='c' || p[3]=='b');
}
static int chunks(avi *a,const uint8_t *p,size_t n,int depth,int list_stream,int movi) {
    if (depth>8) return 1;
    size_t off=0;
    while (off+8<=n) {
        const uint8_t *c=p+off,*d=c+8; uint32_t size=u32(c+4);
        if (size>n-off-8) return 1;
        if (!memcmp(c,"LIST",4)) {
            if (size<4) return 1;
            int stream=list_stream;
            if (!memcmp(d,"strl",4)) stream=(int)a->streams++;
            if (chunks(a,d+4,size-4,depth+1,stream,movi || !memcmp(d,"movi",4))) return 1;
        } else if (!memcmp(c,"strh",4) && size>=56 && list_stream>=0 && list_stream<16) {
            a->audio[list_stream].header=d;
            if (!memcmp(d,"vids",4)) {
                if (a->video>=0) return 1;
                a->video=list_stream; a->header=d;
            }
        } else if (!memcmp(c,"strf",4) && list_stream>=0 && list_stream<16) {
            a->audio[list_stream].format=d; a->audio[list_stream].format_size=size;
            if (list_stream==a->video) { a->format=d; a->format_size=size; }
        } else if (movi && video_chunk(c,a->video)) {
            if (a->count==100000) return 1;
            if (a->count==a->capacity) {
                unsigned capacity=a->capacity?a->capacity*2:256;
                packet *next=realloc(a->packets,capacity*sizeof *next);
                if (!next) return 1;
                a->packets=next; a->capacity=capacity;
            }
            packet *pkt=&a->packets[a->count++];
            pkt->data=d; pkt->size=size; pkt->flags=a->count==1?0:0x08000000u;
        } else if (movi && c[0]>='0' && c[0]<='9' && c[1]>='0' && c[1]<='9' && c[2]=='w' && c[3]=='b') {
            unsigned stream=(c[0]-'0')*10+c[1]-'0'; if (stream>=16) return 1;
            audio_stream *track=&a->audio[stream];
            if (track->count==100000) return 1;
            if (track->count==track->capacity) {
                unsigned capacity=track->capacity?track->capacity*2:256;
                packet *next=realloc(track->packets,capacity*sizeof *next); if (!next) return 1;
                track->packets=next; track->capacity=capacity;
            }
            packet *pkt=&track->packets[track->count++]; pkt->data=d; pkt->size=size; pkt->flags=0;
        } else if (!memcmp(c,"idx1",4)) {
            unsigned frame=0;
            for (size_t i=0;i+16<=size;i+=16) if (video_chunk(d+i,a->video)) {
                if (frame<a->count) a->packets[frame++].flags=(u32(d+i+4)&16)?0:0x08000000u;
            }
        }
        size_t step=8+(size_t)size+(size&1);
        if (step>n-off) return 1;
        off+=step;
    }
    return off!=n;
}
static void free_index(avi *a) {
    free(a->packets);
    for (unsigned i=0;i<16;i++) { free(a->audio[i].packets); if (a->audio[i].pcm) SDL_FreeWAV(a->audio[i].pcm); }
}
static unsigned seek_keyframe(const avi *a,unsigned frame) {
    while (frame && (a->packets[frame].flags&0x08000000u)) frame--;
    return frame;
}
int pod_codec_decode(const char *input,const char *output,unsigned first) {
    FILE *in=fopen(input,"rb"),*out=NULL; uint8_t *file=NULL;
    avi a={0}; a.video=-1;
    uint32_t id=0,format=0,bih=0,pixels=0,compressed=0,params=0;
    int result=1,begun=0; unsigned decoded=0;
    if (!in) { perror(input); goto done; }
    if (fseek(in,0,SEEK_END)) goto done;
    long length=ftell(in); if (length<12 || length>512*1024*1024) goto done;
    rewind(in); file=malloc((size_t)length); if (!file) goto done;
    if (fread(file,1,(size_t)length,in)!=(size_t)length) goto done;
    if (memcmp(file,"RIFF",4) || memcmp(file+8,"AVI ",4) || u32(file+4)>(size_t)length-8 ||
        u32(file+4)<4 || chunks(&a,file+12,u32(file+4)-4,0,-1,0) || !a.count ||
        !a.format || a.format_size<40 || a.format_size>4096) goto done;
    if (first>=a.count) goto done;
    int32_t width=(int32_t)u32(a.format+4),height=(int32_t)u32(a.format+8);
    if (width<1 || width>4096 || height<1 || height>4096 || memcmp(a.format+16,"IV41",4)) goto done;
    unsigned row=((unsigned)width*3+3)&~3u,bytes=row*(unsigned)height,max_packet=1;
    for (unsigned i=0;i<a.count;i++) if (a.packets[i].size>max_packet) max_packet=a.packets[i].size;
    if (max_packet>32*1024*1024) goto done;
    format=hle_va(hle_alloc(a.format_size)); bih=hle_va(hle_alloc(40));
    pixels=hle_va(hle_alloc(bytes)); compressed=hle_va(hle_alloc(max_packet)); params=hle_va(hle_alloc(24));
    if (!format || !bih || !pixels || !compressed || !params) goto done;
    memcpy(hle_ptr(format),a.format,a.format_size);
    MEM32(bih)=40; MEM32(bih+4)=width; MEM32(bih+8)=height;
    MEM16(bih+12)=1; MEM16(bih+14)=24; MEM32(bih+20)=bytes;
    id=start(); if (!id) goto done;
    uint32_t query=call(id,0x400b,format,bih),begin;
    if (query) { fprintf(stderr,"[codec] query failed: %d\n",(int)query); goto done; }
    begin=call(id,0x400c,format,bih);
    if (begin) { fprintf(stderr,"[codec] begin failed: %d\n",(int)begin); goto done; }
    begun=1; out=fopen(output,"wb"); if (!out) { perror(output); goto done; }
    for (unsigned i=seek_keyframe(&a,first);i<a.count;i++) {
        packet *pkt=&a.packets[i];
        memcpy(hle_ptr(compressed),pkt->data,pkt->size); MEM32(format+20)=pkt->size;
        MEM32(params)=pkt->flags; MEM32(params+4)=format; MEM32(params+8)=compressed;
        MEM32(params+12)=bih; MEM32(params+16)=pixels;
        uint32_t r=call(id,0x400d,params,24);
        if (r) { fprintf(stderr,"[codec] frame %u failed: %d\n",i,(int)r); goto done; }
        if (i>=first) { if (fwrite(hle_ptr(pixels),1,bytes,out)!=bytes) goto done; decoded++; }
    }
    fprintf(stderr,"[codec] decoded %u frames, %dx%d bottom-up BGR24 -> %s\n",decoded,width,height,output);
    result=0;
 done:
    if (begun) call(id,0x400e,0,0);
    if (id) stop(id);
    unload();
    uint32_t blocks[]={format,bih,pixels,compressed,params};
    for (unsigned i=0;i<5;i++) if (blocks[i]) hle_free(hle_ptr(blocks[i]));
    if (in) fclose(in);
    if (out && fclose(out)) result=1;
    free(file); free_index(&a);
    if (result) fprintf(stderr,"[codec] decode failed after %u frames\n",decoded);
    return result;
}

/* Read-only VfW hosting for POD's own player. SDL's existing WAV loader
 * converts the selected AVI audio track to PCM before exposing it. */
typedef struct { uint8_t *data; avi index; unsigned references; } movie;
typedef struct ticket {
    struct ticket *next;
    uint32_t magic,kind; movie *file; int stream;
    uint32_t id,format,bih,pixels,compressed,params;
    unsigned row,height,capacity,requests;
    uint32_t first_frame,clock_start,show_first,show_last,show_started;
    uint32_t presentation_waits,presentation_wait_ms,presentation_rebases;
    int last,begun,showing;
} ticket;
static ticket *frame_contexts;
enum { TICKET=0x504f4456, FILE_T=1, STREAM_T=2, FRAME_T=3 };
static ticket *get(uint32_t h,unsigned kind) {
    ticket *t=hle_handle_obj(h,HLE_H_OTHER);
    return t && t->magic==TICKET && t->kind==kind?t:NULL;
}
static uint32_t new_ticket(movie *m,unsigned kind) {
    ticket *t=calloc(1,sizeof *t); if (!t) return 0;
    t->magic=TICKET; t->kind=kind; t->file=m; t->last=-1; t->stream=m->index.video; m->references++;
    uint32_t h=hle_handle_alloc(HLE_H_OTHER,t);
    if (!h) { m->references--; free(t); }
    else if (kind==FRAME_T) { t->next=frame_contexts; frame_contexts=t; }
    return h;
}
static void release_ticket(uint32_t h,ticket *t) {
    movie *m=t->file;
    if (t->kind==FRAME_T) {
        ticket **link=&frame_contexts;
        while (*link && *link!=t) link=&(*link)->next;
        if (*link) *link=t->next;
    }
    if (t->kind==FRAME_T) fprintf(stderr,"[avi] frame context closed: requests=%u first=%u last=%d elapsed=%u ms\n",t->requests,t->first_frame,t->last,t->requests?hle_ticks_ms()-t->clock_start:0);
    if (t->showing) fprintf(stderr,"[avi] presentation: frame=%u..%u waited=%u ms sleeps=%u origins=%u\n",
        t->show_first,t->show_last,t->presentation_wait_ms,t->presentation_waits,t->presentation_rebases);
    if (t->id) { if (t->begun) call(t->id,0x400e,0,0); stop(t->id); }
    uint32_t blocks[]={t->format,t->bih,t->compressed,t->params};
    for (unsigned i=0;i<4;i++) if (blocks[i]) hle_free(hle_ptr(blocks[i]));
    t->magic=0; hle_handle_close(h); free(t);
    if (!--m->references) { free(m->data); free_index(&m->index); free(m); }
}
static movie *load_movie(const char *guest) {
    char path[4096];
    if (!guest || !hle_host_path(guest,path,sizeof path)) return NULL;
    FILE *f=fopen(path,"rb"); if (!f) return NULL;
    movie *m=calloc(1,sizeof *m); long n=-1;
    if (fseek(f,0,SEEK_END)==0) n=ftell(f);
    if (!m || n<12 || n>512*1024*1024) { free(m); fclose(f); return NULL; }
    m->index.video=-1; m->data=malloc((size_t)n); rewind(f);
    int bad=!m->data || fread(m->data,1,(size_t)n,f)!=(size_t)n; fclose(f);
    if (!bad) bad=memcmp(m->data,"RIFF",4) || memcmp(m->data+8,"AVI ",4) ||
        u32(m->data+4)>(size_t)n-8 || u32(m->data+4)<4;
    if (!bad) bad=chunks(&m->index,m->data+12,u32(m->data+4)-4,0,-1,0) ||
        !m->index.count || !m->index.header || !m->index.format ||
        m->index.format_size<40 || m->index.format_size>4096 || memcmp(m->index.format+16,"IV41",4);
    if (bad) { free(m->data); free_index(&m->index); free(m); return NULL; }
    fprintf(stderr,"[avi] opened %s: %u IV41 frames\n",guest,m->index.count);
    return m;
}
static void avi_init(void) { RET(0,0); }
static void avi_open(void) {
    if (!A32(0) || (A32(2)&0x1003u)) RET(0x80044067u,4);
    MEM32(A32(0))=0; movie *m=load_movie(ASTR(1)); if (!m) RET(0x80044067u,4);
    uint32_t h=new_ticket(m,FILE_T);
    if (!h) { free(m->data); free_index(&m->index); free(m); RET(0x80044065u,4); }
    MEM32(A32(0))=h; RET(0,4);
}
static void file_release(void) { ticket *t=get(A32(0),FILE_T); if (t) release_ticket(A32(0),t); RET(0,1); }
static void stream_release(void) { ticket *t=get(A32(0),STREAM_T); if (t) release_ticket(A32(0),t); RET(0,1); }
static void put32(uint8_t *p,uint32_t n) { memcpy(p,&n,4); }
static void put16(uint8_t *p,uint16_t n) { memcpy(p,&n,2); }
static int load_audio(audio_stream *a) {
    if (a->pcm) return 0;
    if (!a->header || memcmp(a->header,"auds",4) || !a->format || a->format_size<16 || a->format_size>4096) return 1;
    uint64_t total=0;
    for (unsigned i=0;i<a->count;i++) total+=a->packets[i].size;
    if (!total || total>128*1024*1024) return 1;
    unsigned fmt_bytes=a->format_size+(a->format_size&1),bytes=28+fmt_bytes+(unsigned)total+((unsigned)total&1);
    uint8_t *wav=calloc(1,bytes); if (!wav) return 1;
    memcpy(wav,"RIFF",4); put32(wav+4,bytes-8); memcpy(wav+8,"WAVEfmt ",8);
    put32(wav+16,a->format_size); memcpy(wav+20,a->format,a->format_size);
    unsigned pos=20+fmt_bytes; memcpy(wav+pos,"data",4); put32(wav+pos+4,(uint32_t)total); pos+=8;
    for (unsigned i=0;i<a->count;i++) { memcpy(wav+pos,a->packets[i].data,a->packets[i].size); pos+=a->packets[i].size; }
    SDL_RWops *rw=SDL_RWFromConstMem(wav,(int)bytes);
    int ok=rw && SDL_LoadWAV_RW(rw,1,&a->spec,&a->pcm,&a->pcm_bytes);
    free(wav);
    if (!ok || (a->spec.format!=AUDIO_U8 && a->spec.format!=AUDIO_S16LSB) ||
        (a->spec.channels!=1 && a->spec.channels!=2)) {
        if (a->pcm) SDL_FreeWAV(a->pcm); a->pcm=NULL;
        fprintf(stderr,"[avi] SDL audio conversion failed: %s\n",SDL_GetError()); return 1;
    }
    unsigned bits=SDL_AUDIO_BITSIZE(a->spec.format),align=a->spec.channels*bits/8;
    put16(a->wave_format,1); put16(a->wave_format+2,a->spec.channels);
    put32(a->wave_format+4,a->spec.freq); put32(a->wave_format+8,a->spec.freq*align);
    put16(a->wave_format+12,align); put16(a->wave_format+14,bits);
    fprintf(stderr,"[avi] audio converted via SDL: %u bytes PCM, %d Hz %u channels\n",a->pcm_bytes,a->spec.freq,a->spec.channels);
    return 0;
}
static audio_stream *ticket_audio(ticket *t) {
    if (!t || t->kind!=STREAM_T || t->stream==t->file->index.video || t->stream<0 || t->stream>=16) return NULL;
    return &t->file->index.audio[t->stream];
}
static uint32_t open_stream(movie *m,uint32_t type,unsigned ordinal) {
    int chosen=-1;
    for (unsigned i=0;i<m->index.streams && i<16;i++) {
        audio_stream *track=&m->index.audio[i];
        if (track->header && (!type || u32(track->header)==type)) { if (!ordinal--) { chosen=(int)i; break; } }
    }
    if (chosen<0 || (chosen!=m->index.video && load_audio(&m->index.audio[chosen]))) return 0;
    uint32_t h=new_ticket(m,STREAM_T); ticket *t=get(h,STREAM_T); if (t) t->stream=chosen; return h;
}
static void file_stream(void) {
    ticket *t=get(A32(0),FILE_T);
    if (!A32(1)) RET(0x8004406au,4); MEM32(A32(1))=0;
    if (!t) RET(0x80044066u,4);
    MEM32(A32(1))=open_stream(t->file,A32(2),A32(3)); RET(MEM32(A32(1))?0:0x80044066u,4);
}
static void stream_from_file(void) {
    if (!A32(0)) RET(0x8004406au,6); MEM32(A32(0))=0;
    if (A32(4)&0x1003u) RET(0x80044066u,6);
    movie *m=load_movie(ASTR(1)); if (!m) RET(0x80044067u,6);
    uint32_t h=open_stream(m,A32(2),A32(3));
    if (!h) { free(m->data); free_index(&m->index); free(m); RET(0x80044066u,6); }
    MEM32(A32(0))=h; RET(0,6);
}
static void file_info(void) {
    ticket *t=get(A32(0),FILE_T); if (!t || !A32(1) || A32(2)<108) RET(0x8004406au,3);
    uint32_t p=A32(1); avi *a=&t->file->index; memset(hle_ptr(p),0,108);
    MEM32(p+8)=1; MEM32(p+12)=a->streams; MEM32(p+20)=u32(a->format+4); MEM32(p+24)=u32(a->format+8);
    MEM32(p+28)=u32(a->header+20); MEM32(p+32)=u32(a->header+24); MEM32(p+36)=a->count;
    memcpy(hle_ptr(p+44),"POD IV41 video",15); RET(0,3);
}
static void stream_info(void) {
    ticket *t=get(A32(0),STREAM_T); if (!t || !A32(1) || A32(2)<140) RET(0x8004406au,3);
    uint32_t p=A32(1); avi *a=&t->file->index; memset(hle_ptr(p),0,140);
    audio_stream *audio=ticket_audio(t);
    if (audio) {
        unsigned block=audio->spec.channels*SDL_AUDIO_BITSIZE(audio->spec.format)/8;
        MEM32(p)=0x73647561u; MEM32(p+20)=1; MEM32(p+24)=audio->spec.freq;
        MEM32(p+32)=audio->pcm_bytes/block; MEM32(p+48)=block; RET(0,3);
    }
    MEM32(p)=0x73646976u; MEM32(p+4)=0x31345649u;
    MEM32(p+20)=u32(a->header+20); MEM32(p+24)=u32(a->header+24); MEM32(p+32)=a->count;
    MEM32(p+60)=u32(a->format+4); MEM32(p+64)=u32(a->format+8); RET(0,3);
}
static void stream_length(void) {
    ticket *t=get(A32(0),STREAM_T); if (!t) RET(0xffffffffu,1);
    audio_stream *a=ticket_audio(t); RET(a?a->pcm_bytes/(a->spec.channels*SDL_AUDIO_BITSIZE(a->spec.format)/8):t->file->index.count,1);
}
static void stream_start(void) { RET(get(A32(0),STREAM_T)?0:0xffffffffu,1); }
static void sample_time(void) {
    ticket *t=get(A32(0),STREAM_T); if (!t || (int32_t)A32(1)<0) RET(0xffffffffu,2);
    avi *a=&t->file->index; uint32_t scale=u32(a->header+20),rate=u32(a->header+24);
    audio_stream *audio=ticket_audio(t); if (audio) { scale=1; rate=audio->spec.freq; }
    RET(rate?(uint32_t)((uint64_t)A32(1)*scale*1000/rate):0xffffffffu,2);
}
static void time_sample(void) {
    ticket *t=get(A32(0),STREAM_T); if (!t || (int32_t)A32(1)<0) RET(0xffffffffu,2);
    avi *a=&t->file->index; uint32_t scale=u32(a->header+20),rate=u32(a->header+24);
    audio_stream *audio=ticket_audio(t); if (audio) { scale=1; rate=audio->spec.freq; }
    RET(scale?(uint32_t)((uint64_t)A32(1)*rate/((uint64_t)scale*1000)):0xffffffffu,2);
}
static void read_format(void) {
    ticket *t=get(A32(0),STREAM_T); if (!t || !A32(3)) RET(0x8004406au,4);
    avi *a=&t->file->index; uint32_t want=MEM32(A32(3));
    audio_stream *audio=ticket_audio(t);
    if (audio) {
        MEM32(A32(3))=18;
        if (A32(2)) { if (want<18) RET(0x80044074u,4); memcpy(APTR(2),audio->wave_format,18); }
        RET(0,4);
    }
    MEM32(A32(3))=a->format_size;
    if (A32(2)) { if (want<a->format_size) RET(0x80044074u,4); memcpy(APTR(2),a->format,a->format_size); }
    RET(0,4);
}
static void read_stream(void) {
    ticket *t=get(A32(0),STREAM_T); if (A32(5)) MEM32(A32(5))=0; if (A32(6)) MEM32(A32(6))=0;
    if (!t) RET(0x8004406au,7);
    audio_stream *audio=ticket_audio(t);
    if (audio) {
        unsigned block=audio->spec.channels*SDL_AUDIO_BITSIZE(audio->spec.format)/8;
        uint64_t start=(uint64_t)A32(1)*block;
        if (start>=audio->pcm_bytes) RET(0,7);
        uint32_t frames=audio->pcm_bytes/(uint32_t)block-A32(1),want=A32(2);
        if (want==0xffffffffu) want=A32(3)?A32(4)/block:1024;
        if (frames>want) frames=want;
        if (A32(3) && frames>A32(4)/block) frames=A32(4)/block;
        if (A32(3)) memcpy(APTR(3),audio->pcm+(size_t)start,frames*block);
        if (A32(5)) MEM32(A32(5))=frames*block; if (A32(6)) MEM32(A32(6))=frames;
        RET(0,7);
    }
    avi *a=&t->file->index; unsigned i=A32(1);
    if (i>=a->count || !A32(2)) RET(0,7);
    packet *p=&a->packets[i]; if (A32(3) && A32(4)<p->size) RET(0x80044074u,7);
    if (A32(3)) memcpy(APTR(3),p->data,p->size);
    if (A32(5)) MEM32(A32(5))=p->size; if (A32(6)) MEM32(A32(6))=1; RET(0,7);
}
static void frame_open(void) {
    ticket *stream=get(A32(0),STREAM_T); if (!stream || ticket_audio(stream)) RET(0,2);
    avi *a=&stream->file->index; int w=(int)u32(a->format+4),h=(int)u32(a->format+8);
    if (w<1 || w>4096 || h<1 || h>4096) RET(0,2);
    uint32_t handle=new_ticket(stream->file,FRAME_T); ticket *t=get(handle,FRAME_T); if (!t) RET(0,2);
    t->row=((unsigned)w*3+3)&~3u; t->height=h; t->capacity=1;
    for (unsigned i=0;i<a->count;i++) if (a->packets[i].size>t->capacity) t->capacity=a->packets[i].size;
    if (t->capacity>32*1024*1024) { release_ticket(handle,t); RET(0,2); }
    t->format=hle_va(hle_alloc(a->format_size)); t->bih=hle_va(hle_alloc(40+t->row*h));
    t->compressed=hle_va(hle_alloc(t->capacity)); t->params=hle_va(hle_alloc(24));
    if (!t->format || !t->bih || !t->compressed || !t->params) { release_ticket(handle,t); RET(0,2); }
    t->pixels=t->bih+40;
    memcpy(hle_ptr(t->format),a->format,a->format_size);
    MEM32(t->bih)=40; MEM32(t->bih+4)=w; MEM32(t->bih+8)=h; MEM16(t->bih+12)=1; MEM16(t->bih+14)=24; MEM32(t->bih+20)=t->row*h;
    t->id=start();
    if (!t->id || call(t->id,0x400b,t->format,t->bih) || call(t->id,0x400c,t->format,t->bih)) {
        release_ticket(handle,t); RET(0,2);
    }
    fprintf(stderr,"[avi] frame open: want=%X output=%dx%d BGR24\n",A32(1),w,h);
    t->begun=1; RET(handle,2);
}
static void frame_get(void) {
    ticket *t=get(A32(0),FRAME_T); unsigned frame=A32(1); if (!t || frame>=t->file->index.count) RET(0,2);
    if (!t->requests) { t->first_frame=frame; t->clock_start=hle_ticks_ms(); }
    if (++t->requests<=3) fprintf(stderr,"[avi] get frame %u\n",frame);
    if ((int)frame<t->last) { call(t->id,0x400e,0,0); if (call(t->id,0x400c,t->format,t->bih)) RET(0,2); t->last=-1; }
    if (t->last<0) {
        /* A new/rewound decompressor starts at the preceding keyframe, not
         * at frame zero. POD probes the final frame when skipping a movie. */
        unsigned key=seek_keyframe(&t->file->index,frame);
        t->last=(int)key-1;
    }
    for (unsigned i=(unsigned)(t->last+1);i<=frame;i++) {
        packet *p=&t->file->index.packets[i]; memcpy(hle_ptr(t->compressed),p->data,p->size); MEM32(t->format+20)=p->size;
        MEM32(t->params)=p->flags; MEM32(t->params+4)=t->format; MEM32(t->params+8)=t->compressed;
        MEM32(t->params+12)=t->bih; MEM32(t->params+16)=t->pixels;
        if (call(t->id,0x400d,t->params,24)) RET(0,2); t->last=(int)i;
    }
    RET(t->bih,2);
}
void pod_video_pace(uint32_t dib) {
    ticket *t=frame_contexts;
    while (t && t->bih!=dib) t=t->next;
    if (!t || t->last<0) return;
    avi *a=&t->file->index;
    for (unsigned i=0;i<a->streams && i<16;i++)
        if (a->audio[i].header && a->audio[i].count && !memcmp(a->audio[i].header,"auds",4)) return;
    /* Silent clips have no waveOut clock. Pace their presentation from the
     * AVI timestamps; leave decode/seek APIs and clips with audio untouched.
     * A final-frame probe gets its own origin, so skipping never waits for
     * the full duration of the movie. */
    uint32_t frame=(uint32_t)t->last,rate=u32(a->header+24),scale=u32(a->header+20);
    if (!rate || !scale) return;
    if (!t->showing || frame<t->show_last) {
        if (t->showing) fprintf(stderr,"[avi] presentation sequence: frame=%u..%u elapsed=%u ms expected=%llu ms\n",
            t->show_first,t->show_last,hle_ticks_ms()-t->show_started,
            (unsigned long long)((uint64_t)(t->show_last-t->show_first)*scale*1000/rate));
        t->presentation_rebases++;
        t->showing=1; t->show_first=frame; t->show_started=hle_ticks_ms();
    }
    t->show_last=frame;
    uint32_t due=t->show_started+(uint32_t)((uint64_t)(frame-t->show_first)*scale*1000/rate);
    for (;;) {
        int32_t wait=(int32_t)(due-hle_ticks_ms()); if (wait<=0) break;
        uint32_t before=hle_ticks_ms();
        hle_block_begin(); SDL_Delay((Uint32)(wait>20?20:wait)); hle_block_end();
        /* Revalidate after yielding: the frame context may have been closed
         * while this thread was blocked. */
        t=frame_contexts; while (t && t->bih!=dib) t=t->next;
        if (!t) return;
        t->presentation_waits++; t->presentation_wait_ms+=hle_ticks_ms()-before;
    }
}
static void frame_close(void) { ticket *t=get(A32(0),FRAME_T); if (t) release_ticket(A32(0),t); RET(0,1); }
const win32hle_shim pod_video_shims[]={
    {"AVIFileInit",avi_init},{"AVIFileExit",avi_init},{"AVIFileOpenA",avi_open},
    {"AVIFileRelease",file_release},{"AVIFileInfoA",file_info},{"AVIFileGetStream",file_stream},
    {"AVIStreamOpenFromFileA",stream_from_file},{"AVIStreamRelease",stream_release},
    {"AVIStreamInfoA",stream_info},{"AVIStreamStart",stream_start},{"AVIStreamLength",stream_length},
    {"AVIStreamReadFormat",read_format},{"AVIStreamRead",read_stream},
    {"AVIStreamSampleToTime",sample_time},{"AVIStreamTimeToSample",time_sample},
    {"AVIStreamGetFrameOpen",frame_open},{"AVIStreamGetFrame",frame_get},{"AVIStreamGetFrameClose",frame_close},
    {NULL,NULL}
};
