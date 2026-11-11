/* DirectInput 3 keyboard in the guest ABI. Queue semantics follow POD's
 * existing runtime/dinput.c; no host function pointers enter guest memory. */
#include <stdio.h>
#include <string.h>
#include "dinput.h"

enum { QUEUE = 64, DIERR_UNSUPPORTED = 0x80004001u };
static struct { uint32_t key, data, time, sequence; } queue[QUEUE];
static uint8_t state[256];
static unsigned count, sequence;
static uint32_t event_handle, input_vtable, keyboard_vtable;

static void key_event(unsigned key, int down) {
    if (!key || key>=256 || state[key] == (down ? 0x80 : 0)) return;
    state[key] = down ? 0x80 : 0;
    if (count == QUEUE) {
        memmove(queue, queue + 1, (QUEUE - 1) * sizeof *queue);
        count--;
    }
    queue[count].key = key; queue[count].data = state[key];
    queue[count].time = hle_ticks_ms(); queue[count++].sequence = ++sequence;
    if (event_handle) hle_set_event(event_handle);
}
void pod_keyboard_dik(unsigned key, int down) { key_event(key, down); }
void pod_keyboard_release(void) {
    for (unsigned k = 1; k < 256; k++) if (state[k]) key_event(k, 0);
}

static void success1(void) { RET(0, 1); }
static void success2(void) { RET(0, 2); }
static void success3(void) { RET(0, 3); }
static void enum_devices(void) { RET(0, 5); } /* no joystick/mouse devices yet */
static void unsupported3(void) { RET(DIERR_UNSUPPORTED, 3); }
static void unsupported2(void) { RET(DIERR_UNSUPPORTED, 2); }
static void unsupported4(void) { RET(DIERR_UNSUPPORTED, 4); }
static void get_caps(void) {
    uint32_t p = A32(1), n = p ? MEM32(p) : 0;
    if (!p || n < 24) RET(0x80070057u, 2);
    memset(hle_ptr(p), 0, n);
    MEM32(p)=n; MEM32(p+4)=1; MEM32(p+8)=3; MEM32(p+16)=256;
    RET(0, 2);
}
static void set_format(void) {
    uint32_t p = A32(1);
    if (!p || MEM32(p+12) != 256) RET(0x80070057u, 2);
    RET(0, 2);
}
static void get_state(void) {
    if (A32(1) != 256 || !A32(2)) RET(0x80070057u, 3);
    memcpy(APTR(2), state, sizeof state);
    RET(0, 3);
}
static void set_event(void) {
    event_handle = A32(1);
    if (win32hle_trace) fprintf(stderr,"[dinput-event] handle=%X\n",event_handle);
    if (event_handle && count) hle_set_event(event_handle);
    RET(0, 2);
}
static void get_data(void) {
    uint32_t stride=A32(1), dst=A32(2), pn=A32(3), flags=A32(4);
    if (!pn || stride < 16) RET(0x80070057u, 5);
    unsigned n = MEM32(pn) < count ? MEM32(pn) : count;
    if (win32hle_trace && n) fprintf(stderr,"[dinput-data] queued=%u stride=%u want=%u dst=%X flags=%X\n",count,stride,MEM32(pn),dst,flags);
    if (dst) for (unsigned i=0; i<n; i++) {
        uint32_t p=dst+i*stride;
        memset(hle_ptr(p),0,stride);
        MEM32(p)=queue[i].key; MEM32(p+4)=queue[i].data;
        MEM32(p+8)=queue[i].time; MEM32(p+12)=queue[i].sequence;
    }
    MEM32(pn)=n;
    if (!(flags & 1)) { memmove(queue, queue+n, (count-n)*sizeof *queue); count-=n; }
    if (event_handle && count) hle_set_event(event_handle);
    RET(0, 5);
}
static const win32hle_shim keyboard[] = {
    {"PODKeyboard::QueryInterface",hle_com_QueryInterface},
    {"PODKeyboard::AddRef",hle_com_AddRef},{"PODKeyboard::Release",hle_com_Release},
    {"PODKeyboard::GetCapabilities",get_caps}, {"PODKeyboard::EnumObjects",unsupported4},
    {"PODKeyboard::GetProperty",unsupported3},{"PODKeyboard::SetProperty",success3},
    {"PODKeyboard::Acquire",success1},{"PODKeyboard::Unacquire",success1},
    {"PODKeyboard::GetDeviceState",get_state},{"PODKeyboard::GetDeviceData",get_data},
    {"PODKeyboard::SetDataFormat",set_format},{"PODKeyboard::SetEventNotification",set_event},
    {"PODKeyboard::SetCooperativeLevel",success3},{"PODKeyboard::GetObjectInfo",unsupported4},
    {"PODKeyboard::GetDeviceInfo",unsupported2},{"PODKeyboard::RunControlPanel",unsupported3},
    {"PODKeyboard::Initialize",unsupported4},{NULL,NULL}
};
static void create_device(void) {
    uint32_t guid = A32(1), out=A32(2);
    if (!out) RET(0x80070057u,4);
    MEM32(out)=0;
    if (!guid || MEM32(guid) != 0x6f1d2b61u) RET(0x80070002u,4);
    if (!keyboard_vtable) keyboard_vtable=hle_com_vtable(keyboard);
    MEM32(out)=hle_com_new(keyboard_vtable,8);
    fprintf(stderr,"[dinput] keyboard created\n");
    RET(0,4);
}
static const win32hle_shim input[] = {
    {"PODInput::QueryInterface",hle_com_QueryInterface},
    {"PODInput::AddRef",hle_com_AddRef},{"PODInput::Release",hle_com_Release},
    {"PODInput::CreateDevice",create_device},{"PODInput::EnumDevices",enum_devices},
    {"PODInput::GetDeviceStatus",success2},{"PODInput::RunControlPanel",success3},
    {"PODInput::Initialize",success3},{NULL,NULL}
};
static void create_input(void) {
    uint32_t out=A32(2);
    if (!out) RET(0x80070057u,4);
    if (!input_vtable) input_vtable=hle_com_vtable(input);
    MEM32(out)=hle_com_new(input_vtable,8);
    RET(0,4);
}
const win32hle_shim pod_dinput_shims[]={{"DirectInputCreateA",create_input},{NULL,NULL}};
