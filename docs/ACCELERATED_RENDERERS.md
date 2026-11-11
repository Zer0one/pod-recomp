# Alternate renderers

## Current status — 2026-11-11

The default standalone remains the verified PODMMX software build. The same fork
also contains an experimental ARM64 Glide-to-Metal path that renders car
selection and Beltane racing scenes. Race WM_CLOSE and Quit Game / Yes now
complete through the original ExitProcess(0), with effects/CD active. Full
rendering, extended gameplay and audiovisual qualification remain outstanding. Linux and Libretro remain architectural
preparation, not implemented ports.

| Guest | Inventory | Result |
| --- | --- | --- |
| PODMMX.EXE | Existing default lift and compatibility policy | Software standalone retained; eight CTests and scripted effects/CD/race/close regression pass |
| PODX3DFX.EXE | 263 imports, including 43 Glide names; 9,595 catalog entries after observed shutdown seeds | Experimental native Metal menu/race rendering, scripted acceleration and fullscreen/window round trip observed |
| PODD3DX.EXE | 226 imports; 11,669 catalog entries; original IDirect3D GUID bytes present | Static/catalog assessment only; no accelerated runtime support claimed |

The Glide lift plus Indeo has 12,225 dispatch entries, no reported lift errors
and 1,084 bodies without a clean terminator. These counts do not establish
correct control flow or exhaustive runtime coverage. Alternate catalogs, code,
assets and detailed execution evidence stay under ignored `work/`. The default
MMX lift was separately regenerated and all 113 files were byte-identical;
its generated sources are not replaced by alternate builds.

The checked Glide executable SHA256 is
`c3c2243d919f4ca1508031699f4c47914c573d360aa99de133954458b339c08f`;
the Direct3D executable is
`51750191616b6c26fa6816160db997368a43b3fc7f4185fbb30195dac030498b`.
The GOG installer contains the same Glide executable as the disc.

## Ownership and API coverage

- `src/portable/glide_bootstrap.c` owns guest ABI decoding, POD state, virtual
  TMU storage, palette/cache epochs, guest LFBs and HLE presentation storage.
- `glide_vertex.c` selects consumed attributes and decodes the default shared
  FBI/TMU0 reciprocal-W layout. POD leaves unused vertex fields uninitialized;
  POD imports no `grHints` override. Nonfinite consumed fields are rejected.
- `glide_texture.c` owns packed/paletted conversion, mip layout and rectangular
  coordinate scales. Coordinates use a 256-texel longest edge, independently
  of the selected base LOD.
- `glide_gpu.h` describes a small native-data resource/draw contract. It contains
  no HLE pointer, SDL window, CLI or Metal type. C and Metal share state indices
  through `glide_state_layout.h`, with compile-time structure-size checks.
- `src/macos/glide_metal.m` owns native resources, command lifetime and draw
  batches; `glide_shader.metal` implements vertex/fragment operations. Only the
  separate Glide target links these components. MMX does not enable Objective-C
  or acquire a Metal dependency.

All 43 static import names have registrations. This is inventory coverage,
not a claim that every possible state of every API is supported. Current
coverage includes observed 512x384/640x480 modes, upper-left origin, independent
front/back buffers, depth, 565 LFB access, selected packed/paletted textures,
mip chains, clipping, RGB combiners, alpha sources, blending, chromakey,
presentation gamma and paced swaps. 800x600 is accepted but not game-tested.

The shader encodes reciprocal W into the SST-1 exponent/mantissa depth domain,
replacing the initial continuous ordering approximation. Metal floating-point
interpolation and rasterization are still not equivalent to Voodoo fixed-point
triangle setup. Table/Z fog, unsupported texture combiner states, NCC/YIQ
textures, split mip chains and trilinear blending are explicitly rejected.
Mipmap dithering, RGB565 output dithering and complete legacy sampling/blending
precision remain unqualified.

Draw and texture upload copy native caller data. Read/write/replacement/swap
finish pending GPU work before CPU access. Presentation currently reads BGRA
back into the existing HLE/SDL presenter on each swap. This is not a direct
Metal window presenter. Global service state, HLE GDI presentation ownership
and wall-clock pacing still require isolation for a future pull-driven host;
see [frontend boundaries](FRONTEND_ARCHITECTURE.md).

## Build and reproduce

Use the pinned toolkit and Python environment from [the native guide](MACOS_NATIVE.md):

```sh
work/venv/bin/python tools/portable/renderer_probe.py glide --stage audit
work/venv/bin/python tools/portable/renderer_probe.py glide --stage lift \
  --seed 0x004C2581 --seed 0x004C2637
cmake -S . -B build/native-glide -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DPOD_GUEST_VARIANT=glide -DPOD_GEN_OPT=-O1
cmake --build build/native-glide -j3
ctest --test-dir build/native-glide --output-on-failure
```

The seeds are observed missing targets in the jump table at 0x004C2610. They
are persisted per variant under ignored `work/`; regenerate through the
catalog/lifter rather than editing generated C. Alternate lifts use an empty
patch set: the MMX executable's address-specific CPU patches cannot be applied
to another executable. The HLE CPU profile and tested toolkit pin are retained.

The executable is `build/native-glide/pod-glide-probe`, separate from the normal
`build/native/pod` and its software app bundle. A bounded race test can use:

```sh
build/native-glide/pod-glide-probe --headless \
  --state-dir work/glide-test-state --watchdog 45 --close-after 26000 \
  --key ESC@6500 --key ENTER@7000 --key ENTER@7800 \
  --key ENTER@9000 --key ENTER@10500 --key ENTER@14000
```

The paced swap records a surface generation before releasing the HLE machine.
If another thread closes, shuts down or replaces that surface during the wait,
the old swap returns without using the freed or replacement context. Closing
and reopening also reset the pacing deadline. The asset-free service test
covers both replacement-buffer integrity and shutdown during a pending swap;
a negative control without the guard fails both checks. The ten-test suite
and service ASan/UBSan pass (`work/glide-swap-full-ctest.log`,
`work/glide-swap-sanitize-ctest.log`, `work/glide-swap-negative/test.log`).

Before the POD lifecycle correction below, the real effects/CD race still
reached its 41-second WM_CLOSE watchdog after this swap fix (`work/glide-swap-game/effects-result.json`). This resource-lifetime
correction alone did not resolve the separate guest wait or menu Quit failure.

A watchdog expiry is a failed shutdown check. `--exit-after` is a host cutoff,
not evidence of the game choosing Quit. Only the Glide guest reports a 16-bit
virtual desktop through GetDeviceCaps; the actual macOS display and the MMX
host policy are unchanged. Linux currently needs another GPU backend; none
is supplied or verified here. The existing Windows host build path is retained.

## Verified evidence and limitations

On Apple M4, all eleven CTests in the current Glide build pass
(`work/shutdown-fix/final-ctest.log`). The asset-free GPU and
Glide guest-service tests also pass ASan/UBSan. They cover full-target
clear/readback, short-buffer rejection, Z occlusion, independent buffer swaps,
textured quadrants, rectangular/projective coordinates, chromakey, alpha
blending, encoded W thresholds and same-bucket depth ties. Service tests cover
read/write LFB locks, wrong unlock types, 565 rectangles and bounds, mode
changes, palette updates and same-address TMU cache invalidation.

Metal tests run on the actual device. A sandbox without a Metal device returns
77 (skip), not a hardware pass. Under ASan, the current sdl2-compat installation
needs `DYLD_FALLBACK_LIBRARY_PATH` set to the SDL3 library directory, as already
documented for the software host. Logs include `work/glide-final-integrated-ctest.log`,
`work/glide-wdepth-ctest.log` and `work/glide-wdepth-sanitize-ctest.log`.

Scripted real-game runs render Gamma/Beltane racing and a 64 MPH acceleration
capture (`work/glide-wdepth.png`). A temporary test bundle and CUA verified F11
fullscreen and a second F11 returning to a decorated window while the game
entered a race. The first UI attempt had delayed permission approval and is
inconclusive, not proof of a native-window hang. Screenshots were observed
inline; the fullscreen observation record is `work/glide-ui-verification.json`.
These tests do not qualify extended gameplay, physical controls or listening.

Before the lifecycle correction below, race shutdown was intermittent: one motion test reaches ExitProcess(0),
while several close tests time out. The optimized `-O1` effects/CD case also
reaches its 41-second watchdog (`work/glide-o1-effects.log`), so optimization
does not resolve this separate race-shutdown wait. A temporary diagnostic observed thread 260
waiting for an unsignaled event while worker 268 waited on an empty semaphore.
The source comparison and isolated semaphore-release trial below now explain
this observed wait cycle; the event itself is never force-signaled. The diagnostics were removed and the probe rebuilt.
Evidence: `work/glide-motion-run.log`, `work/glide-wait-diagnostic.log`.

A fresh `-O1` run with CD disabled also reaches the watchdog. A native stack
sample at 29 seconds places the guest in `004C339C -> 004C1248 ->
WaitForSingleObject`, after posting a teardown message and creating an event;
the remaining worker is in `004C2BE8 -> 004C1A74 -> 004C1248 ->
WaitForSingleObject`. No Metal call appears in these sampled wait chains.
This is evidence about the sampled shutdown, not proof that rendering cannot
contribute to another failure. The log, sample and command are retained under
`work/glide-shutdown-sample/`. The initial interactive Quit check expired
before the complete confirmation sequence was observed. A subsequent scripted
Pause / Down / Down / Enter / Left / Enter check does reach Quit Game / Yes,
but the renderer worker then selects board 0 after Glide shutdown and aborts.
The menu skips a disabled row: three Down presses wrap back to Resume.

An experiment discarding late draw/clear/swap calls after explicit shutdown
avoided the service abort, but still reached POD's own fatal-error loop. It is
not a shutdown fix and was removed; strict draw/selection checks remain.
A sample of the discarded experiment places the later fatal loop in
`004A2DE0 -> 004A5DD4 -> 00490E78`: its LFB lock fails after shutdown.
This is diagnostic evidence from an experimental binary, not the current
runtime behavior (`work/glide-quit-fatal-sample/`).
Evidence is in `work/glide-menu-quit-yes/`, `work/glide-menu-quit-fixed/`,
`work/glide-menu-quit-lifecycle/` and `work/glide-menu-quit-control/`.

One independent API correction is retained: `grSstControl` succeeds without
accessing graphics resources when no window is initialized. This matches the
[Glide 2 implementation](https://github.com/sezero/glide/blob/glide-devel-sezero/glide2x/sst1/glide/src/gsst.c).
The service test verifies late activation after shutdown and an explicit
Init/Open/Clear/LFB-read cycle afterward. This does not qualify either Quit or
WM_CLOSE. The final ten-test result is `work/glide-shutdown-sample/final-ctest.log`.
The final service lifecycle also passes ASan/UBSan
(`work/glide-shutdown-sample/final-sanitize-ctest.log`).

Pause / Down / Enter / Left / Enter returns from the race to the main menu;
the 33-second game capture confirms the menu (`work/glide-return-main/return-result.png`).
A subsequent WM_CLOSE at 37 seconds still reaches the 48-second watchdog.
Returning to the menu is therefore a verified bounded navigation result, not
a successful shutdown workaround. The command and results are retained in
`work/glide-return-main/result.json`.


The preliminary unoptimized (`-O0`) guest also fails a movie-close run when POD
probes frame 5979 of the 5,981-frame `intropod.AVI`. Its crash report shows stack
guard protection in a long lifted Indeo continuation chain. The same final
two-frame offline decode succeeds with the software `-O1` binary and crashes
with the retained Glide `-O0` probe, independently of game rendering. The standard `-O1` Glide build decodes both final frames byte-identically to
the software build (399,360 bytes, SHA256
`fffcf39148301d8d4c6b3f5187d42bc66073cc9da919996b78e3ef3ab51d8211`).
The movie-enabled game run then completes WM_CLOSE/ExitProcess(0) at 24 seconds
without the prior stack fault (`work/glide-o1-movies.log`). This is bounded
evidence for that failure/configuration, not a proof of general stack safety. No larger stack, generated-code
edit or toolkit change is used to hide this failure. Evidence is under
`work/glide-codec-compare/` and `work/glide-movie-crash-summary.json`.

## Working Windows reference

The user's installed GOG POD GOLD is a working 3dfx reference. Its executable
and Glide DLL hashes match the previously extracted files. The installed path
has compatibility layers including `DWM8And16BitMitigation`; these settings
were inspected without modification. An x86 module query confirms the installed
`glide2x.dll`, its local `d3dx9_43.dll` and Windows `d3d9.dll` loaded in the game.
The GOG wrapper SHA256 is
`f7701243b2829aaa2bcc01c334c0ed85434b7be5e532bb4030599eaf9b320525`.

CUA navigated One Player / Single Race / Beltane / Gamma and observed the car
menu and race. The Player 1 car-status outline is blue on Windows as on Metal;
empty HUD bars without acceleration also occur on Windows. The orange outline
in the attract demo belongs to another driver and is not a channel-order test.
The initial Windows inspection left the game open at its pause menu; a later
shutdown comparison used a newly launched installed-game process.

The later installed-game run repeats Beltane/Gamma, enters pause and confirms
Quit Game / Yes. It leaves a black game-sized region over Explorer and PID
11180 remains alive after a 15-second WaitForExit (`Responding` is true). A
second trial, launched with a temporary interactive task at the same
installed path, reproduces the black region and remains alive before cleanup.
These trials do not qualify a clean Windows exit or establish a common cause
with the native failures. A separate diagnostic dump attempt times out and
produces no usable stack; it is not used as thread evidence. The owned reference
process is stopped afterward as cleanup, not counted as a successful Quit.
WOW64 register/stack-word snapshots succeed before Quit; the after-Quit
collection does not finish and is not used as an unwound stack or evidence of
a common wait chain. The temporary task and collectors are removed/stopped;
the owned game processes are stopped as cleanup. See
`work/windows-reference/quit-observation.json` and the corresponding logs.
No wrapper, game configuration or compatibility-layer setting is changed.

This reference is a visual observation of specific scenes, not a pixel-aligned
comparison or proof of original Voodoo hardware output. No replacement DLL,
installer, compatibility-setting or display-mode change was applied to the
working installation. Records are in ignored `work/windows-reference/`:
`installed-reference-observations.json`, `active-reference.json` and
`active-reference-modules-x86.json`. Screenshots were observed inline, not saved.

Earlier failures belonged to an isolated extracted copy and must not be
generalized to the installed game or VMware. Alternative dgVoodoo/D3D11/WARP
attempts did not supply a usable reference; they are excluded from visual
qualification. The original wrapper in that isolated folder was restored and
verified by hash, and temporary tasks removed. No alternative wrapper is
linked or distributed with the native port.

## Other renderer work and sources

The inspected pcrecomp `d3d.c` implements IDirect3D3, while POD's alternate
Direct3D executable contains original IDirect3D GUID bytes. Byte presence is
not an observed COM call. The [DX6 work](https://github.com/sp00nznet/pcrecomp/pull/154)
excludes older execute-buffer interfaces. The [Metal proposal](https://github.com/sp00nznet/pcrecomp/pull/163)
and [Vulkan proposal](https://github.com/sp00nznet/pcrecomp/pull/187) were
unmerged at initial inspection and report Theme Park World, not POD coverage.
No automatic toolkit upgrade or Direct3D support follows from them.

API layouts were checked against the primary
[Glide header](https://github.com/sezero/glide/blob/glide-devel-sezero/glide2x/sst1/glide/src/glide.h)
and [Wine Direct3D header](https://github.com/wine-mirror/wine/blob/master/include/d3d.h).
Texture coordinate ranges were checked against OpenGlide's
[aspect table](https://github.com/voyageur/openglide/blob/master/OGLTextureTables.cpp).
W encoding was checked against MAME's
[Voodoo rasterizer](https://github.com/mamedev/mame/blob/master/src/devices/video/voodoo_render.cpp)
and [register conversion](https://github.com/mamedev/mame/blob/master/src/devices/video/voodoo.cpp).
No third-party renderer implementation was imported into this fork.

## Software / Glide shutdown signal comparison

Both variants register `MSG_DesInitSnd`. The request and acknowledgement are
also checked against the original PE instructions, not only the lifted C.

| Operation | Software PODMMX | Glide PODX3DFX |
| --- | --- | --- |
| Main cleanup entry | `004A02D0` | `004A82B4` |
| Audio-stop request | `004BBD6C` | `004C339C` |
| Audio worker | `004BB5B8` | `004C2BE8` |
| Stop-message slot | `00712EC8` | `007081D8` |
| Audio thread ID slot | `00712EC0` | `007081D0` |
| Rendering semaphore slot | `006E6E08` | `006DC504` |
| Worker acknowledges stop | `004BB9B4: SetEvent` | `004C2FE4: SetEvent` |

The caller creates an initially unsignaled event E and posts
`PostThreadMessage(audio_thread, MSG_DesInitSnd, E, 0)`, then waits on E.
The audio worker reads GetMessage, recognizes that registered message,
deinitializes audio and signals the event from wParam. Metal/SDL does not
acknowledge this protocol.

Glide cleanup acquires the rendering semaphore S before terminating the
renderer, stopping the timer and requesting audio shutdown. It still holds S
while waiting for E. The corresponding software cleanup has no equivalent
semaphore acquisition at entry. Both workers acquire their rendering semaphore
around waveOutWrite. A buffer in progress can therefore produce this cycle:

```text
main:  holds S -> waits E
sound: waits S -> cannot read MSG_DesInitSnd -> cannot signal E
```

This matches the captured Glide failure: thread 260 waits on event `0138`,
thread 268 waits on semaphore `0108`, and the queue contains `C002` (the
registered stop ID in that run) with wParam `0138`. Window close/destroy messages
have already arrived. The audio receiver is blocked before reading its queue.

An isolated causal trial releases S **after renderer termination**, leaving the
normal audio message, deinitialization and SetEvent unchanged. The effects/CD
race then reaches ExitProcess(0) in 26.080 seconds, versus the production test's
41.029-second watchdog. There is no forced acknowledgement or host cutoff.
The temporary runtime change is removed and the original runtime rebuilt.
This verified the cause of that observed WM_CLOSE wait and informed the
permanent service correction below. Evidence: `work/shutdown-comparison/glide-release-result.json`,
`glide-release.log`, `original-disassembly.json` and `restored-build.log`.

The in-game Quit callback has a separate difference. Software `0049CB20` sends
WM_DESTROY via SendMessageCallbackA and returns. Glide `004A3530` first calls
grGlideShutdown, then sends the same message and returns into the still-running
render iteration. The native trace records shutdown and later board selection
on rendering thread 264. Releasing the audio semaphore alone does not fix this
early graphics teardown. A complete correction must stop rendering before
freeing its resources, and let audio acknowledge stop without the main thread
holding the semaphore it needs.


## Installed POD Glide lifecycle correction

`src/portable/glide_compat.c` is a separate game-service library linked only
by the Glide target. Its register/bind entry points preserve the original HLE
and Glide delegates. The addresses above belong to the checked PODX3DFX image;
no generated lift, pinned toolkit source or Windows host is edited.

The policy tracks successful waits and releases of POD's rendering semaphore.
After the actual renderer handle has been terminated successfully and reports
done, it releases S only if the calling cleanup thread acquired it. Unrelated
handles, failed waits/termination and repeated termination do not authorize a
release. The audio message, deinitialization and acknowledgement remain guest
code; no event is force-signaled.

The early Quit shutdown is deferred. After the callback queues WM_DESTROY,
the renderer yields at its next semaphore acquisition instead of immediately
reentering the rendering section. This leaves S available to the polling
cleanup waiter. The worker remains at a cooperative HLE checkpoint until the
game terminates it. Graphics resources stay alive through the main thread's
remaining board-selection cleanup, then the original final WinClose/Shutdown
releases them. Destroying the GPU immediately at TerminateThread is too early:
main cleanup still selects the board afterward. Strict Glide validation remains
active; late drawing is not silently discarded.

The asset-free `pod_glide_lifecycle` test uses real HLE guest threads,
semaphores, a queued stop message and a receiver-generated acknowledgement.
A fake graphics teardown checks that it cannot run while the renderer is live.
It also verifies unrelated and failed termination, retained graphics after
renderer termination, and absence of duplicate semaphore release. Existing
GPU/service lifecycle coverage remains separate from this title policy.

On the corrected standalone, effects/CD race WM_CLOSE exits 0 in 26.139 seconds
and race Quit Game / Yes exits 0 in 31.266 seconds. Both logs contain the
original ExitProcess(0), with non-silent effects submitted and CD PCM submitted;
neither uses --exit-after or expires its watchdog. The default software build
also passes eight CTests and both equivalent race close/Quit checks. Timings
include scripted delays and startup; they are not shutdown latency benchmarks.
Commands, state overlays and results are isolated under `work/shutdown-fix/`.
These results qualify the tested routes, not arbitrary gameplay or library
unload. The current process-global HLE/Glide lifecycle still requires further
work for a future pull-driven Libretro frontend.


A repeated headless Quit run with CD disabled also exits through ExitProcess(0)
in 31.296 seconds. A CUA windowed run independently observes One Player /
Single Race / Beltane / Gamma / race / pause / Quit Game / Yes, and the
recorded process then reaches ExitProcess(0) without watchdog or host cutoff
(`work/shutdown-fix/ui-observation.json`, `interactive.log`). This is a real
window/menu check, not a timed key script alone. The first timed visible trial
remained at the main menu and is explicitly excluded from shutdown validation.

That interactive check exposed an unrelated missing GetClipCursor import on
focus changes. POD Glide's compatibility policy now returns the unconstrained
virtual desktop rectangle, consistent with the current HLE's no-op ClipCursor;
it does not implement physical pointer confinement. The subsequent focused
menu/race/quit run completes without that import failure. All eleven Glide
CTests and eight software CTests pass. The lifecycle and Glide-service tests
pass ASan/UBSan with `DYLD_FALLBACK_LIBRARY_PATH=/opt/homebrew/lib`,
`ASAN_OPTIONS=detect_leaks=0` and `UBSAN_OPTIONS=halt_on_error=1`
(`work/shutdown-fix/sanitize-final.log`). Without the documented SDL3 fallback,
sdl2-compat stalls in a library-initializer error dialog before main; those
attempts are not treated as tests of the shutdown code. Leak checking is not
qualified by these runs.


## Additional integration checks and menu-close ordering

The final audit exposed another ordering cycle in the original Glide guest.
WM_CLOSE (`004A9F86`) waits on S before setting the close flag at `006DC538`.
After returning from the race, the nested menu dispatcher (`0042DA31`) can
retain S until it observes that flag. The pre-fix run visibly returns to the
main menu, receives WM_CLOSE, then expires its 48-second watchdog. A native
sample places the UI in the window procedure's S wait, audio in its S wait,
and the renderer still drawing that menu (`work/integration-final/return-sample.txt`).

The POD-specific service proxies only registration of the known game window
procedure (`004A8754`); it copies WNDCLASSA instead of modifying caller data.
For an actual WM_CLOSE on the main game window, it publishes the same close
flag before delegating to the original procedure. That lets the menu unwind
and release S normally. It does not invent a close request, release a semaphore
owned by another thread, acknowledge audio, or terminate the host process.
The synthetic lifecycle test now exercises this dependency with a menu worker
holding S until the flag, checks that ordinary messages preserve their
arguments/results, and that class registration preserves guest input data.

With this final extension, race / return to main menu / WM_CLOSE reaches the
original ExitProcess(0) in 42.087 seconds (close requested at 37 seconds), versus
the prior 48-second watchdog. The movie-enabled close check also exits through
ExitProcess(0) in 24.161 seconds. Logs, commands and separate state overlays are
under `work/integration-final/`. Final race WM_CLOSE and Quit regressions are
recorded separately there. All eleven CTests and the lifecycle/service
ASan/UBSan checks pass again. The default software target does not link this
POD Glide policy and its prior eight-test and real-game regression results
remain applicable.

A CUA check on the final updated bundle observes fullscreen via F11, a rendered
Beltane/Gamma race, pause, and Quit Game / Yes, then confirms ExitProcess(0)
without watchdog or host cutoff. See `fullscreen-observation.json` and
`interactive-result.json` in the same evidence directory. Screenshots were
observed inline, not saved. These checks close this bounded integration audit;
extended play, exhaustive renderer fidelity, physical controls, listening and
clean-machine distribution remain separate qualifications.


The pre-commit cleanup removed regenerable temporary executables (including the
old unoptimized probe), the UI test bundle, standalone one-off sanitizer
binaries, and the downloaded dgVoodoo archive. Current builds, game/disc data,
state overlays, logs, stack samples and diagnostic media were retained. The UI
bundle's plist is preserved as `work/integration-final/GlideUITest-Info.plist`;
its executable can be copied again from the current Glide build. Historical
commands referring to that temporary bundle need it recreated before reuse.
The local cleanup inventory is `work/integration-final/cleanup.json`.
