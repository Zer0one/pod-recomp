/* Synthetic windows only; verify sender/receiver thread ownership. */
#include <stdio.h>
#include "host.h"
#include "win32hle.h"
static int failures,processed,completed;
static uint32_t class_name,worker_window,ready,go,delivered,stop,main_tid,worker_tid;
#define CHECK(c) do { if (!(c)) { fprintf(stderr,"FAIL %d: %s\n",__LINE__,#c); failures++; } } while(0)
static uint32_t api(const char *name,int n,uint32_t *a) { return hle_call_guest(hle_resolve(name),n,a); }
static uint32_t window(uint32_t parent) {
    uint32_t a[12]={0,class_name,class_name,parent?0x40000000u:0,0,0,100,100,parent,0,0,0};
    return api("CreateWindowExA",12,a);
}
static void proc(void) {
    if (A32(1)==0x81) RET(1,4);
    if (A32(1)==0x400) {
        CHECK(hle_thread_id()==(A32(0)==worker_window?worker_tid:main_tid));
        CHECK(A32(2)==12 && A32(3)==34); processed++; RET(0x12345678,4);
    }
    RET(0,4);
}
static void callback(void) {
    CHECK(hle_thread_id()==main_tid); CHECK(A32(1)==0x400 && A32(2)==0xabc && A32(3)==0x12345678);
    completed++; RET(0,4);
}
static void worker(void) {
    worker_tid=hle_thread_id(); worker_window=window(0); CHECK(worker_window!=0);
    api("SetEvent",1,&ready); uint32_t w[2]={go,2000}; CHECK(api("WaitForSingleObject",2,w)==0);
    uint32_t msg=hle_va(hle_alloc(28)),peek[5]={msg,0,0x400,0x400,1};
    api("PeekMessageA",5,peek); /* dispatches the sent message without posting it */
    api("SetEvent",1,&delivered); w[0]=stop; CHECK(api("WaitForSingleObject",2,w)==0);
    api("DestroyWindow",1,&worker_window); hle_free(hle_ptr(msg)); RET(0,1);
}
const recomp_dispatch_entry_t recomp_dispatch_table[]={
    {0x90000000,worker},{0x90000010,proc},{0x90000020,callback}};
const uint32_t recomp_dispatch_count=3;
static uint32_t event(void) { uint32_t a[4]={0,0,0,0}; return api("CreateEventA",4,a); }
int main(void) {
    recomp_host_init(); main_tid=hle_thread_id();
    class_name=hle_guest_strdup("Send callback synthetic"); uint32_t *wc=hle_alloc(40);
    wc[1]=0x90000010; wc[9]=class_name; uint32_t p=hle_va(wc); CHECK(api("RegisterClassA",1,&p)!=0);
    uint32_t first=window(0),second=window(0),child=window(first); CHECK(first && second && child);
    uint32_t a[6]={first,0x400,12,34,0x90000020,0xabc};
    CHECK(api("SendMessageCallbackA",6,a)==1); CHECK(processed==1 && completed==1);
    a[4]=0; CHECK(api("SendMessageCallbackA",6,a)==1); CHECK(processed==2 && completed==1);
    a[0]=0xffff; a[4]=0x90000020;
    CHECK(api("SendMessageCallbackA",6,a)==1); CHECK(processed==4 && completed==3); /* excludes child */
    a[0]=0xdead; CHECK(api("SendMessageCallbackA",6,a)==0);
    CHECK(api("GetLastError",0,NULL)==1400);
    ready=event(); go=event(); delivered=event(); stop=event();
    uint32_t create[6]={0,0,0x90000000,0,0,0},thread=api("CreateThread",6,create),wait[2]={ready,2000}; CHECK(thread!=0);
    CHECK(api("WaitForSingleObject",2,wait)==0);
    a[0]=worker_window; CHECK(api("SendMessageCallbackA",6,a)==1);
    CHECK(processed==4 && completed==3); /* receiver is blocked: send must return */
    api("SetEvent",1,&go); wait[0]=delivered; CHECK(api("WaitForSingleObject",2,wait)==0);
    CHECK(processed==5 && completed==3); /* completion waits for sender's message polling */
    uint32_t msg=hle_va(hle_alloc(28)),peek[5]={msg,0,0x400,0x400,1}; api("PeekMessageA",5,peek);
    CHECK(completed==4); api("SetEvent",1,&stop); wait[0]=thread; CHECK(api("WaitForSingleObject",2,wait)==0);
    api("DestroyWindow",1,&child); api("DestroyWindow",1,&first); api("DestroyWindow",1,&second);
    uint32_t handles[]={thread,ready,go,delivered,stop};
    for (unsigned i=0;i<5;i++) api("CloseHandle",1,&handles[i]);
    hle_free(hle_ptr(msg)); hle_free(wc); hle_free(hle_ptr(class_name));
    printf("SendMessageCallback: %d failures\n",failures); return failures!=0;
}
