/* Guest DrawDib/GDI/DirectDraw bridge. The frontend owns the final framebuffer
 * output; this module only updates the HLE primary surface. */
#include <stdio.h>
#include <string.h>
#include "video_present.h"
#include "codec.h"
static void draw_dib_open(void) { RET(hle_handle_alloc(HLE_H_OTHER,NULL),0); }
static void draw_dib_close(void) { RET(hle_handle_close(A32(0)),1); }
static void present_gdi(void) {
    const uint8_t *source,*dest; const uint32_t *palette;
    int sw,sh,sp,sb,dw,dh,dp,db; unsigned seq;
    if (!hle_gdi_window_frame(&source,&sw,&sh,&sp,&sb,&seq) || sb!=32 ||
        !hle_dd_frame(&dest,&dw,&dh,&dp,&db,&palette,&seq) || (db!=16 && db!=32)) return;
    int w=sw<dw?sw:dw,h=sh<dh?sh:dh;
    /* The window DC is separate from DD storage, which may be released or
     * flipped between movie frames. Copy; never leave a DC aliasing it. */
    for (int y=0;y<h;y++) for (int x=0;x<w;x++) {
        uint32_t rgb; memcpy(&rgb,source+y*sp+x*4,4);
        uint8_t *out=(uint8_t *)dest+y*dp+x*(db/8);
        if (db==32) memcpy(out,&rgb,4);
        else {
            uint16_t pixel=(uint16_t)(((rgb>>19)&31)<<11 | ((rgb>>10)&63)<<5 | ((rgb>>3)&31));
            memcpy(out,&pixel,2);
        }
    }
    hle_dd_primary_touched();
}
static void draw_dib(void) {
    uint32_t info=A32(6),bits=A32(7);
    static unsigned draws;
    if (++draws<=3) fprintf(stderr,"[video] DrawDib %d,%d %dx%d src=%d,%d %dx%d info=%X bits=%X\n",(int)A32(2),(int)A32(3),(int)A32(4),(int)A32(5),(int)A32(8),(int)A32(9),(int)A32(10),(int)A32(11),info,bits);
    if (!info || MEM32(info)<40 || MEM32(info)>4096) RET(0,13);
    pod_video_pace(info);
    int w=(int)MEM32(info+4),height=(int)MEM32(info+8),h=height<0?-height:height;
    unsigned bpp=MEM16(info+14),colors=MEM32(info+32);
    if (w<1 || w>4096 || h<1 || h>4096 || (bpp!=8 && bpp!=16 && bpp!=24 && bpp!=32)) RET(0,13);
    if (bpp==8 && !colors) colors=256;
    if (!bits) bits=info+MEM32(info)+(bpp==8?colors*4:MEM32(info+16)==3 && MEM32(info)==40?12:0);
    uint32_t bitmap=hle_gdi_make_dib(w,h,bpp,hle_ptr(bits),height<0,
        bpp==8?hle_ptr(info+MEM32(info)):NULL,(int)colors,
        !(MEM32(info+16)==3 && MEM32(info+MEM32(info)+4)==0x7e0));
    if (!bitmap) RET(0,13);
    uint32_t window=A32(1),src=hle_call_guest(hle_resolve("CreateCompatibleDC"),1,&window);
    uint32_t select[2]={src,bitmap};
    hle_call_guest(hle_resolve("SelectObject"),2,select);
    uint32_t a[11]={window,A32(2),A32(3),A32(4)==0xffffffffu?(uint32_t)w:A32(4),
        A32(5)==0xffffffffu?(uint32_t)h:A32(5),src,A32(8),A32(9),
        A32(10)==0xffffffffu?(uint32_t)w:A32(10),A32(11)==0xffffffffu?(uint32_t)h:A32(11),0x00cc0020};
    uint32_t r=src?hle_call_guest(hle_resolve("StretchBlt"),11,a):0;
    hle_gdi_release_dc(src);
    hle_call_guest(hle_resolve("DeleteObject"),1,&bitmap);
    if (r) present_gdi();
    RET(r!=0,13);
}
const win32hle_shim pod_draw_shims[]={
    {"DrawDibOpen",draw_dib_open},{"DrawDibClose",draw_dib_close},{"DrawDibDraw",draw_dib},
    {NULL,NULL}
};
