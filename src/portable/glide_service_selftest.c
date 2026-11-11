/* Asset-free guest ABI/LFB ownership tests against the real native backend. */
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <stdatomic.h>
#include <SDL2/SDL.h>
#include "host.h"
#include "glide_bootstrap.h"
#include "glide_gpu.h"
const recomp_dispatch_entry_t recomp_dispatch_table[]={{0,NULL}};
const uint32_t recomp_dispatch_count=0;
static int failures;
#define CHECK(c) do {if(!(c)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#c);failures++;}}while(0)
static uint32_t call(const char *name,unsigned count,uint32_t *args) {
    uint32_t address=hle_resolve(name);
    if(!address){fprintf(stderr,"Missing test shim: %s\n",name);failures++;return 0;}
    return hle_call_guest(address,count,args);
}
static uint32_t info;
static uint16_t *lock(unsigned write,unsigned buffer) {
    memset(hle_ptr(info),0,20);MEM32(info)=20;
    uint32_t args[6]={write,buffer,0,0,0,info};
    CHECK(call("_grLfbLock@24",6,args)==1);
    CHECK(MEM32(info+8)==640*2);
    CHECK(MEM32(info+12)==0&&MEM32(info+16)==0);
    return hle_ptr(MEM32(info+4));
}
static void unlock(unsigned write,unsigned buffer) {
    uint32_t args[2]={write,buffer};CHECK(call("_grLfbUnlock@8",2,args)==1);
}
static atomic_int swap_started,swap_done;
static void *pending_swap(void *unused) {
    (void)unused;
    uint32_t interval=4;
    atomic_store(&swap_started,1);
    call("_grBufferSwap@4",1,&interval);
    atomic_store(&swap_done,1);
    return NULL;
}
static void swap_lifecycle(unsigned reopen) {
    pthread_t worker;
    atomic_store(&swap_started,0);atomic_store(&swap_done,0);
    int error=pthread_create(&worker,NULL,pending_swap,NULL);
    CHECK(!error);if(error)return;
    while(!atomic_load(&swap_started))SDL_Delay(1);
    /* The swap waits four refresh intervals. Acquire the guest machine while
     * it is sleeping, then hold it across the entire replacement operation. */
    SDL_Delay(5);mach_enter();
    CHECK(!atomic_load(&swap_done));
    if(reopen) {
        call("_grSstWinClose@0",0,NULL);
        uint32_t mode[7]={0,3,0,0,0,2,1};
        CHECK(call("_grSstWinOpen@28",7,mode)==1);
        uint32_t green[3]={0x00ff00,255,65535};call("_grBufferClear@12",3,green);
    } else call("_grGlideShutdown@0",0,NULL);
    mach_leave();pthread_join(worker,NULL);
    CHECK(atomic_load(&swap_done));
    if(reopen) {
        uint32_t result=hle_va(hle_alloc(20));MEM32(result)=20;
        uint32_t lock_args[6]={0,0,0,0,0,result};
        CHECK(call("_grLfbLock@24",6,lock_args)==1);
        /* A stale swap would expose the new back buffer's green clear. */
        CHECK(MEM16(MEM32(result+4))==0);
        unlock(0,0);hle_free(hle_ptr(result));
    } else CHECK(call("_grSstScreenWidth@0",0,NULL)==0);
}
int main(void) {
    char error[192];pod_glide_gpu *preflight=pod_glide_gpu_open(1,1,error,sizeof error);
    if(!preflight){fprintf(stderr,"SKIP: %s\n",error);return 77;}
    pod_glide_gpu_close(preflight);
    win32hle_register(pod_glide_bootstrap_shims);recomp_host_init();
    call("_grGlideInit@0",0,NULL);
    uint32_t args[8]={0,7,0,0,0,2,1,0};
    CHECK(call("_grSstWinOpen@28",7,args)==1);
    info=hle_va(hle_alloc(20));
    args[0]=0x112233;args[1]=255;args[2]=65535;
    call("_grBufferClear@12",3,args);
    uint16_t *pixels=lock(0,1);
    CHECK(pixels[0]==0x1106&&pixels[640*480-1]==0x1106);
    /* Read lock edits are discarded, write lock edits reach exactly its buffer. */
    pixels[0]=0xf800;unlock(0,1);
    pixels=lock(0,1);CHECK(pixels[0]==0x1106);unlock(0,1);
    pixels=lock(1,1);pixels[0]=0xf800;unlock(1,1);
    pixels=lock(0,0);CHECK(pixels[0]==0);unlock(0,0);
    args[0]=0;call("_grBufferSwap@4",1,args);
    pixels=lock(0,0);CHECK(pixels[0]==0xf800&&pixels[1]==0x1106);unlock(0,0);
    uint16_t *region=hle_alloc(4);region[0]=0x07e0;region[1]=0x001f;
    uint32_t rect[8]={1,1,1,0,2,1,4,hle_va(region)};
    CHECK(call("_grLfbWriteRegion@32",8,rect)==1);
    pixels=lock(0,1);CHECK(pixels[641]==0x07e0&&pixels[642]==0x001f&&pixels[0]==0);unlock(0,1);
    /* Invalid lock/unlock/rectangle must fail without corrupting another lock. */
    uint32_t wrong[6]={1,2,0,0,0,info};CHECK(call("_grLfbLock@24",6,wrong)==0);
    pixels=lock(1,1);pixels[0]=0xffff;
    wrong[0]=0;wrong[1]=1;CHECK(call("_grLfbUnlock@8",2,wrong)==0);
    unlock(1,1);
    rect[1]=640;CHECK(call("_grLfbWriteRegion@32",8,rect)==0);
    /* Palette and TMU uploads invalidate cached native textures even when
     * source address/LOD stay unchanged. All data is synthetic guest memory. */
    uint32_t *pal=hle_alloc(1024),*texinfo=hle_alloc(20);
    uint8_t *indices=hle_alloc(1);
    memset(pal,0,1024);pal[0]=0xff0000;pal[1]=0x0000ff;*indices=0;
    texinfo[0]=texinfo[1]=8;texinfo[2]=3;texinfo[3]=5;texinfo[4]=hle_va(indices);
    uint32_t table[3]={0,2,hle_va(pal)},download[4]={0,0,3,hle_va(texinfo)};
    call("_grTexDownloadTable@12",3,table);
    call("_grTexDownloadMipMap@16",4,download);
    call("_grTexSource@16",4,download);
    args[0]=4;call("_guColorCombineFunction@4",1,args);
    args[0]=2;call("_guAlphaSource@4",1,args);
    float *vertices=hle_alloc(3*12*sizeof(float));memset(vertices,0,3*12*sizeof(float));
    vertices[0]=1;vertices[1]=1;vertices[12]=31;vertices[13]=1;
    vertices[24]=1;vertices[25]=31;
    for(unsigned i=0;i<3;i++)vertices[i*12+8]=1;
    uint32_t triangle[3]={hle_va(vertices),hle_va(vertices+12),hle_va(vertices+24)};
    const uint16_t expected[3]={0xf800,0x07e0,0x001f};
    for(unsigned pass=0;pass<3;pass++) {
        if(pass==1){pal[0]=0x00ff00;call("_grTexDownloadTable@12",3,table);}
        if(pass==2){*indices=1;call("_grTexDownloadMipMap@16",4,download);}
        args[0]=0;args[1]=255;args[2]=65535;call("_grBufferClear@12",3,args);
        call("_grDrawTriangle@12",3,triangle);
        args[0]=0;call("_grBufferSwap@4",1,args);
        pixels=lock(0,0);CHECK(pixels[8*640+8]==expected[pass]);unlock(0,0);
    }
    hle_free(vertices);hle_free(indices);hle_free(texinfo);hle_free(pal);
    hle_free(region);hle_free(hle_ptr(info));
    call("_grSstWinClose@0",0,NULL);
    /* Reopening a different mode must reset dimensions and guest LFB ownership. */
    uint32_t mode[7]={0,3,0,0,0,2,1};
    CHECK(call("_grSstWinOpen@28",7,mode)==1);
    CHECK(call("_grSstScreenWidth@0",0,NULL)==512);
    CHECK(call("_grSstScreenHeight@0",0,NULL)==384);
    call("_grSstWinClose@0",0,NULL);
    call("_grGlideShutdown@0",0,NULL);
    /* Late activation notifications succeed without recreating a surface. */
    args[0]=1;CHECK(call("_grSstControl@4",1,args)==1);
    CHECK(call("_grSstScreenWidth@0",0,NULL)==0);
    CHECK(call("_grSstWinOpen@28",7,mode)==0);
    call("_grGlideInit@0",0,NULL);
    CHECK(call("_grSstWinOpen@28",7,mode)==1);
    args[0]=0x00ff00;args[1]=255;args[2]=65535;
    call("_grBufferClear@12",3,args);
    info=hle_va(hle_alloc(20));MEM32(info)=20;
    uint32_t reopened_lock[6]={0,1,0,0,0,info};
    CHECK(call("_grLfbLock@24",6,reopened_lock)==1);
    CHECK(MEM32(info+8)==512*2);
    CHECK(MEM16(MEM32(info+4))==0x07e0);
    unlock(0,1);hle_free(hle_ptr(info));
    swap_lifecycle(1);
    swap_lifecycle(0);
    SDL_Quit();fprintf(stderr,"Glide guest ABI/LFB lifecycle: %s\n",failures?"FAIL":"PASS");
    return failures!=0;
}
