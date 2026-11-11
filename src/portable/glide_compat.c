/* POD Gold's Glide cleanup differs from PODMMX in two places:
 * 004A3530 destroys Glide from an active render iteration; 004A82B4 holds
 * the render semaphore while joining audio, which needs that semaphore.
 * Keep this executable-specific policy outside the Glide service and HLE.
 * All state is accessed under the HLE guest machine, including after waits. */
#include <stdio.h>
#include <string.h>
#include "glide_compat.h"
#include "glide_bootstrap.h"

#define RENDER_SEMAPHORE 0x006DC504u
#define RENDER_THREAD    0x006DC520u
#define MAIN_WINDOW      0x006DC510u
#define CLOSE_REQUEST    0x006DC538u
#define WINDOW_PROC      0x004A8754u
static uint32_t wait_base,release_base,terminate_base,shutdown_base;
static uint32_t register_class_base;
static uint32_t semaphore_owner,deferred_renderer,deferred_thread;

static void window_proc(void) {
    uint32_t args[4]={A32(0),A32(1),A32(2),A32(3)};
    /* WM_CLOSE at 004A9F86 waits for S before publishing this flag. The
     * nested menu loop at 0042DA31 can retain S until it sees that flag.
     * Publish the actual close request first, under the guest machine; then
     * let the original procedure acquire S and perform normal destruction. */
    if (args[0]==MEM32(MAIN_WINDOW) && args[1]==0x10u)
        MEM32(CLOSE_REQUEST)=1;
    uint32_t result=hle_call_guest(WINDOW_PROC,4,args);
    RET(result,4);
}
static void register_class(void) {
    uint32_t cls=A32(0),result;
    if (cls && MEM32(cls+4)==WINDOW_PROC) {
        uint32_t mark=hle_scratch_mark(),copy=hle_scratch(40);
        memcpy(hle_ptr(copy),hle_ptr(cls),40);
        MEM32(copy+4)=hle_resolve("POD::GlideWindowProc");
        result=hle_call_guest(register_class_base,1,&copy);
        hle_scratch_release(mark);
    } else result=hle_call_guest(register_class_base,1,&cls);
    RET(result,1);
}
static void get_clip_cursor(void) {
    /* The current HLE ClipCursor does not constrain the host pointer. POD's
     * focus handler saves/restores this rectangle; report the unconstrained
     * virtual desktop, not the physical macOS monitor coordinates. */
    uint32_t rect=A32(0);
    if (!rect) {hle_set_last_error(87);RET(0,1);}
    int width,height,bpp;hle_dd_mode(&width,&height,&bpp);
    MEM32(rect)=MEM32(rect+4)=0;
    MEM32(rect+8)=width;MEM32(rect+12)=height;
    RET(1,1);
}
static void wait_one(void) {
    uint32_t args[2]={A32(0),A32(1)};
    if (deferred_renderer && hle_thread_id()==deferred_thread &&
        args[0] && args[0]==MEM32(RENDER_SEMAPHORE)) {
        /* Quit has already queued WM_DESTROY. At the next render-section
         * boundary, leave S available to cleanup instead of reacquiring it
         * before the polling waiter can run. TerminateThread's cooperative
         * checkpoint ends this worker; no GPU call resumes after that. */
        fprintf(stderr,"[pod-glide] renderer yielding rendering semaphore for cleanup\n");
        uint32_t delay=1;
        for (;;) hle_call_guest(hle_resolve("Sleep"),1,&delay);
    }
    uint32_t result=hle_call_guest(wait_base,2,args);
    if (!result && args[0] && args[0]==MEM32(RENDER_SEMAPHORE))
        semaphore_owner=hle_thread_id();
    RET(result,2);
}
static void release(void) {
    uint32_t args[3]={A32(0),A32(1),A32(2)};
    uint32_t result=hle_call_guest(release_base,3,args);
    if (result && args[0] && args[0]==MEM32(RENDER_SEMAPHORE))
        semaphore_owner=0;
    RET(result,3);
}
static void shutdown_glide(void) {
    uint32_t renderer=MEM32(RENDER_THREAD);
    if (renderer && hle_handle_obj(renderer,HLE_H_THREAD) && !hle_thread_done(renderer)) {
        deferred_renderer=renderer;
        deferred_thread=hle_thread_id();
        fprintf(stderr,"[pod-glide] defer graphics shutdown until renderer stops\n");
        RET(0,0);
    }
    deferred_renderer=deferred_thread=0;
    hle_call_guest(shutdown_base,0,NULL);
    RET(0,0);
}
static void terminate(void) {
    uint32_t args[2]={A32(0),A32(1)};
    uint32_t renderer=MEM32(RENDER_THREAD);
    uint32_t result=hle_call_guest(terminate_base,2,args);
    if (result && renderer && args[0]==renderer && hle_thread_done(renderer)) {
        /* The renderer can no longer resume into either the GPU or the
         * semaphore-protected section. Audio must be free to finish its
         * current buffer before it can read MSG_DesInitSnd and acknowledge. */
        if (semaphore_owner==hle_thread_id()) {
            uint32_t unlock[3]={MEM32(RENDER_SEMAPHORE),1,0};
            if (!hle_call_guest(release_base,3,unlock))
                hle_fatal("POD Glide cleanup could not release rendering semaphore");
            semaphore_owner=0;
            fprintf(stderr,"[pod-glide] released rendering semaphore before audio shutdown\n");
        }
        /* Main cleanup still selects the board before its final WinClose /
         * Shutdown. Keep graphics alive until that original shutdown call. */
    }
    RET(result,2);
}
static const win32hle_shim overrides[]={
    {"RegisterClassA",register_class},{"POD::GlideWindowProc",window_proc},
    {"GetClipCursor",get_clip_cursor},
    {"WaitForSingleObject",wait_one},{"ReleaseSemaphore",release},
    {"TerminateThread",terminate},{"_grGlideShutdown@0",shutdown_glide},{NULL,NULL}
};
void pod_glide_compat_register(void) {
    semaphore_owner=deferred_renderer=deferred_thread=0;
    win32hle_register(overrides);
}
static uint32_t delegate(const win32hle_shim *table,const char *name,
                         const char *alias,win32hle_shim storage[2]) {
    for (const win32hle_shim *s=table;s->name;s++) if (!strcmp(s->name,name)) {
        storage[0].name=alias;storage[0].fn=s->fn;
        win32hle_register(storage);
        return hle_resolve(alias);
    }
    hle_fatal("Missing POD Glide lifecycle delegate: %s",name);
    return 0;
}
void pod_glide_compat_bind(void) {
    static win32hle_shim waits[2],releases[2],terminates[2],shutdowns[2],classes[2];
    register_class_base=delegate(win32hle_user32,"RegisterClassA","POD::GlideRegisterClassBase",classes);
    wait_base=delegate(win32hle_kernel32,"WaitForSingleObject","POD::GlideWaitBase",waits);
    release_base=delegate(win32hle_kernel32,"ReleaseSemaphore","POD::GlideReleaseBase",releases);
    terminate_base=delegate(win32hle_kernel32_crt,"TerminateThread","POD::GlideTerminateBase",terminates);
    shutdown_base=delegate(pod_glide_bootstrap_shims,"_grGlideShutdown@0","POD::GlideShutdownBase",shutdowns);
}
