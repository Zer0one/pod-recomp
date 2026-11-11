/* Synthetic two-thread quit delivery: a mixer polling PeekMessage must not
 * consume the UI thread's quit request. No game data or native window. */
#include <stdio.h>
#include "host.h"
#include "win32hle.h"
static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr,"FAIL %d: %s\n",__LINE__,#c); failures++; } } while(0)
static uint32_t api(const char *name,int n,uint32_t *a) { return hle_call_guest(hle_resolve(name),n,a); }
static void worker(void) {
    uint32_t msg=hle_va(hle_alloc(28)),a[5]={msg,0,0,0,0};
    CHECK(api("PeekMessageA",5,a)==0); /* parent's quit is private */
    a[2]=0x100; a[3]=0x109;
    uint32_t code=77; api("PostQuitMessage",1,&code);
    CHECK(api("PeekMessageA",5,a)==1); CHECK(MEM32(msg+4)==0x12 && MEM32(msg+8)==77);
    CHECK(api("PeekMessageA",5,a)==1); /* PM_NOREMOVE retains pending quit */
    a[4]=1; CHECK(api("PeekMessageA",5,a)==1);
    CHECK(api("PeekMessageA",5,a)==0);
    hle_free(hle_ptr(msg)); RET(0,1);
}
const recomp_dispatch_entry_t recomp_dispatch_table[]={{0x90000000,worker}};
const uint32_t recomp_dispatch_count=1;
int main(void) {
    recomp_host_init();
    uint32_t code=99; api("PostQuitMessage",1,&code);
    uint32_t a[6]={0,0,0x90000000,0,0,0};
    uint32_t thread=api("CreateThread",6,a); CHECK(thread!=0);
    uint32_t wait[2]={thread,2000}; CHECK(api("WaitForSingleObject",2,wait)==0);
    uint32_t msg=hle_va(hle_alloc(28)),get[4]={msg,0,0x100,0x109};
    CHECK(api("GetMessageA",4,get)==0); CHECK(MEM32(msg+4)==0x12 && MEM32(msg+8)==99);
    api("CloseHandle",1,&thread); hle_free(hle_ptr(msg));
    printf("Thread quit messages: %d failures\n",failures); return failures!=0;
}
