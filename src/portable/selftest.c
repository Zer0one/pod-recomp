/* Synthetic inputs only: no game code, catalog or binary-derived data. */
#include <SDL2/SDL.h>
#include <stdio.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "host.h"
#include "pe_format.h"
#include "pe_loader.h"
#include "dinput.h"
#include "input_sdl.h"
#include "audio.h"
static volatile int worker_count;
#ifdef __APPLE__
static size_t worker_stack_size;
#endif
static int activated,focused;
static int audio_opened,audio_done,audio_closed;
static uint32_t audio_header;
static void audio_callback(void) {
    if (A32(1)==0x3bb) audio_opened++;
    if (A32(1)==0x3bd) { audio_done++; audio_header=A32(3); }
    if (A32(1)==0x3bc) audio_closed++;
    RET(0,5);
}
static void window_proc(void) {
    if (A32(1)==0x1c && A32(2)==1) activated++;
    if (A32(1)==7) focused++;
    RET(A32(1)==0x81,4);
}
static void worker(void) {
#ifdef __APPLE__
    worker_stack_size=pthread_get_stacksize_np(pthread_self());
#endif
    for (;;) {
        __sync_add_and_fetch(&worker_count,1);
        uint32_t ms=2;
        hle_call_guest(hle_resolve("Sleep"),1,&ms);
    }
}
static void blocked_worker(void) {
    uint32_t a[4]={A32(0),0,0,0};
    hle_call_guest(hle_resolve("GetMessageA"),4,a);
    RET(0,1);
}
const recomp_dispatch_entry_t recomp_dispatch_table[]={
    {0x90000000,worker},{0x90000010,window_proc},{0x90000020,audio_callback},{0x90000030,blocked_worker}};
const uint32_t recomp_dispatch_count=4;
static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#c); failures++; } } while(0)
static uint32_t method(uint32_t obj,unsigned slot,int n,uint32_t *a) {
    a[0]=obj;
    return hle_call_guest(MEM32(MEM32(obj)+slot*4),n,a);
}
static void loader_test(void) {
    uint8_t file[1024]={0};
    pe_dos_header *dos=(void *)file; dos->e_magic=0x5a4d; dos->e_lfanew=64;
    pe_nt_headers32 *nt=(void *)(file+64); nt->Signature=0x4550;
    nt->FileHeader.Machine=0x14c; nt->FileHeader.NumberOfSections=2;
    nt->FileHeader.SizeOfOptionalHeader=sizeof(pe_opt_header32);
    pe_opt_header32 *o=&nt->OptionalHeader;
    o->Magic=0x10b; o->ImageBase=0x400000; o->SizeOfImage=0x30000;
    o->SizeOfHeaders=512; o->SectionAlignment=4096; o->FileAlignment=512;
    o->NumberOfRvaAndSizes=16; o->AddressOfEntryPoint=0x1000;
    pe_section_header *sec=(void *)((uint8_t *)o+sizeof *o);
    sec[0].VirtualAddress=0x1000; sec[0].SizeOfRawData=512; sec[0].PointerToRawData=512;
    sec[1].VirtualAddress=0x10000; sec[1].SizeOfRawData=0x10000; /* no backing */
    memset(file+512,0xa5,512);
    char path[]="/tmp/pod-loader-XXXXXX";
    int fd=mkstemp(path); CHECK(fd>=0); if (fd<0) return;
    CHECK(write(fd,file,sizeof file)==sizeof file); close(fd);
    pe_image image;
    int rc=recomp_pe_map(path,&image); CHECK(rc==0);
    if (!rc) {
        CHECK(MEM32(image.base+0x1000)==0xa5a5a5a5);
        CHECK(MEM32(image.base+0x10000)==0);
        CHECK(MEM32(image.base+0x1fffc)==0);
        hle_vm_release(image.base,image.span);
    }
    unlink(path);
}
static void keyboard_test(void) {
    uint32_t *storage=hle_alloc(512),out=hle_va(storage),guid=out+16,events=out+64,pn=out+48;
    MEM32(guid)=0x6f1d2b61;
    uint32_t a[5]={0,0x300,out,0};
    CHECK(hle_call_guest(hle_resolve("DirectInputCreateA"),4,a)==0);
    uint32_t input=MEM32(out);
    a[1]=guid; a[2]=out+4; a[3]=0;
    CHECK(method(input,3,4,a)==0);
    uint32_t keyboard=MEM32(out+4);
    uint32_t ea[4]={0,0,0,0};
    uint32_t event=hle_call_guest(hle_resolve("CreateEventA"),4,ea);
    a[1]=event; CHECK(method(keyboard,12,2,a)==0);
    pod_keyboard_dik(0,1); pod_keyboard_dik(256,1); /* invalid keys are ignored */
    pod_keyboard_key(SDL_SCANCODE_UP,1);
    pod_keyboard_dik(0xc8,1); /* same transition through the frontend-neutral API */
    CHECK(hle_wait(1,&event,0,0)==0);
    MEM32(pn)=20; a[1]=16; a[2]=events; a[3]=pn; a[4]=1;
    CHECK(method(keyboard,10,5,a)==0);
    CHECK(MEM32(pn)==1 && MEM32(events)==0xc8 && MEM32(events+4)==0x80);
    MEM32(pn)=20; a[4]=0;
    CHECK(method(keyboard,10,5,a)==0); CHECK(MEM32(pn)==1);
    a[1]=256; a[2]=events;
    CHECK(method(keyboard,9,3,a)==0); CHECK(((uint8_t *)hle_ptr(events))[0xc8]==0x80);
    pod_keyboard_release();
    MEM32(pn)=20; a[1]=16; a[2]=events; a[3]=pn; a[4]=0;
    CHECK(method(keyboard,10,5,a)==0);
    CHECK(MEM32(pn)==1 && MEM32(events+4)==0);
    MEM32(pn)=20; CHECK(method(keyboard,10,5,a)==0); CHECK(MEM32(pn)==0);
    hle_handle_close(event); hle_free(storage);
}
static void thread_test(void) {
    uint32_t a[6]={0,0,0x90000000,0,0,0};
    uint32_t thread=hle_call_guest(hle_resolve("CreateThread"),6,a),ms=20;
    CHECK(thread!=0);
    hle_call_guest(hle_resolve("Sleep"),1,&ms);
    CHECK(worker_count>0);
#ifdef __APPLE__
    CHECK(worker_stack_size>=8u*1024u*1024u);
#endif
    a[0]=thread;
    CHECK(hle_call_guest(hle_resolve("SuspendThread"),1,a)==0);
    hle_call_guest(hle_resolve("Sleep"),1,&ms);
    int suspended=worker_count;
    hle_call_guest(hle_resolve("Sleep"),1,&ms);
    CHECK(worker_count==suspended);
    CHECK(hle_call_guest(hle_resolve("ResumeThread"),1,a)==1);
    hle_call_guest(hle_resolve("Sleep"),1,&ms);
    CHECK(worker_count>suspended);
    a[0]=thread; a[1]=7;
    CHECK(hle_call_guest(hle_resolve("TerminateThread"),2,a)==1);
    CHECK(hle_wait(1,&thread,0,0)==0);
    int stopped=worker_count;
    hle_call_guest(hle_resolve("Sleep"),1,&ms);
    CHECK(worker_count==stopped);
    uint32_t *code=hle_alloc(4);
    a[0]=thread; a[1]=hle_va(code);
    CHECK(hle_call_guest(hle_resolve("GetExitCodeThread"),2,a)==1);
    CHECK(*code==7);
    hle_free(code); hle_handle_close(thread);
    uint32_t *message=hle_alloc(28);
    uint32_t blocked[6]={0,0,0x90000030,hle_va(message),0,0};
    thread=hle_call_guest(hle_resolve("CreateThread"),6,blocked);
    CHECK(thread!=0); hle_call_guest(hle_resolve("Sleep"),1,&ms);
    a[0]=thread; a[1]=9;
    CHECK(hle_call_guest(hle_resolve("TerminateThread"),2,a)==1);
    CHECK(hle_wait(1,&thread,0,0)==0); /* also stops an infinite GetMessage */
    hle_handle_close(thread); hle_free(message);
}
static uint32_t key_query(const char *name,uint32_t vk) {
    return hle_call_guest(hle_resolve(name),1,&vk);
}
static void key_message_test(void) {
    uint32_t *storage=hle_alloc(300),msg=hle_va(storage),keys=msg+32;
    uint32_t peek[5]={msg,0,0x100,0x101,1};
    hle_input_key(1,0x28,0x50);
    CHECK(hle_call_guest(hle_resolve("PeekMessageA"),5,peek)==1);
    CHECK(MEM32(msg+4)==0x100 && (MEM32(msg+12)&0x41000000)==0x01000000);
    CHECK((key_query("GetKeyState",0x28)&0x8001)==0x8000);
    CHECK(key_query("GetAsyncKeyState",0x28)==0x8001);
    CHECK(key_query("GetAsyncKeyState",0x28)==0x8000);
    hle_input_key(1,0x28,0x50); /* native auto-repeat */
    CHECK(hle_call_guest(hle_resolve("PeekMessageA"),5,peek)==1);
    CHECK((MEM32(msg+12)&0x41000000)==0x41000000);
    hle_input_key(0,0x28,0x50);
    CHECK(hle_call_guest(hle_resolve("PeekMessageA"),5,peek)==1);
    CHECK((MEM32(msg+12)&0xc1000000)==0xc1000000);
    CHECK(key_query("GetKeyState",0x28)==0);
    CHECK(key_query("GetAsyncKeyState",0x28)==0);
    CHECK(hle_call_guest(hle_resolve("GetKeyboardState"),1,&keys)==1);
    CHECK(MEM8(keys+0x28)==0);
    hle_input_key(1,0x14,0x3a); hle_input_key(1,0x14,0x3a); hle_input_key(0,0x14,0x3a);
    CHECK(key_query("GetKeyState",0x14)==1); /* only lock keys latch */
    hle_input_key(1,0x14,0x3a); hle_input_key(0,0x14,0x3a);
    CHECK(key_query("GetKeyState",0x14)==0);
    hle_input_mouse(0x201,4,4); hle_input_mouse(0x202,4,4);
    CHECK(key_query("GetKeyState",1)==0);
    CHECK(key_query("GetAsyncKeyState",1)==1);
    CHECK(key_query("GetAsyncKeyState",1)==0);
    while (hle_call_guest(hle_resolve("PeekMessageA"),5,peek)) {}
    hle_free(storage);
}
static void window_test(void) {
    uint32_t name=hle_guest_strdup("POD synthetic window"),*wc=hle_alloc(40);
    wc[1]=0x90000010; wc[9]=name;
    uint32_t p=hle_va(wc);
    CHECK(hle_call_guest(hle_resolve("RegisterClassA"),1,&p)!=0);
    uint32_t a[12]={0,name,name,0x10000000,0,0,640,480,0,0,0,0};
    uint32_t hwnd=hle_call_guest(hle_resolve("CreateWindowExA"),12,a);
    CHECK(hwnd!=0 && activated==1 && focused==1);
    CHECK(hle_focus_window()==hwnd);
    key_message_test();
    uint32_t cl[3]={hwnd,(uint32_t)-12,123};
    CHECK(hle_call_guest(hle_resolve("SetClassLongA"),3,cl)==0);
    cl[2]=456; CHECK(hle_call_guest(hle_resolve("SetClassLongA"),3,cl)==123);
    /* A fullscreen DirectDraw mode owns the virtual window geometry. */
    uint32_t *out=hle_alloc(20),ddargs[3]={0,hle_va(out),0};
    CHECK(hle_call_guest(hle_resolve("DirectDrawCreate"),3,ddargs)==0);
    uint32_t dd=*out,cooperative[3]={dd,hwnd,0x11};
    CHECK(hle_call_guest(MEM32(MEM32(dd)+20*4),3,cooperative)==0);
    uint32_t mode[4]={dd,800,600,16};
    CHECK(hle_call_guest(MEM32(MEM32(dd)+21*4),4,mode)==0);
    uint32_t rect[2]={hwnd,hle_va(out+1)};
    CHECK(hle_call_guest(hle_resolve("GetClientRect"),2,rect)==1);
    CHECK(out[3]==800 && out[4]==600);
    /* A flip chain must advance at the virtual display rate even without
     * an SDL window. The immediate-flip implementation fails this bound. */
    uint32_t desc=hle_va(hle_alloc(108));
    MEM32(desc)=108; MEM32(desc+4)=0x21; /* CAPS | BACKBUFFERCOUNT */
    MEM32(desc+20)=1; MEM32(desc+104)=0x218; /* PRIMARY | FLIP | COMPLEX */
    uint32_t create[4]={dd,desc,hle_va(out),0};
    CHECK(hle_call_guest(MEM32(MEM32(dd)+6*4),4,create)==0);
    uint32_t surface=*out,flip[3]={surface,0,0};
    uint32_t begun=hle_ticks_ms();
    for (unsigned i=0;i<12;i++) CHECK(method(surface,11,3,flip)==0);
    CHECK(hle_ticks_ms()-begun>=170); /* 12 retraces take about 200 ms */
    uint32_t release_surface[1]={surface};
    hle_call_guest(MEM32(MEM32(surface)+2*4),1,release_surface);
    hle_free(hle_ptr(desc));
    uint32_t release[1]={dd}; hle_call_guest(MEM32(MEM32(dd)+2*4),1,release);
    hle_free(out);
    hle_call_guest(hle_resolve("DestroyWindow"),1,&hwnd);
    hle_free(wc); hle_free(hle_ptr(name));
}
static void audio_test(void) {
    pod_audio_init(NULL);
    uint8_t *memory=hle_alloc(4096); uint32_t out=hle_va(memory),fmt=out+16,wh=out+64,data=out+128,pos=out+4000;
    MEM16(fmt)=1; MEM16(fmt+2)=1; MEM32(fmt+4)=8000;
    MEM32(fmt+8)=16000; MEM16(fmt+12)=2; MEM16(fmt+14)=16;
    uint32_t a[6]={out,0xffffffffu,fmt,0x90000020,0x1234,0x30001};
    CHECK(hle_call_guest(hle_resolve("waveOutOpen"),6,a)==0); CHECK(MEM32(out)==0);
    CHECK(audio_opened==0);
    a[5]=0x30000; CHECK(hle_call_guest(hle_resolve("waveOutOpen"),6,a)==0);
    CHECK(audio_opened==1);
    uint32_t wave=MEM32(out); CHECK(wave!=0);
    a[0]=wave; CHECK(hle_call_guest(hle_resolve("waveOutPause"),1,a)==0);
    MEM32(wh)=data; MEM32(wh+4)=3200;
    for (unsigned i=0;i<1600;i++) MEM16(data+i*2)=(i%16)<8?1000:(uint16_t)-1000;
    a[1]=wh; a[2]=32;
    CHECK(hle_call_guest(hle_resolve("waveOutPrepareHeader"),3,a)==0);
    CHECK(hle_call_guest(hle_resolve("waveOutWrite"),3,a)==0);
    SDL_Delay(30); pod_audio_pump();
    CHECK(audio_done==0);
    CHECK((MEM32(wh+16)&17)==16); /* paused: not completed at submission */
    CHECK(hle_call_guest(hle_resolve("waveOutUnprepareHeader"),3,a)==33);
    CHECK(hle_call_guest(hle_resolve("waveOutClose"),1,a)==33);
    CHECK(hle_call_guest(hle_resolve("waveOutRestart"),1,a)==0);
    uint32_t start=SDL_GetTicks();
    while (!(MEM32(wh+16)&1) && SDL_GetTicks()-start<2000) { SDL_Delay(5); pod_audio_pump(); }
    CHECK((MEM32(wh+16)&17)==1);
    CHECK(audio_done==1 && audio_header==wh);
    MEM32(pos)=4; a[1]=pos; a[2]=12;
    CHECK(hle_call_guest(hle_resolve("waveOutGetPosition"),3,a)==0); CHECK(MEM32(pos+4)==3200);
    MEM32(pos)=2; CHECK(hle_call_guest(hle_resolve("waveOutGetPosition"),3,a)==0); CHECK(MEM32(pos+4)==1600);
    MEM32(pos)=1; CHECK(hle_call_guest(hle_resolve("waveOutGetPosition"),3,a)==0); CHECK(MEM32(pos+4)==200);
    a[1]=wh; a[2]=32;
    CHECK(hle_call_guest(hle_resolve("waveOutWrite"),3,a)==0);
    CHECK(hle_call_guest(hle_resolve("waveOutReset"),1,a)==0);
    CHECK((MEM32(wh+16)&17)==1);
    CHECK(hle_call_guest(hle_resolve("waveOutUnprepareHeader"),3,a)==0);
    CHECK(hle_call_guest(hle_resolve("waveOutClose"),1,a)==0);
    CHECK(audio_done==2 && audio_closed==1);
    pod_audio_shutdown(); hle_free(memory); SDL_QuitSubSystem(SDL_INIT_AUDIO);
}
int main(void) {
    win32hle_register(pod_audio_shims);
    recomp_host_init(); win32hle_register(pod_dinput_shims);
    CPUID_impl(1,0,0,0);
    CHECK(g_eax==0x543 && (g_edx & (1u<<23)));
    CPUID_impl(0,0,0,0);
    CHECK(g_eax==1 && g_ebx==0x756e6547 && g_ecx==0x6c65746e && g_edx==0x49656e69);
    loader_test(); keyboard_test(); thread_test(); window_test(); audio_test();
    printf("POD portable selftest: %d failures\n",failures);
    return failures!=0;
}
