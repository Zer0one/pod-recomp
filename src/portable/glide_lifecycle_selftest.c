/* Exercise POD's actual stop/ack dependency with synthetic guest workers.
 * No game assets, GPU, SDL window or host process termination required. */
#include <stdio.h>
#include "host.h"
#include "glide_bootstrap.h"
#include "glide_compat.h"
#define SEM 0x006DC504u
#define RENDERER 0x006DC520u
#define MAIN_WINDOW 0x006DC510u
#define CLOSE_REQUEST 0x006DC538u
static int failures,shutdowns;
static uint32_t ready,ack,audio_tid,stop_message;
#define CHECK(c) do {if(!(c)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#c);failures++;}}while(0)
static uint32_t api(const char *name,unsigned n,uint32_t *a) {
    return hle_call_guest(hle_resolve(name),n,a);
}
static void fake_shutdown(void) {
    CHECK(!MEM32(RENDERER)||hle_thread_done(MEM32(RENDERER)));
    shutdowns++;RET(0,0);
}
const win32hle_shim pod_glide_bootstrap_shims[]={
    {"_grGlideShutdown@0",fake_shutdown},{NULL,NULL}
};
static void renderer(void) {
    api("_grGlideShutdown@0",0,NULL);
    CHECK(!shutdowns); /* the iteration can still use live graphics resources */
    api("SetEvent",1,&ready);
    uint32_t wait[2]={MEM32(SEM),2000};
    api("WaitForSingleObject",2,wait);
    CHECK(0);RET(0,1); /* must park until cleanup terminates this renderer */
}
static void idle_renderer(void) {
    api("SetEvent",1,&ready);
    uint32_t delay=1;
    for (;;) api("Sleep",1,&delay);
}
static void audio(void) {
    api("SetEvent",1,&ready);
    uint32_t wait[2]={MEM32(SEM),2000};
    CHECK(api("WaitForSingleObject",2,wait)==0);
    uint32_t release[3]={MEM32(SEM),1,0};CHECK(api("ReleaseSemaphore",3,release)==1);
    uint32_t msg=hle_va(hle_alloc(28)),get[4]={msg,0,0,0};
    CHECK(api("GetMessageA",4,get)==1);
    CHECK(MEM32(msg+4)==stop_message && MEM32(msg+8)==ack);
    api("SetEvent",1,&ack);hle_free(hle_ptr(msg));RET(0,1);
}
static void menu_renderer(void) {
    uint32_t wait[2]={MEM32(SEM),2000};
    CHECK(api("WaitForSingleObject",2,wait)==0);
    api("SetEvent",1,&ready);
    uint32_t delay=1;
    while (!MEM32(CLOSE_REQUEST)) api("Sleep",1,&delay);
    uint32_t release[3]={MEM32(SEM),1,0};CHECK(api("ReleaseSemaphore",3,release)==1);
    RET(0,1);
}
static void original_window_proc(void) {
    if (A32(1)==0x81) RET(1,4);
    if (A32(1)==0x400) {
        CHECK(!MEM32(CLOSE_REQUEST));CHECK(A32(2)==12 && A32(3)==34);RET(123,4);
    }
    if (A32(1)==0x10) {
        CHECK(MEM32(CLOSE_REQUEST)==1);
        /* The original WM_CLOSE cannot acquire S until the menu sees the
         * close flag and releases it. No forced semaphore release here. */
        uint32_t wait[2]={MEM32(SEM),2000};
        CHECK(api("WaitForSingleObject",2,wait)==0);
        uint32_t release[3]={MEM32(SEM),1,0};CHECK(api("ReleaseSemaphore",3,release)==1);
        RET(456,4);
    }
    RET(0,4);
}
const recomp_dispatch_entry_t recomp_dispatch_table[]={
    {0x004A8754,original_window_proc},
    {0x90000000,renderer},{0x90000010,idle_renderer},{0x90000020,audio},
    {0x90000030,menu_renderer}
};
const uint32_t recomp_dispatch_count=5;
static uint32_t event(void) {uint32_t a[4]={0,0,0,0};return api("CreateEventA",4,a);}
static uint32_t thread(uint32_t start,uint32_t *tid) {
    uint32_t a[6]={0,0,start,0,0,tid?hle_va(tid):0};return api("CreateThread",6,a);
}
static void wait_handle(uint32_t handle) {
    uint32_t a[2]={handle,2000};CHECK(api("WaitForSingleObject",2,a)==0);
}
static void stop(uint32_t handle) {uint32_t a[2]={handle,0};CHECK(api("TerminateThread",2,a)==1);}
static void close(uint32_t handle) {api("CloseHandle",1,&handle);}
int main(void) {
    pod_glide_compat_register();win32hle_register(pod_glide_bootstrap_shims);
    recomp_host_init();pod_glide_compat_bind();
    CHECK(hle_vm_reserve(0x006D0000u,0x10000u)==0x006D0000u);
    uint32_t semargs[4]={0,1,1,0};MEM32(SEM)=api("CreateSemaphoreA",4,semargs);
    CHECK(MEM32(SEM)!=0);ready=event();ack=event();
    uint32_t name=hle_guest_strdup("MSG_DesInitSnd");
    stop_message=api("RegisterWindowMessageA",1,&name);
    uint32_t *tid=hle_alloc(4);
    /* No renderer: normal Glide shutdown is immediate. */
    api("_grGlideShutdown@0",0,NULL);CHECK(shutdowns==1);shutdowns=0;
    MEM32(RENDERER)=thread(0x90000000,NULL);wait_handle(ready);
    wait_handle(MEM32(SEM)); /* same acquisition as 004A82B4 */
    uint32_t sound=thread(0x90000020,tid);audio_tid=*tid;wait_handle(ready);
    uint32_t post[4]={audio_tid,stop_message,ack,0};
    CHECK(api("PostThreadMessageA",4,post)==1);
    stop(MEM32(RENDERER));CHECK(shutdowns==0);
    wait_handle(ack);wait_handle(sound); /* real receiver acknowledges, no forced event */
    /* Main may still use graphics during cleanup, then owns final shutdown. */
    CHECK(shutdowns==0);api("_grGlideShutdown@0",0,NULL);CHECK(shutdowns==1);
    stop(MEM32(RENDERER));CHECK(shutdowns==1); /* no double release/destruction */
    uint32_t poll[2]={MEM32(SEM),0};CHECK(api("WaitForSingleObject",2,poll)==0);
    CHECK(api("WaitForSingleObject",2,poll)==258); /* still binary */
    close(MEM32(RENDERER));close(sound);
    /* Terminating an unrelated thread must not unlock the rendering section. */
    MEM32(RENDERER)=thread(0x90000010,NULL);wait_handle(ready);
    uint32_t unrelated=thread(0x90000010,NULL);wait_handle(ready);stop(unrelated);
    CHECK(api("WaitForSingleObject",2,poll)==258);close(unrelated);
    stop(MEM32(RENDERER)); /* actual renderer releases owner's semaphore */
    CHECK(api("WaitForSingleObject",2,poll)==0);close(MEM32(RENDERER));
    /* Failed termination must neither unlock nor claim completed teardown. */
    MEM32(RENDERER)=0xdead;
    uint32_t invalid[2]={0xdead,0};CHECK(api("TerminateThread",2,invalid)==0);
    CHECK(api("WaitForSingleObject",2,poll)==258);
    close(MEM32(SEM));MEM32(RENDERER)=0;
    /* Return-to-menu retains S across its loop: exercise the real WM_CLOSE
     * dependency using the original procedure address, without game assets. */
    MEM32(SEM)=api("CreateSemaphoreA",4,semargs);
    MEM32(CLOSE_REQUEST)=0;
    uint32_t name_class=hle_guest_strdup("POD Glide close ordering");
    uint32_t *wc=hle_alloc(40);wc[1]=0x004A8754;wc[9]=name_class;
    uint32_t cls=hle_va(wc);CHECK(api("RegisterClassA",1,&cls)!=0);
    CHECK(wc[1]==0x004A8754); /* registration does not edit guest class data */
    uint32_t create[12]={0,name_class,name_class,0,0,0,100,100,0,0,0,0};
    MEM32(MAIN_WINDOW)=api("CreateWindowExA",12,create);CHECK(MEM32(MAIN_WINDOW)!=0);
    uint32_t send[4]={MEM32(MAIN_WINDOW),0x400,12,34};CHECK(api("SendMessageA",4,send)==123);
    MEM32(RENDERER)=thread(0x90000030,NULL);wait_handle(ready);
    send[1]=0x10;CHECK(api("SendMessageA",4,send)==456);
    wait_handle(MEM32(RENDERER));
    uint32_t hwnd=MEM32(MAIN_WINDOW);api("DestroyWindow",1,&hwnd);
    close(MEM32(RENDERER));close(MEM32(SEM));
    MEM32(SEM)=MEM32(RENDERER)=MEM32(MAIN_WINDOW)=0;
    hle_free(wc);hle_free(hle_ptr(name_class));
    api("_grGlideShutdown@0",0,NULL);CHECK(shutdowns==2);
    close(ready);close(ack);hle_free(tid);hle_free(hle_ptr(name));
    printf("POD Glide shutdown lifecycle: %d failures\n",failures);
    return failures!=0;
}
