# Frontend boundaries

The native ARM64 target is structured to allow later reuse by a Libretro
frontend. Correct standalone recomp behavior takes priority; extraction of
interfaces must preserve it and be checked in the standalone application.
It is still a standalone development application. No Libretro ABI,
frame-stepping contract or save-state support is implemented here.

Portability of the standalone to other systems, including Linux, is also a
project requirement. The shared services support both directions: other
standalone platforms and a future Libretro frontend. SDL input/output adapters
belong to the standalone frontend and can be reused across operating systems;
they should not acquire macOS-specific application or packaging policy.

## Standalone platform boundaries

Keep Cocoa/main-thread requirements, `.app` construction, platform stack-size
adjustments and platform dependency discovery outside guest services. Resolve
data/state locations in the frontend rather than baking host-specific paths
into protocol modules. Preserve the existing Windows host while evolving the
portable standalone; build success on macOS does not establish Linux support.

The top-level CMake already routes non-Windows builds to `src/portable`, but
Linux build/runtime verification remains outstanding. Check POSIX APIs,
compiler assumptions, SDL/SDL_ttf discovery, threads, data paths and output
device behavior on the target. Report architecture and actual test coverage
separately. A relocatable package is a further check beyond a local build.

## Current ownership

| Module/target | Responsibility | Frontend dependency |
|---|---|---|
| `pod_guest` | Generated POD and codec execution | HLE machine/dispatch; no frontend entry point |
| `pod_hle` | Pinned, locally patched Windows services | Global machine state and existing SDL presenter/timers |
| `pod_services` | POD-specific guest services; static library without `main()` | HLE and selected PCM backend; codec still uses SDL |
| `compat.c` | Disc/INI policy, Windows fallbacks and restricted DirectDraw modes | Guest ABI only |
| `video_present.c` | DrawDib to GDI/DirectDraw primary surface | HLE framebuffer, optional codec pacing |
| `codec.c` | Read-only AVI/VfW and lifted disc codec | SDL ADPCM conversion and current blocking presentation wait |
| `dinput.c` | Guest DirectInput state, buffered transitions and events | HLE; no SDL key enum or event polling |
| `audio.c`, `cd_audio.c` | waveOut and MCI protocol, PCM queue accounting | Opaque PCM output contract; CD CUE/source reading |
| `pcm_output.h` | Exact-format queue ownership and consumption contract | No SDL or guest callbacks |
| `pcm_sdl.c` / `pod_pcm_sdl` | Independent SDL output queues | Standalone frontend backend |
| `input_sdl.c` | SDL scancode to DIK translation | Standalone input adapter |
| `host.c` | CLI, paths, SDL/Cocoa lifecycle, scripts, watchdog, captures, worker startup | Standalone application only |

`pod_compat_register(videos)` registers POD overrides before
`recomp_host_init()`. `pod_compat_bind()` then resolves the original HLE
delegates. This preserves the override ordering used by the native port.
The library intentionally does not own application shutdown or process exit.

A future frontend can submit a keyboard transition through
`pod_keyboard_dik(key, down)` and release held keys through
`pod_keyboard_release()`. The key is PC/AT set 1 with the DirectInput extended
bit; it is not an SDL or Libretro key code. Call these services with the
machine lock held. The standalone adapter remains separately compiled.

## Remaining boundaries for Libretro

1. Introduce an explicit session lifecycle and bounded guest execution/yield
   protocol. Today the guest runs on workers and waits for Windows messages;
   a call to `recomp_host_run()` is not a single video frame.
2. PCM queue operations are now isolated through `pcm_output.h`; the standalone
   explicitly links `pod_pcm_sdl`. A pull-driven frontend still needs effects
   and CD mixing/resampling into its requested output format,
   plus queue positions and guest completion notifications that follow
   consumed samples. Replacing SDL_QueueAudio with a callback alone is
   insufficient. Keep guest callbacks on the machine thread.
3. Export the HLE primary framebuffer with a defined lifetime/pixel format.
   Keep DrawDib conversion independent of the final presenter; remove native
   window requirements from HLE setup for a frontend that owns its display.
4. Replace wall-clock pacing and sleeps with frontend-controlled advancement.
   Silent-movie waits currently release the machine lock, but still block a
   guest worker. They are not an implementation of a frontend frame scheduler.
5. Replace process-global initialization, current-directory changes and
   ExitProcess teardown with explicit reset/unload behavior. The present
   shutdown freezes surviving workers through process exit and cannot safely
   unload a shared library. Multiple sessions are not supported.
6. Define serialization only after ownership of machine memory, threads,
   handles, codec state, pending audio and external file positions is clear.
   Do not advertise save states or rewind based only on serializing RAM.

These are staged implementation constraints, not reasons to add a generic
abstraction layer to every service now. New work should respect the concrete
module boundaries and expose only interfaces the project actually uses.

## Validation of this split

The native rebuild and eight CTests pass (including PCM, per-thread quit and sent-message callback tests). The keyboard test submits the same
transition through the SDL adapter and the direct DIK API, checks duplicate
suppression, and ignores out-of-range DIKs. A bounded headless video/CD run
exercises the extracted registration, delegate binding and DrawDib path;
see `work/modular.log`. The final instrumented 32-second movie/CD run
also exited 0 (`work/presentation-loops.log`). Independent presentation
sequences distinguish credit repetitions from a single slow traversal.
These checks do not establish Libretro compatibility.


## PCM output contract

The frontend links an implementation of `pcm_output.h`. Each queue accepts
an exact PCM format and owns a copy of submitted bytes. Its remaining byte
count decreases only as samples are consumed; pause freezes consumption,
clear discards the remainder, and close releases the queue. No operation
invokes guest callbacks. waveOut retires headers during the service pump;
MCI uses the same remaining-byte count for CD positions and notifications.

`pcm_sdl.c` implements this for the standalone and balances its SDL audio
subsystem references per queue. Backend selection uses ordinary linking;
there is no mutable global callback table or runtime backend switching.
The `pod_audio_queue` test links the same waveOut service with a manually
consumed queue. It checks independent guest-buffer ownership, frozen pause
positions, partial consumption, header completion order, reset, and backend
open/write failures. This is a test backend, not a Libretro mixer.

The codec's SDL WAV conversion, HLE's SDL support, blocking MCI wait and
process-global lifecycle still need separate treatment for a future core.


## Platform-specific packaging

`tools/macos/package_app.py` owns Mach-O dependency collection, load-path
rewriting and local ad-hoc signing. These operations stay outside game services
and the reusable standalone SDL adapters. A moved bundle is verified using
explicit content/state paths; default path discovery and Finder setup are
still development limitations. No Linux port is started by this packaging work.


## Guest display clock

DirectDraw flip scheduling belongs to the runtime display service. The current
synchronous HLE models a 60 Hz virtual display using a monotonic host clock and
releases the machine during its wait, including in headless runs. The standalone
selects an SDL presenter without a second physical VSync wait; its window does
not define guest simulation time. A future pull-driven frontend must replace
this blocking scheduling contract deliberately, alongside the existing guest
thread/lifecycle limitations, rather than reusing it inside `retro_run`.
