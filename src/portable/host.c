/* POD-specific native host for the existing game lift. Portable Windows
 * services come from pcrecomp/win32hle (MIT, sp00nz). */
#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>
#include <errno.h>
#include <glob.h>
#include <limits.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>
#include "host.h"
#include "dinput.h"
#include "audio.h"
#include "codec.h"
#include "cd_audio.h"
#include "compat.h"
#include "input_sdl.h"

static int headless, in_pump, videos, window_log;
static uint32_t close_requested;
static int close_reported;
static unsigned watchdog_seconds, exit_ms, close_ms, capture_ms=2000;
static uint32_t started, last_heartbeat;
static pthread_t main_thread;
static atomic_int guest_done;
static int guest_exit_code;
static const char *capture_path;
static int captured;
static unsigned capture_every, capture_count;
static uint32_t capture_next;
static void (*screen_primary)(void);
static struct { SDL_Scancode sc; unsigned at, hold; int phase; } keys[64];
static unsigned nkeys;

static void make_dir(const char *path) {
    if (mkdir(path,0755) && errno!=EEXIST) { perror(path); exit(1); }
}
static void capture(void) {
    const uint8_t *px; const uint32_t *palette=NULL;
    int w,h,pitch,bpp; unsigned seq;
    uint32_t elapsed=hle_ticks_ms()-started;
    if (!capture_path || captured || (capture_every && elapsed<capture_next)) return;
    if (!hle_dd_frame(&px,&w,&h,&pitch,&bpp,&palette,&seq) &&
        !hle_gdi_window_frame(&px,&w,&h,&pitch,&bpp,&seq)) return;
    SDL_Surface *s=SDL_CreateRGBSurfaceWithFormat(0,w,h,32,SDL_PIXELFORMAT_ARGB8888);
    if (!s) return;
    for (int y=0; y<h; y++) {
        uint32_t *out=(uint32_t *)((uint8_t *)s->pixels+y*s->pitch);
        for (int x=0; x<w; x++) {
            if (bpp==8) {
                unsigned v=px[y*pitch+x];
                out[x]=0xff000000u | (palette ? palette[v] : v*0x010101u);
            } else if (bpp==16) {
                uint16_t v; memcpy(&v,px+y*pitch+x*2,2);
                out[x]=0xff000000u | ((v>>11)*255/31)<<16 |
                       (((v>>5)&63)*255/63)<<8 | (v&31)*255/31;
            } else if (bpp==32) memcpy(&out[x],px+y*pitch+x*4,4);
            else out[x]=0xff000000u;
        }
    }
    char numbered[PATH_MAX];
    const char *output=capture_path;
    if (capture_every) {
        int n=snprintf(numbered,sizeof numbered,"%s.%03u.bmp",capture_path,capture_count);
        if (n<0 || (size_t)n>=sizeof numbered) {
            fprintf(stderr,"[capture] output path too long\n");
            captured=1; SDL_FreeSurface(s); return;
        }
        output=numbered;
    }
    if (!SDL_SaveBMP(s,output)) {
        capture_count++;
        captured=!capture_every || capture_count>=64;
        capture_next=elapsed+capture_every;
        fprintf(stderr,"[capture] at=%u frame=%u %dx%d %dbpp -> %s\n",elapsed,seq,w,h,bpp,output);
    } else fprintf(stderr,"[capture] %s\n",SDL_GetError());
    SDL_FreeSurface(s);
}
static int on_key(int sc, int down) {
    if (window_log) fprintf(stderr,"[input-feed] at=%u scancode=%d down=%d\n",hle_ticks_ms()-started,sc,down);
    pod_keyboard_key(sc,down);
    return 0; /* HLE also delivers the corresponding window messages. */
}
static void primary_changed(void) {
    if (screen_primary) screen_primary();
    if (hle_ticks_ms()-started>=capture_ms) capture();
}
static uint32_t vk_of(SDL_Scancode sc) {
    switch(sc) {
    case SDL_SCANCODE_ESCAPE:return 27; case SDL_SCANCODE_RETURN:return 13;
    case SDL_SCANCODE_SPACE:return 32; case SDL_SCANCODE_UP:return 38;
    case SDL_SCANCODE_DOWN:return 40; case SDL_SCANCODE_LEFT:return 37;
    case SDL_SCANCODE_RIGHT:return 39; case SDL_SCANCODE_TAB:return 9;
    default: { if (sc>=SDL_SCANCODE_A && sc<=SDL_SCANCODE_Z) return 'A'+sc-SDL_SCANCODE_A;
        if (sc>=SDL_SCANCODE_1 && sc<=SDL_SCANCODE_9) return '1'+sc-SDL_SCANCODE_1;
        if (sc==SDL_SCANCODE_0) return '0';
        SDL_Keycode k=SDL_GetKeyFromScancode(sc);
        return k>='a' && k<='z' ? (uint32_t)(k-'a'+'A') : k>0 && k<128 ? (uint32_t)k : 0; }
    }
}
static void window_diagnostic(uint32_t hwnd,uint32_t msg,uint32_t w,uint32_t l) {
    if (msg==2 || msg==0x10 || msg==0x82 || (msg>=0x100 && msg<=0x102) || msg==0x201 || msg==0x202 || (msg==0x112 && (w&0xfff0)==0xf060))
        fprintf(stderr,"[window] at=%u thread=%u hwnd=%X class=%s msg=%X w=%X l=%X\n",
            hle_ticks_ms()-started,hle_thread_id(),hwnd,hle_window_class(hwnd),msg,w,l);
}
static void pump(void) {
    if (in_pump || !pthread_equal(pthread_self(),main_thread)) return;
    in_pump=1;
    int trace=win32hle_trace;
    if (window_log) win32hle_trace=1;
    hle_screen_pump();
    win32hle_trace=trace;
    pod_audio_pump();
    pod_cd_pump();
    if (!headless && !SDL_GetKeyboardFocus()) pod_keyboard_release();
    uint32_t now=hle_ticks_ms(), elapsed=now-started;
    if (hle_first_hwnd() && now-last_heartbeat>=16) {
        hle_post_message(hle_first_hwnd(),0x113,1,0);
        last_heartbeat=now;
    }
    for (unsigned i=0;i<nkeys;i++) {
        if (keys[i].phase==0 && elapsed>=keys[i].at) {
            on_key(keys[i].sc,1);
            uint32_t map[2]={vk_of(keys[i].sc),0};
            uint32_t scan=hle_call_guest(hle_resolve("MapVirtualKeyA"),2,map);
            hle_input_key(1,map[0],scan);
            keys[i].phase=1;
            fprintf(stderr,"[input] %s down at %u\n",SDL_GetScancodeName(keys[i].sc),elapsed);
        }
        if (keys[i].phase==1 && elapsed>=keys[i].at+keys[i].hold) {
            on_key(keys[i].sc,0);
            uint32_t map[2]={vk_of(keys[i].sc),0};
            uint32_t scan=hle_call_guest(hle_resolve("MapVirtualKeyA"),2,map);
            hle_input_key(0,map[0],scan);
            keys[i].phase=2;
            fprintf(stderr,"[input] %s up at %u\n",SDL_GetScancodeName(keys[i].sc),elapsed);
        }
    }
    if (elapsed>=capture_ms) capture();
    if (close_ms && elapsed>=close_ms && hle_first_hwnd()) {
        fprintf(stderr,"[host] requesting window close at %u ms hwnd=%X class=%s\n",
            elapsed,hle_first_hwnd(),hle_window_class(hle_first_hwnd()));
        close_requested=now;
        hle_post_message(hle_first_hwnd(),0x10,0,0); /* WM_CLOSE, handled by POD */
        close_ms=0;
    }
    if (window_log && close_requested && !close_reported && now-close_requested>=2000) {
        fprintf(stderr,"[window] still running two seconds after close; first=%X class=%s\n",
            hle_first_hwnd(),hle_window_class(hle_first_hwnd()));
        hle_queue_dump(); close_reported=1;
    }
    if (exit_ms && elapsed>=exit_ms) {
        fprintf(stderr,"[host] bounded run complete at %u ms\n",elapsed);
        hle_exit(0);
    }
    in_pump=0;
}
static void finish(int code) {
    capture();
    fprintf(stderr,"[host] exit=%d guest=sub_%08X last_import=%s\n",code,g_cur_func,
            g_cur_import ? g_cur_import : "none");
    if (!pthread_equal(pthread_self(),main_thread)) {
        guest_exit_code=code;
        atomic_store(&guest_done,1);
        /* ExitProcess belongs to the guest. Let the main SDL thread clean
         * up its window instead of running Cocoa teardown on a worker. */
        hle_block_begin();
        pthread_exit(NULL);
    }
    pod_cd_shutdown();
    pod_audio_shutdown();
    SDL_Quit();
}
static void *run_guest(void *unused) {
    (void)unused;
    int code=(int)recomp_host_run();
    finish(code);
    return NULL;
}
static void *watchdog(void *unused) {
    (void)unused; sleep(watchdog_seconds);
    fprintf(stderr,"[watchdog] %u seconds: sub_%08X last_import=%s\n",
            watchdog_seconds,g_cur_func,g_cur_import ? g_cur_import : "none");
    fflush(stderr); _exit(124);
}
static void dialog_diagnostic(uint32_t hwnd,uint32_t resource) {
    fprintf(stderr,"[dialog] hwnd=%X resource=%u\n",hwnd,resource);
    uint32_t mark=hle_scratch_mark(),buf=hle_scratch(512);
    uint32_t a[3]={hwnd,buf,512};
    hle_call_guest(hle_resolve("GetWindowTextA"),3,a);
    fprintf(stderr,"[dialog] title: %s\n",(char *)hle_ptr(buf));
    for (unsigned id=1;id<2000;id++) {
        uint32_t child=hle_dialog_item(hwnd,id);
        if (!child) continue;
        a[0]=child;
        hle_call_guest(hle_resolve("GetWindowTextA"),3,a);
        fprintf(stderr,"[dialog] %u: %s\n",id,(char *)hle_ptr(buf));
    }
    hle_scratch_release(mark);
}

static void add_key(const char *arg) {
    char name[40]; unsigned at=0,hold=150;
    if (nkeys==64 || sscanf(arg,"%39[^@]@%u+%u",name,&at,&hold)<2) {
        fprintf(stderr,"invalid --key %s\n",arg); exit(64);
    }
    if (!strcasecmp(name,"ENTER")) strcpy(name,"Return");
    if (!strcasecmp(name,"ESC")) strcpy(name,"Escape");
    SDL_Scancode sc=SDL_GetScancodeFromName(name);
    if (!sc) { fprintf(stderr,"unknown key %s\n",name); exit(64); }
    keys[nkeys].sc=sc; keys[nkeys].at=at; keys[nkeys++].hold=hold;
}
int main(int argc,char **argv) {
    const char *game=POD_SOURCE_DIR "/game",*state=POD_SOURCE_DIR "/work/native-state";
    int run=1,codec_probe=0;
    const char *window_title="POD Gold — native prototype";
    unsigned decode_first=0;
    int no_cd=0;
    const char *cd_cue=NULL,*cd_capture=NULL;
    char cue_absolute[PATH_MAX],cd_absolute[PATH_MAX];
    const char *audio_capture=NULL,*decode_input=NULL,*decode_output=NULL;
    char audio_absolute[PATH_MAX];
    for (int i=1;i<argc;i++) {
        if (!strcmp(argv[i],"--run")) run=1;
        else if (!strcmp(argv[i],"--decode-test") && i+2<argc) { decode_input=argv[++i]; decode_output=argv[++i]; codec_probe=1; }
        else if (!strcmp(argv[i],"--decode-start") && i+1<argc) decode_first=(unsigned)strtoul(argv[++i],NULL,10);
        else if (!strcmp(argv[i],"--no-cd")) no_cd=1;
        else if (!strcmp(argv[i],"--cd-cue") && i+1<argc) { cd_cue=argv[++i]; no_cd=0; }
        else if (!strcmp(argv[i],"--cd-capture") && i+1<argc) cd_capture=argv[++i];
        else if (!strcmp(argv[i],"--videos")) videos=1;
        else if (!strcmp(argv[i],"--codec-probe")) codec_probe=1;
        else if (!strcmp(argv[i],"--dry-run")) run=0;
        else if (!strcmp(argv[i],"--headless")) headless=1;
        else if (!strcmp(argv[i],"--window-title") && i+1<argc) window_title=argv[++i];
        else if (!strcmp(argv[i],"--window-log")) window_log=1;
        else if (!strcmp(argv[i],"--trace")) win32hle_trace=1;
        else if (!strcmp(argv[i],"--game") && i+1<argc) game=argv[++i];
        else if (!strcmp(argv[i],"--state-dir") && i+1<argc) state=argv[++i];
        else if (!strcmp(argv[i],"--watchdog") && i+1<argc) watchdog_seconds=(unsigned)strtoul(argv[++i],NULL,10);
        else if (!strcmp(argv[i],"--exit-after") && i+1<argc) exit_ms=(unsigned)strtoul(argv[++i],NULL,10);
        else if (!strcmp(argv[i],"--close-after") && i+1<argc) close_ms=(unsigned)strtoul(argv[++i],NULL,10);
        else if (!strcmp(argv[i],"--audio-capture") && i+1<argc) audio_capture=argv[++i];
        else if (!strcmp(argv[i],"--capture") && i+1<argc) capture_path=argv[++i];
        else if (!strcmp(argv[i],"--capture-ms") && i+1<argc) capture_ms=(unsigned)strtoul(argv[++i],NULL,10);
        else if (!strcmp(argv[i],"--capture-every") && i+1<argc) capture_every=(unsigned)strtoul(argv[++i],NULL,10);
        else if (!strcmp(argv[i],"--key") && i+1<argc) add_key(argv[++i]);
        else { fprintf(stderr,"usage: pod [--dry-run] [--codec-probe] [--videos] [--game DIR] [--state-dir DIR] [--headless] [--trace]\n"
            "  [--window-log] [--window-title TEXT] [--watchdog SECONDS] [--exit-after MS] [--key NAME@MS[+HOLD]]\n"
            "  [--decode-test INPUT.avi OUTPUT.bgr] [--decode-start FRAME] [--close-after MS]\n"
            "  [--cd-cue FILE.cue | --no-cd] [--cd-capture FILE.wav]\n"
            "  [--capture FILE.bmp] [--capture-ms MS] [--capture-every MS] [--audio-capture FILE.wav]\n"); return 64; }
    }
    char game_path[PATH_MAX],state_path[PATH_MAX],path[PATH_MAX],image[PATH_MAX];
    if (!realpath(game,game_path)) { perror(game); return 1; }
    make_dir(state);
    if (!realpath(state,state_path)) { perror(state); return 1; }
    snprintf(path,sizeof path,"%s/UbiSoft",state_path); make_dir(path);
    snprintf(path,sizeof path,"%s/UbiSoft/Ubi.ini",state_path);
    if (access(path,F_OK)) {
        FILE *f=fopen(path,"w"); if (!f) { perror(path); return 1; }
        fputs("[POD2_0]\nDirectory=\nLanguage=English\nConfiguration=2002\nValid=1\nSizeView=0\nDisplayMode=0\nLetterBox=0\n\n[OSD]\nDirectory=OSD\nValid=2.11\nVersion=2.11\n",f);
        fclose(f);
    }
    if (capture_path) {
        static char absolute[PATH_MAX],cwd[PATH_MAX];
        if (!getcwd(cwd,sizeof cwd)) return 1;
        snprintf(absolute,sizeof absolute,"%s%s%s",capture_path[0]=='/'?"":cwd,capture_path[0]=='/'?"":"/",capture_path);
        capture_path=absolute;
    }
    if (audio_capture) {
        char cwd[PATH_MAX];
        if (!getcwd(cwd,sizeof cwd)) return 1;
        snprintf(audio_absolute,sizeof audio_absolute,"%s%s%s",audio_capture[0]=='/'?"":cwd,audio_capture[0]=='/'?"":"/",audio_capture);
        audio_capture=audio_absolute;
    }
    if (!no_cd && !cd_cue) {
        glob_t cues={0};
        if (!glob(POD_SOURCE_DIR "/work/disc/*.cue",0,NULL,&cues) && cues.gl_pathc==1) {
            snprintf(cue_absolute,sizeof cue_absolute,"%s",cues.gl_pathv[0]); cd_cue=cue_absolute;
        }
        globfree(&cues);
    }
    if (no_cd) cd_cue=NULL;
    if (cd_cue && cd_cue!=cue_absolute) {
        if (!realpath(cd_cue,cue_absolute)) { perror(cd_cue); return 1; } cd_cue=cue_absolute;
    }
    if (cd_capture) {
        char cwd[PATH_MAX]; if (!getcwd(cwd,sizeof cwd)) return 1;
        snprintf(cd_absolute,sizeof cd_absolute,"%s%s%s",cd_capture[0]=='/'?"":cwd,cd_capture[0]=='/'?"":"/",cd_capture);
        cd_capture=cd_absolute;
    }
    snprintf(image,sizeof image,"%s/PODMMX.EXE",game_path);
    if (codec_probe || videos) {
        char codec_path[PATH_MAX];
        snprintf(codec_path,sizeof codec_path,"%s/INSTALL/IR41_32.DLL",game_path);
        if (pod_codec_map(codec_path)) return 1;
    }
    hle_set_drive('C',game_path); hle_set_drive('D',game_path);
    hle_set_overlay(game_path,state_path);
    hle_set_module_path(image);
    hle_set_command_line("\"C:\\PODMMX.EXE\"");
    pod_compat_register(videos);
    win32hle_register(pod_dinput_shims);
    win32hle_register(pod_audio_shims);
    win32hle_register(pod_cd_shims);
    recomp_host_init();
    pod_compat_bind();
    if (recomp_host_boot_permissive(image)) return 1;
    fprintf(stderr,"[pod] native host; videos=%s; PCM effects via SDL; state=%s\n",videos?"experimental IV41 + SDL-converted audio":"skipped",state_path);
    if (codec_probe) {
        if (watchdog_seconds) { pthread_t t; if (pthread_create(&t,NULL,watchdog,NULL)) return 1; pthread_detach(t); }
        return decode_input?pod_codec_decode(decode_input,decode_output,decode_first):pod_codec_probe();
    }
    if (!run) return 0;
    if (chdir(game_path)) { perror(game_path); return 1; }
    SDL_SetMainReady();
    /* DirectDraw owns the guest's 60 Hz clock. Waiting for a second,
     * physical refresh while holding the HLE machine can delay guest
     * startup and couple its speed to the macOS display again. */
    hle_screen_vsync(0);
    main_thread=pthread_self();
    hle_gdi_window_surface(hle_alloc(640*480*4),640,480,640*4,32);
    if (hle_screen_open(window_title,0,headless)) return 1;
    if (headless) SDL_setenv("SDL_AUDIODRIVER","dummy",1);
    pod_audio_init(audio_capture);
    if (pod_cd_init(cd_cue,cd_capture)) return 1;
    hle_screen_key_hook=on_key; hle_set_pump_hook(pump); hle_on_exit=finish;
    hle_dialog_hook=dialog_diagnostic;
    if (window_log) hle_proc_hook=window_diagnostic;
    started=hle_ticks_ms();
    screen_primary=hle_dd_primary_hook; hle_dd_primary_hook=primary_changed;
    hle_gdi_window_hook=primary_changed;
    if (watchdog_seconds) {
        pthread_t t;
        if (pthread_create(&t,NULL,watchdog,NULL)) return 1;
        pthread_detach(t);
    }
    /* The game's WinMain can wait for its renderer without pumping messages.
     * Keep Cocoa/SDL responsive on main, and run the guest on a worker. */
    pthread_t guest_thread;
    pthread_attr_t guest_attr;
    if (pthread_attr_init(&guest_attr)) return 1;
#ifdef __APPLE__
    if (pthread_attr_setstacksize(&guest_attr,8u*1024u*1024u)) { pthread_attr_destroy(&guest_attr); return 1; }
#endif
    int guest_error=pthread_create(&guest_thread,&guest_attr,run_guest,NULL);
    pthread_attr_destroy(&guest_attr);
    if (guest_error) return 1;
    while (!atomic_load(&guest_done)) {
        mach_enter(); pump(); mach_leave();
        usleep(1000);
    }
    pthread_join(guest_thread,NULL);
    /* ExitProcess stops every guest thread. Keep the machine until the
     * process exits: a surviving mixer must not write into a capture FILE
     * or SDL device while the main thread destroys it. */
    mach_enter();
    pod_cd_shutdown();
    pod_audio_shutdown();
    SDL_Quit();
    return guest_exit_code;
}
