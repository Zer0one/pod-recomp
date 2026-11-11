/* POD-specific Windows policy, shared by frontend hosts. No CLI or SDL window
 * ownership here. Register before recomp_host_init; bind delegates after it. */
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include "compat.h"
#include "codec.h"
#include "video_present.h"
static uint32_t profile_string,ddraw_base;
static uint32_t device_caps_base;
static unsigned desktop_bits;
static void device_caps(void) {
    if (A32(1)==12) RET(desktop_bits,2); /* BITSPIXEL, not the host's display */
    recomp_lookup_import(device_caps_base)();
}
/* The data folder stands for the CD; the writable overlay holds Ubi.ini
 * and saves. This is POD's existing host policy, without a real drive mount. */
static void windows_dir(void) {
    if (A32(1)>=4) { memcpy(APTR(0),"C:\\",4); RET(3,2); }
    RET(4,2);
}
static void system_dir(void) {
    const char path[]="C:\\INSTALL";
    if (A32(1)>strlen(path)) { memcpy(APTR(0),path,sizeof path); RET(sizeof path-1,2); }
    RET(sizeof path,2);
}
static int is_cd(const char *s) { return s && (s[0]=='D'||s[0]=='d') && s[1]==':'; }
static void drive_type(void) { RET(is_cd(ASTR(0)) ? 5 : ASTR(0) && (ASTR(0)[0]=='C'||ASTR(0)[0]=='c') ? 3 : 1,1); }
static void volume_info(void) {
    if (ASTR(1) && A32(2)) snprintf(ASTR(1),A32(2),"%s",is_cd(ASTR(0)) ? "POD2_0" : "POD");
    if (A32(3)) MEM32(A32(3))=0x504f4432;
    if (A32(4)) MEM32(A32(4))=255;
    if (A32(5)) MEM32(A32(5))=0;
    if (ASTR(6) && A32(7)) snprintf(ASTR(6),A32(7),"CDFS");
    RET(1,8);
}
static void ini_string(void) {
    if (win32hle_trace) fprintf(stderr,"[ini] %s / %s (%s)\n",ASTR(0)?ASTR(0):"",ASTR(1)?ASTR(1):"",ASTR(5)?ASTR(5):"");
    if (ASTR(0) && ASTR(1) && !strcasecmp(ASTR(0),"Drivers32") && !strcasecmp(ASTR(1),"VIDC.IV41")) {
        /* VfW resolves this name through the optional lifted codec service. */
        if (A32(4)) snprintf(ASTR(3),A32(4),"ir41_32.dll");
        RET(A32(4) ? (uint32_t)strlen(ASTR(3)) : 0,6);
    }
    recomp_lookup_import(profile_string)();
}
static void avi_open(void) {
    fprintf(stderr,"[avi] explicitly skipped in native prototype: %s\n",ASTR(1));
    if (A32(0)) MEM32(A32(0))=0;
    RET(0x80044067u,4); /* AVIERR_FILEOPEN, matching Windows host --no-codec */
}
static void message_box(void) {
    fprintf(stderr,"[messagebox] %s: %s\n",ASTR(2)?ASTR(2):"",ASTR(1)?ASTR(1):""); RET(1,4);
}
/* POD's Watcom CRT probes the Unicode API and falls back to ANSI on Win95.
 * Match the HLE's existing GetEnvironmentStringsW policy. */
static void no_unicode3(void) { hle_set_last_error(120); RET(0,3); }
static void no_unicode2(void) { hle_set_last_error(120); RET(0,2); }
static void no_unicode0(void) { hle_set_last_error(120); RET(0,0); }
static void upper_buffer(void) {
    uint8_t *p=APTR(0); uint32_t n=A32(1);
    if (!p) RET(0,2);
    for (uint32_t i=0;i<n;i++) {
        unsigned c=p[i];
        if ((c>='a' && c<='z') || (c>=0xe0 && c<=0xfe && c!=0xf7)) c-=32;
        else if (c==0xff) c=0x9f;
        else if (c==0x9a || c==0x9c || c==0x9e) c-=16;
        p[i]=(uint8_t)c;
    }
    RET(n,2);
}
static void avi_init(void) { RET(0,0); }
static void window_dc(void) { RET(hle_gdi_window_dc(),1); }
/* Match POD's established Windows host: this title's mode table was never
 * meant for the generic HLE's dozens of modern display modes. */
static void enum_modes(void) {
    uint32_t cb=A32(4),ctx=A32(3),mark=hle_scratch_mark(),d=hle_scratch(108);
    for (unsigned bpp=8;bpp<=16;bpp+=8) {
        memset(hle_ptr(d),0,108);
        MEM32(d)=108; MEM32(d+4)=0x100e;
        MEM32(d+8)=480; MEM32(d+12)=640; MEM32(d+16)=640*bpp/8;
        MEM32(d+72)=32; MEM32(d+76)=0x40|(bpp==8?0x20:0);
        MEM32(d+84)=bpp;
        if (bpp==16) { MEM32(d+88)=0xf800; MEM32(d+92)=0x7e0; MEM32(d+96)=0x1f; }
        uint32_t a[2]={d,ctx};
        if (!hle_call_guest(cb,2,a)) break;
    }
    hle_scratch_release(mark); RET(0,5);
}
static void patch_ddraw(uint32_t obj) {
    if (!obj) return;
    uint32_t vt=MEM32(obj);
    MEM32(vt)=hle_resolve("POD::DDrawQueryInterface");
    MEM32(vt+8*4)=hle_resolve("POD::EnumDisplayModes");
}
static void ddraw_query(void) {
    uint32_t a[3]={A32(0),A32(1),A32(2)},out=A32(2);
    uint32_t r=hle_call_guest(hle_resolve("IDirectDraw::QueryInterface"),3,a);
    if (!r && out) patch_ddraw(MEM32(out));
    RET(r,3);
}
static void ddraw_create(void) {
    uint32_t a[3]={A32(0),A32(1),A32(2)},out=A32(1);
    uint32_t r=hle_call_guest(ddraw_base,3,a);
    if (!r && out) patch_ddraw(MEM32(out));
    RET(r,3);
}
static void mouse_event(void) {
    uint32_t flags=A32(0),mark=hle_scratch_mark(),pos=hle_scratch(8);
    uint32_t a=pos;
    hle_call_guest(hle_resolve("GetCursorPos"),1,&a);
    int x=(int)MEM32(pos),y=(int)MEM32(pos+4);
    if (flags&1) {
        if (flags&0x8000) {
            int w,h,bpp; hle_dd_mode(&w,&h,&bpp);
            x=(int)((uint64_t)A32(1)*(w-1)/65535);
            y=(int)((uint64_t)A32(2)*(h-1)/65535);
        } else { x+=(int32_t)A32(1); y+=(int32_t)A32(2); }
        hle_input_mouse(0x200,x,y);
    }
    if (flags&2) hle_input_mouse(0x201,x,y);
    if (flags&4) hle_input_mouse(0x202,x,y);
    if (flags&8) hle_input_mouse(0x204,x,y);
    if (flags&16) hle_input_mouse(0x205,x,y);
    if (flags&32) hle_input_mouse(0x207,x,y);
    if (flags&64) hle_input_mouse(0x208,x,y);
    hle_scratch_release(mark); RET(0,5);
}
static void is_iconic(void) { RET((hle_window_style(A32(0))&0x20000000u)!=0,1); }
static void open_icon(void) {
    uint32_t a[2]={A32(0),9};
    hle_call_guest(hle_resolve("ShowWindow"),2,a);
    RET(hle_is_window(A32(0)),1);
}
static const win32hle_shim shims[]={
    {"GetWindowsDirectoryA",windows_dir},{"GetDriveTypeA",drive_type},
    {"GetSystemDirectoryA",system_dir},
    {"GetVolumeInformationA",volume_info},{"GetPrivateProfileStringA",ini_string},
    {"AVIFileOpenA",avi_open},{"MessageBoxA",message_box},
    {"GetModuleFileNameW",no_unicode3},{"SetEnvironmentVariableW",no_unicode2},
    {"GetCommandLineW",no_unicode0},
    {"CharUpperBuffA",upper_buffer},
    {"AVIFileInit",avi_init},{"AVIFileExit",avi_init},
    {"GetWindowDC",window_dc},
    {"DirectDrawCreate",ddraw_create},{"POD::DDrawQueryInterface",ddraw_query},
    {"POD::EnumDisplayModes",enum_modes},
    {"mouse_event",mouse_event},
    {"IsIconic",is_iconic},{"OpenIcon",open_icon},
    {NULL,NULL}
};
void pod_compat_register(int videos, unsigned bits) {
    desktop_bits=bits;
    if (desktop_bits) {
        static const win32hle_shim display[]={{"GetDeviceCaps",device_caps},{NULL,NULL}};
        win32hle_register(display);
    }
    if (videos) win32hle_register(pod_video_shims);
    win32hle_register(shims);
    win32hle_register(pod_draw_shims);
}
void pod_compat_bind(void) {
    if (desktop_bits) for (const win32hle_shim *s=win32hle_gdi32;s->name;s++)
        if (!strcmp(s->name,"GetDeviceCaps")) {
            static win32hle_shim original[2];
            original[0].name="POD::DeviceCapsBase"; original[0].fn=s->fn;
            win32hle_register(original); device_caps_base=hle_resolve(original[0].name);
        }
    for (const win32hle_shim *s=win32hle_ddraw;s->name;s++)
        if (!strcmp(s->name,"DirectDrawCreate")) {
            static win32hle_shim original[2];
            original[0].name="POD::DirectDrawBase"; original[0].fn=s->fn;
            win32hle_register(original); ddraw_base=hle_resolve(original[0].name);
        }
    /* Save the toolkit's implementation for all non-video INI queries. */
    for (const win32hle_shim *s=win32hle_kernel32_ext;s->name;s++)
        if (!strcmp(s->name,"GetPrivateProfileStringA")) {
            static win32hle_shim original[2];
            original[0].name="POD::ProfileStringBase"; original[0].fn=s->fn;
            win32hle_register(original); profile_string=hle_resolve(original[0].name);
        }
}
