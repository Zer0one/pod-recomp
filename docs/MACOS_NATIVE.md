# Native macOS development prototype

This target compiles the existing POD lift for ARM64 against pcrecomp's
`runtime/win32hle`, using SDL2 for presentation. It does not run Wine,
CrossOver, Rosetta, or the original x86 machine code. The Windows host remains
available through the existing Windows build.

## Current verification summary (2026-11-11)

- Native ARM64 build, eight synthetic CTests, and bounded standalone effects/CD
  and movie/CD regression cases pass.
- The native Esc/YES/Enter exit route reaches ExitProcess(0) before its
  scheduled close. A per-thread quit fix and SendMessageCallbackA support are
  included in the local runtime overlay.
- The local packaged app runs after relocation with five bundled runtime
  libraries and external game/state paths. Finder setup and a clean-machine
  run remain unverified.
- Listening, physical controls, extended play, general mouse/menu navigation,
  and full-game speed/audiovisual timing remain unqualified. The bounded
  race-clock comparison below covers one scene. Preparing the
  code for future standalone platforms or Libretro does not start those ports.

The sections below retain the individual checks and their limits.

## Build

Use the supported POD Gold executable and Indeo DLL described in
[BINARIES.md](BINARIES.md), extracted into `game/`. All game files, catalogs,
generated C, state, and runtime staging remain ignored. No disc files are
included in the fork.

Requirements: Apple command-line tools, CMake, Ninja, pkg-config, SDL2,
FreeType, Python with `pefile` and `capstone`. The local Python environment
uses pefile 2024.8.26 and capstone 5.0.9. The tested SDL2 API version is 2.32.74, supplied locally by Homebrew
`sdl2-compat` (the application links that compatibility library).

From the repository root:

```sh
git clone https://github.com/sp00nznet/pcrecomp.git work/pcrecomp
git -C work/pcrecomp checkout a255839d0b78688aa98e677f05d0f0f74710d8aa
# If SDL2_ttf is not installed, build the official source locally:
git clone --branch release-2.24.0 https://github.com/libsdl-org/SDL_ttf.git work/SDL_ttf
python3 -m venv work/venv
work/venv/bin/python -m pip install pefile==2024.8.26 capstone==5.0.9
PCRECOMP="$PWD/work/pcrecomp" work/venv/bin/python tools/catalog.py
PCRECOMP="$PWD/work/pcrecomp" work/venv/bin/python run_lift.py
cmake -S . -B build/native -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build/native -j 6
ctest --test-dir build/native --output-on-failure
build/native/pod
```

Skip cloning directories that already exist. The tested SDL_ttf revision is
`2a891473eaf05ba1707a4b7913e6c4db7de7458a`; the runtime staging script
requires the exact pcrecomp revision above. It copies and patches the runtime
under `build/native/pcrecomp-runtime`, leaving the toolkit checkout unchanged.
`tools/macos/patches/win32hle.patch` contains the reviewable runtime changes.
Generated guest code defaults to `-O1`; `POD_GEN_OPT` can override this.

On macOS the build also creates `build/native/POD.app`. This is a development
bundle: it uses absolute project data/state paths and build/Homebrew dynamic
libraries. It is not a relocatable or distributable application package.

## Runtime and limits

Default data: `game/`. Default writable state: `work/native-state/`, with
`UbiSoft/Ubi.ini` created on first run. `--game DIR` and `--state-dir DIR`
override these paths. The overlay keeps game data separate from writes.
The state directory's parent must already exist.

Movies are skipped by default. PCM effects now go through an asynchronous
SDL queue, with pause/restart/reset, buffer retirement, position queries,
volume, and callback delivery. CD music uses a separate SDL PCM queue when a
supported CUE is available (see below). Headless runs force
SDL's dummy audio driver; they do not drive the speakers. `--audio-capture
FILE.wav` records the PCM submitted to SDL (up to 64 MiB), not a recording
of the speakers. Playback at the physical output and listening remain
unqualified. Keyboard input is
implemented through a guest-ABI DirectInput device with a buffered queue,
notification events, and key release on focus loss. Mouse window messages
are supplied by the existing HLE; joysticks and controllers are not provided
by this prototype.

The guest runs on a worker while SDL/Cocoa pumping stays on the main thread.
The local runtime patch adds Watcom PE section handling, DirectDraw surface
padding, cooperative thread suspension/termination, window activation and
class updates, and an opt-in Pentium MMX CPU profile. On ARM64 the virtual
TSC is 200 MHz, consistent with that profile and within POD's calibration
range. This is not cycle-accurate CPU emulation.

Useful diagnostics:

```sh
build/native/pod --dry-run
build/native/pod --headless --watchdog 20 --exit-after 9000 \
  --key ESC@6500 --key ENTER@7000 --key ENTER@7800 \
  --capture work/native-track.bmp --capture-ms 8500
```

`--key NAME@MS[+HOLD]` schedules SDL scancode transitions relative to host
startup; the default hold is 150 ms. Times depend on startup speed and on
skipped media. `--capture` captures one framebuffer at or after the deadline. With
`--capture-every MS`, it writes up to 64 numbered `PATH.000.bmp` samples,
logging the elapsed host milliseconds and framebuffer sequence for each.
Sampling does not validate the screen's content. `--trace` enables verbose HLE
logging. `--watchdog` exits with 124 on timeout. `--exit-after` is a host test
cutoff, not evidence of choosing Quit in the game's menu.
`--close-after MS` instead posts `WM_CLOSE` to POD's window and lets the
game run its own cleanup. Pair it with a watchdog when testing shutdown.

For a missing indirect target reported by an execution log, use the existing
upstream discovery loop before rebuilding:

```sh
work/venv/bin/python work/pcrecomp/tools/disasm/seed_from_log.py \
  work/native-gui.log game/PODMMX.EXE --seeds work/seeds.json \
  --functions work/functions.json
PCRECOMP="$PWD/work/pcrecomp" work/venv/bin/python tools/catalog.py
PCRECOMP="$PWD/work/pcrecomp" work/venv/bin/python run_lift.py
cmake --build build/native -j 6
```

The seed list is binary-derived and stays ignored in `work/`. The initial
window-close run exposed a missing alternate entry; a fresh catalog without
the observed seed can still reproduce that original failure. The seeded
local lift contains 11,865 dispatch entries, zero lift errors, and 1,066
bodies without a clean terminator; these counts are not execution coverage. Never treat an
unresolved-target log followed by process termination as a successful exit.

## Verification

Experimental media diagnostics:

```sh
build/native/pod --codec-probe --watchdog 15
build/native/pod --decode-test game/DATA/BINARY/VIDEO/LOGO.AVI work/logo.bgr --watchdog 30
build/native/pod --videos --watchdog 30 --close-after 20000
```

The offline decoder runs the lifted codec through its installable-driver ABI;
its output is raw bottom-up BGR24. The public ABI follows Microsoft's
[Video Compression Manager reference](https://learn.microsoft.com/windows/win32/multimedia/video-compression-manager-reference).
The container reader and AVI shims are original host code. No FFmpeg code
is embedded; its separate executable is only a verification reference.
`--videos` enables the experimental read-only AVI/VfW host for POD's existing
player. It exposes the video and audio streams, preserving audio-track
selection. SDL's existing WAV loader converts a selected ADPCM stream to
PCM; the returned stream format/rate/length describe those PCM samples, so
POD's waveOut-driven movie clock keeps working. This replaces the Windows
wave mapper's compressed-audio conversion, rather than introducing a new
ADPCM decoder or FFmpeg runtime dependency. It is not yet the default.
Clips without audio now wait for their AVI timestamps at DrawDib presentation.
This leaves decoding, seeking, and audio-clocked clips unchanged. The logo
no longer completes in a fraction of a second; complete movie timing,
including physical audiovisual synchronization, remains unqualified.
The macOS guest threads use an 8 MiB host stack: the lifted codec's assembly
continuation chains overflowed the default 512 KiB pthread stack in LLDB.

Synthetic tests cover the Watcom loader rules, DirectInput queue/poll/peek
semantics, thread suspend/resume/terminate, CPUID, and visible-window
activation. Three existing HLE selftests exercise the loader, host, and pump.
They do not establish game conformance. Audio tests additionally check that
paused buffers are not completed immediately, pending headers cannot be
unprepared or closed, playback advances byte/sample/millisecond positions,
reset retires buffers, and function callbacks are delivered.

On 2026-11-11 the updated ARM64 build passed all five CTests (0.79 seconds),
including the new synthetic CD suite described below.
Native SDL window presentation and attract-mode rendering have been observed.
A bounded headless run with Esc at 6500 ms and Enter at 7000/7800 ms reached
the Beltane track selection screen, captured in `work/native-track.png`.
The final development app also displayed the Beltane screen in the native
macOS window, using the same scripted route (`work/native-final-gui.log`).
Both the headless WM_CLOSE run and the final graphical run exited with code
0 without a watchdog timeout (`work/native-close.log`,
`work/native-final-gui.log`). The former logged the game calling
ExitProcess(0). No host `--exit-after` cutoff was used for either shutdown
check. The final GUI run also had a scheduled WM_CLOSE, so it does not
isolate the accessibility close-button action.

This validates scripted menu navigation and window-message shutdown, not
driving a race. General arrow/mouse navigation and the game menu
Quit command were unqualified at this stage: the attempted Down-key route
did not select Quit and timed out. A later native Esc/YES route is verified
in the shutdown follow-up below. Extended
play, Windows oracle comparison, physical controls, listening, and complete
audiovisual timing remain outside the verified scope. See the local logs and captures
under `work/` for individual runs; these are development evidence, not
redistributed game inputs.

## Autonomous audio/race and decoder evidence

`work/autonomous-car.png` shows Gamma car selection. The scripted route
then reached a race with Player 1 (`work/autonomous-start.png`). Holding Up
for six seconds produced a later in-race capture at 63 MPH
(`work/autonomous-effects.png`); this is automated input response, not
physical-device or handling qualification. Game speed/frame pacing remains
unqualified.

`work/autonomous-effects.wav` contains 510,840 stereo frames at 22,050 Hz,
16-bit PCM (23.17 seconds), with 934,497 nonzero samples, RMS 4,794.01 and
peak magnitude 32,768. The run used the dummy device and exited normally.
The capture proves non-silent PCM submission, not audible output or quality.

`work/logo-decode.log` records all 60 LOGO.AVI frames decoded natively. The
existing upstream `tools/verify_indeo.py` correlation check against the
independent FFmpeg decoder returned 0.999207. A representative decoded frame
is `work/logo-lift-frame.png`. No original x86 codec execution was performed
on this Mac.

## Media integration verification

Run the full native codec comparison with:

```sh
python3 tools/macos/verify_media.py
```

This requires the six owned AVI files and separate `ffmpeg`/`ffprobe`
verification tools. Each video's raw frames are removed after comparison.
The JSON results/logs remain under `work/media-verification/`. This is a
licensed-data integration check, separate from the synthetic CTests.

The local campaign passed all six files and all 13,722 frames. Counts and
raw byte lengths match the AVI metadata; final-frame seeks are byte-identical
to the same lifted codec's sequential decode. Correlations against the
independent decoder are:

| Video | Frames | Correlation |
|---|---:|---:|
| CREDITS | 131 | 0.998372 |
| FINPOD | 1,413 | 0.997572 |
| FINPOD2 | 156 | 0.991615 |
| INTROCD | 5,981 | 0.981360 |
| INTROPOD | 5,981 | 0.981360 |
| LOGO | 60 | 0.999207 |

These are independent-decoder comparisons, not a Windows original-code
oracle. The check's explicit minimum correlation is 0.97, allowing for the
known RGB conversion differences documented by upstream.

`work/video-audio.png` shows the intro through POD's player and the native
DirectDraw framebuffer. The ADPCM conversion's first 666,242 submitted
16-bit samples are byte-identical to FFmpeg's first audio stream, with zero
maximum sample error (`work/media-verification/audio.json`). This covers
that captured prefix, not the full soundtrack or all languages.

The native graphical run `work/media-native.log` opened CoreAudio, displayed
the intro, transitioned to the main menu under automated input, and exited
with code 0. `work/media-native.wav` is the submitted PCM capture; no
speaker/listening result is claimed. A screenshot from the framebuffer is
`work/media-native.png`.

An early longer transition/closure run with a WAV capture exited with a bus
error. Teardown now retains the guest-machine lock while closing SDL devices
and captures. Three subsequent 26-second runs passed, but later CD runs
exposed another shutdown race: a guest worker still accessed the CRT file
descriptor table after its owner freed it. The runtime now makes
TerminateThread wait for cooperative termination before returning, including
workers blocked in GetMessage. A checkpoint after blocking waits is guarded
against recursive entry. Synthetic coverage checks immediate completion of
both a running worker and a blocked-message worker. Three additional CD
stress runs exited normally after this correction (see below). These are
bounded regression checks, not proof of indefinite stability.

The expanded runtime patch also sizes a fullscreen virtual window with its
DirectDraw mode. Previously the virtual client area stayed 1x1. A synthetic
800x600 mode/client-rectangle check covers this. DrawDib now uses the existing
GDI bitmap/StretchBlt path for 24-bit frames, handles the API's NULL-bits
convention, copies into the active DD primary without borrowing its lifetime,
and signals that the primary changed. Movie rewinds and final-frame probes
start at their preceding indexed keyframe.

Remaining: physical controls/listening, complete movie timing,
precise game-speed validation, general menu/mouse navigation,
extended play, and a relocatable application bundle. The later scripted
movie-to-race attempt did not establish Player 1 gameplay; its final capture
showed attract mode. Earlier no-video Player 1 evidence is retained above.


## CD music and presentation follow-up (2026-11-11)

The macOS host now serves MCI cdaudio requests from the user's two-file POD
Gold CUE: track 1 must be MODE1/2352, track 2 AUDIO, with separate quoted
FILE entries and INDEX 01. BINARY and MOTOROLA byte orders are supported.
Other disc layouts are rejected rather than guessed. This changes only the
native host; the upstream Windows host's CD roadmap still applies there.

```sh
build/native/pod --cd-cue 'work/disc/POD Gold (Europe) (En,Fr,De,Es,It).cue' \
  --cd-capture work/music.wav --watchdog 30 --close-after 20000
```

Exactly one CUE in the project's `work/disc/` is selected automatically.
Use `--cd-cue PATH` to select explicitly, or `--no-cd` for the previous silent
compatibility path. Source images remain read-only and ignored by Git.
`--cd-capture FILE.wav` records submitted music separately from effects,
with a 64 MiB limit; neither capture records the speakers.

The supplied disc has 229,580 data sectors and an audio INDEX 01 at 149
sectors inside the second file. Playback excludes that stored pregap and
exposes 65,955 playable sectors (879.4 seconds). Music is 44,100 Hz, 16-bit
stereo on a separate SDL device. MCI implements the game's track queries,
time formats, play/stop/pause/resume/seek and window notifications; end of
playback is reported to the guest rather than forcing an invented loop.

Evidence:

- `pod_cd` creates and removes synthetic two-file discs in both byte orders,
  checking pregap exclusion, TOC/length, position, pause/resume, completion
  and abort notifications, plus captured sample values.
- `work/cd-verification.json`: 2,239,104 submitted PCM bytes match the source
  exactly after INDEX 01 (559,776 stereo frames). Listening was not verified.
- `work/cd-stress/results.json`: two effects-plus-CD runs and one
  movies-plus-CD run exited 0, without a watchdog timeout.
- `work/cd-coreaudio.log`: a bounded graphical run opened both 22,050 Hz
  effects/movie PCM and 44,100 Hz CD PCM through CoreAudio and exited 0.
  This establishes device opening and submission, not audible quality.
- `work/cd-paced.log`: the active logo context lasted 1,930 ms, versus the
  earlier sub-second run. The initial credits timing combined repeated
  passes in one frame context; it did not measure one slow traversal.
  `work/presentation-loops.log` separates the passes: frames 0..121,
  0..124 and 0..126 took 4,046/4,146/4,211 ms against AVI timestamp
  intervals of 4,037/4,137/4,204 ms. This supports silent-clip pacing on
  this host, not complete audiovisual synchronization or game-speed
  qualification. `work/presentation-timing.json` retains those measurements.
  Offline frame decoding and seek behavior are unchanged by the new wait.


## Module ownership

The frontend split is described in [FRONTEND_ARCHITECTURE.md](FRONTEND_ARCHITECTURE.md).
POD compatibility policy and DrawDib presentation are separate from the
standalone launcher and compiled into `pod_services`. SDL keyboard mapping
is a separate adapter; DirectInput also accepts DIK transitions directly.
This prepares reuse by a future Libretro host without claiming that global
HLE state, audio output, timing or unload behavior are already frontend-neutral.


## Frontend PCM boundary and standalone regression checks

waveOut and MCI now use `pcm_output.h`: an opaque exact-format PCM queue
with owned copies, remaining-byte counts, pause, clear and close. Neither
service includes SDL headers or opens SDL devices. The standalone explicitly
links `pod_pcm_sdl`; the codec and HLE retain their own SDL dependencies.
Headless audio-driver selection belongs to the standalone launcher.

Six CTests passed in 0.82 seconds. The new `pod_audio_queue` case links the
production waveOut service to a manually consumed queue and checks pause,
positions, copied buffers, completion ordering, reset, and injected output
failures. This does not implement mixing/resampling for Libretro.

Standalone checks after the extraction:

- `work/pcm-coreaudio.log`: movies, effects and CD opened CoreAudio; exit 0.
- `work/pcm-repeat.log`: headless movies, effects and CD, with both PCM
  captures enabled; exit 0.
- `work/pcm-menu-close.log`: scripted intro/credits skip followed by
  WM_CLOSE; exit 0 with both PCM captures enabled.
- `work/pcm-validation.json`: CoreAudio and repeat-run CD captures remain
  byte-identical to the source after INDEX 01.
- `work/pcm-integration.log`: an initial headless run hit its 27-second
  watchdog after WM_CLOSE during credits. Later runs exited normally, but
  the first timeout's cause has not been isolated. This remains a shutdown
  issue to investigate, not a passed stability test.

Correct standalone behavior takes priority over future frontend extraction.
Physical listening, extended gameplay and complete shutdown reliability are
not established by these bounded checks.


## Thread-specific quit delivery and close timing

The HLE overlay now keeps pending PostQuitMessage requests per guest thread.
Previously a process-global quit flag could be overwritten or consumed by
another thread. `pod_messages` reproduces this with a synthetic UI thread and
worker, checks independent exit codes, filtered GetMessage/PeekMessage and
PM_NOREMOVE. It failed before the fix and passes after it. Pending quit is
still deferred behind eligible posted messages; modal loops repost it to
their own thread. See the Microsoft PostQuitMessage/GetMessage contracts
linked from the overlay.

All seven CTests passed in 0.84 seconds. Four bounded standalone cases
(`work/quit-stress/results.json`) exited 0 with effects/CD, movies and/or PCM
capture. Total times were 27.421, 27.128, 17.078 and 30.116 seconds for close
requests at 22, 22, 12 and 25 seconds. Teardown therefore took about five
seconds in these cases. An earlier watchdog at 27 seconds allowed only five
seconds after WM_CLOSE and could cut teardown short. These measurements
support that explanation; they do not prove the old timeout was caused by
the quit-flag bug. Extended stability remains unqualified.

`--window-log` logs close/destroy delivery with thread IDs and the remaining
message queue if the process is still running two seconds after a scheduled
close. It observes shutdown without forcing ExitProcess or repeating close.
Allow at least ten seconds between `--close-after` and `--watchdog` in bounded
regression runs.

## Local app with bundled runtime libraries

The CMake `build/native/POD.app` remains a development bundle. A separate
local packaging step resolves its Mach-O dependency graph, includes the
non-system libraries, rewrites load paths, checks architecture compatibility
and applies/verifies an ad-hoc signature. It also includes SDL3 when the
selected SDL2 implementation is sdl2-compat (SDL3 is loaded dynamically).
The optional source-built SDL_ttf now links the same pkg-config SDL2 target
as the host, avoiding a second unrelated SDL2 system framework.

```sh
python3 tools/macos/package_app.py
work/macos-package/POD.app/Contents/MacOS/pod \
  --game "$PWD/game" --state-dir "$PWD/work/native-state" \
  --cd-cue "$PWD/work/disc/POD Gold (Europe) (En,Fr,De,Es,It).cue"
```

Use `--build DIR` and `--output NEW.app` to select another build/destination.
The script refuses to replace an existing bundle. It does not copy game
assets, sources or writable state. The 53 MiB local bundle contains five
runtime libraries and a manifest with source hashes. Nothing is published.

The packaged binary was copied to a temporary location outside this checkout
and run there with explicit game, state and CUE paths. Movies/effects/CD ran
and ExitProcess(0) completed in 27.334 seconds on the final repeated check. DYLD_PRINT_LIBRARIES showed
all five bundled libraries and no Homebrew, system SDL framework or build-tree
runtime dependency (`work/package-verification/result.json`). The temporary
copy was removed after the run; logs and the local package remain.

This verifies relocation of runtime dependencies on this Mac. The compiled
project defaults still require explicit path overrides outside the development
layout. Finder setup, a clean-machine run, signing/notarization for delivery,
and other OS/architecture support remain separate, unverified work.


The packaged app was also launched graphically from `/private/tmp` with
external game/state paths. Its native main menu was observed through the
native-window-control workflow; CoreAudio handled effects and CD. The
scheduled WM_CLOSE run exited 0 (`work/package-gui.log`), and the framebuffer
capture is `work/package-gui.png`. This is a rendering/device-opening check,
not physical input or listening qualification.


## Input semantics and verified in-game exit

The runtime overlay now separates ordinary key/button down state, lock-key
toggle state and GetAsyncKeyState's recent-press bit. Previously ordinary
arrows and mouse buttons retained a toggle bit after release. Keyboard
messages now include extended-key and repeat/previous-state flags. Synthetic
checks cover release to zero, recent-press consumption, lock-key repeats and
message flags; they failed before the correction and pass afterward.

SendMessageCallbackA was also missing. The native in-game confirmation route
previously aborted on this import (`work/quit-drag.log`). The implementation
uses the existing HLE sent-message machinery: same-thread delivery/completion
is immediate; cross-thread delivery uses the window's thread, and completion
waits for the sender's message polling. The synthetic `pod_send_callback`
case checks these ownership rules, NULL callbacks, invalid windows and
broadcast excluding child windows. All eight CTests passed; the final suite took 0.73 seconds.
This covers tested cases, not complete Windows API conformance.

The native app was then launched with the unique title **POD Quit verification**.
After observing the main menu, automated native Esc opened the game's warning;
Left selected YES and Enter confirmed. `work/quit-identified.log` records the
native key transitions, the game sending WM_DESTROY and ExitProcess(0) at
about 102 seconds. The scheduled WM_CLOSE at 120 seconds was never reached.
This establishes the in-game keyboard exit route on this build. It does not
qualify all arrow navigation, physical controls, or mouse input.

The timed headless imitation `work/quit-api.log` instead captured a loading
screen and timed out; it did not reach the same observed UI route and is not
counted as an exit validation. An earlier graphic attempt also reached its
watchdog in credits. Fixed startup timestamps alone cannot identify menu state.

Mouse diagnostics found a button-down at the intended game coordinate but
a release at 0,0 during an automated click. No coordinate compensation was
added for that observation. Separately, code inspection found that the HLE
cursor-warp path passed renderer pixels to SDL's window-point API; it now
inverts the same HiDPI scale used for incoming coordinates. Physical mouse
behavior and automated click/release reliability remain unqualified.

`--window-log` includes key/button delivery and coordinate conversion details.
`--window-title TEXT` distinguishes the intended test instance from another
app with the same bundle identifier. GUI checks must observe that title and
correlate with its log; a disappeared window alone is not proof of exit 0.


## Repeatable standalone regression runner

```sh
python3 tools/portable/verify_standalone.py \
  --cue 'work/disc/POD Gold (Europe) (En,Fr,De,Es,It).cue'
```

The runner uses explicit content/state paths and headless SDL audio. It
checks effects/CD startup and movies/CD startup, posts WM_CLOSE, allows
15 seconds beyond its scheduled close time, and requires the guest's own
ExitProcess(0). Logs, isolated state and machine-readable results remain
under `work/standalone-regression/`. Use `--only effects` or `--only movies`
for a focused check; `--exe`, `--game` and `--output` select other locations.
Without `--cue`, CD is explicitly disabled rather than selected implicitly.

The final two cases passed in 17.123 and 27.168 seconds. This is a bounded
service/shutdown check, not a scripted gameplay milestone or conformance
comparison with the Windows original.

The attempted graphical race-clock check (`work/race-clock.log`,
`work/race-clock.png`) remained in Player 1 car selection instead of reaching
racing. Its clock comparison is therefore incomplete; no game-speed or
pacing conclusion is drawn, and no timing adjustment was made from it.


## Sanitizer and import coverage checks

A separate Debug build of the eight synthetic test executables passed with
AddressSanitizer and UBSan (1.20 seconds), including stack-use-after-return
checking and halt-on-UB. The generated game translation was not built or run
with sanitizers. The commands used were:

```sh
cmake -S . -B build/native-sanitize -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_C_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer' \
  -DCMAKE_EXE_LINKER_FLAGS='-fsanitize=address,undefined' \
  -DCMAKE_SHARED_LINKER_FLAGS='-fsanitize=address,undefined'
cmake --build build/native-sanitize -j6 --target \
  pod_portable_selftest pod_cd_selftest pod_audio_queue_selftest \
  pod_messages_selftest pod_send_callback_selftest \
  hle_pe_loader_selftest hle_host_selftest hle_pump_selftest
DYLD_FALLBACK_LIBRARY_PATH="$(pkg-config --variable=libdir sdl3)" \
  UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
  ASAN_OPTIONS=abort_on_error=1:detect_stack_use_after_return=1 \
  ctest --test-dir build/native-sanitize --output-on-failure
```

The SDL3 fallback path matters for the current sdl2-compat installation under
ASan. Initial attempts without it stalled in an SDL2 library-constructor
error dialog before main, as shown by `work/sanitize-sample.txt`; removing
the sandbox alone did not fix that. Those attempts were stopped and are not
passed tests. Final results are in `work/sanitize-final-results.log`.

The import census now scans the staged runtime, native host tables and named
aliases separately:

```sh
work/venv/bin/python tools/macos/audit_imports.py \
  --toolkit build/native/pcrecomp-runtime --out work/imports-final.json
```

The current static census finds 9 executable import names and 5 codec import
names absent from the scanned sources. These are potential unexercised API
paths, not evidence that every one is used during normal play; conversely,
a name found in a table does not prove its behavior. Results retain candidate
source files and the missing-name lists under ignored `work/`. Full API
coverage and the original x86 oracle remain outside the verified scope.


## Wave error text and display-clock regression (2026-11-11)

The waveOut service now supplies `waveOutGetErrorTextA`, including its own
error codes, truncation/NUL termination, zero-length buffers, NULL pointers
and unknown-code results. These paths are covered by the fake-queue test;
formatting an error does not require an audio device. The static import census
therefore has nine executable names absent, rather than ten.

Periodic captures reached a racing scene in the same execution. Before the
clock fix, the headless HUD advanced from 10.24 to 48.21 seconds between
host timestamps 18,001 and 22,001 ms (`work/race-samples.log`). A graphical
run advanced from 6.10 to 38.21 seconds over 30,001 ms
(`work/race-graphical-long.log`). The virtual CPU calibration was about
200 MHz as intended; no TSC scaling change was justified by this evidence.

The runtime's DirectDraw `Flip` swapped buffers immediately. POD was observed
calling it with flags zero; the missing synchronization was partly masked by
SDL's physical-display VSync. The overlay now schedules flips against a
monotonic 60 Hz virtual display, releasing the machine while waiting, without
changing generated game code or borrowing the physical monitor's refresh rate.
This synchronous implementation does not advertise FLIPNOVSYNC or FLIPINTERVAL
capabilities. See the [DirectDraw Flip contract](https://learn.microsoft.com/en-us/windows/win32/api/ddraw/nf-ddraw-idirectdrawsurface7-flip).

The synthetic test performs twelve flips without an SDL window: it fails the
minimum elapsed-time check with the old implementation, and passes with the
fix. The bounded headless racing scene then advanced from 6.00 to 25.85 seconds
between host timestamps 26,000 and 46,000 ms (`work/race-flip-sync.log` and
numbered captures). That is 19.85 seconds over 20 seconds, not a full-game or
Windows-oracle timing qualification.

The standalone requests an SDL presenter without physical VSync, because
blocking there while holding the machine would add a second display clock.
Other HLE clients retain their existing default. This remains standalone
scheduling; it does not supply a Libretro frame-step API.

The regression runner now schedules the observed key sequence in the effects
case to reach CD playback, then closes at 26 seconds. The movie case closes at
24 seconds and requires codec/effects startup; CD startup is checked in the
separate effects case. An earlier 12-second, unscripted effects case and the
old combined movie/CD expectation failed after pacing was corrected. Those
failures are retained under `work/standalone-flip-regression/`; they are not
reported as passing runs.


The identified graphical bundle (`POD focused bundle timing`) advanced from
9.91 to 29.75 HUD seconds between host timestamps 50,000 and 70,000 ms:
19.84 seconds over 20 seconds, matching the headless result. It reached
ExitProcess(0) after WM_CLOSE at 80 seconds. See
`work/race-focused-bundle.log` and the numbered captures. Earlier graphical
attempts stayed at the main menu because the scheduled inputs did not start
a race; they are excluded from the clock comparison. Foregrounding the exact
bundle through its unique window title made the final attempt identifiable.
Physical input/handling, extended play and full audiovisual sync remain open.

Final synthetic results: eight normal CTests passed (0.88 seconds), and eight
ASan/UBSan CTests passed (1.34 seconds). Sanitizers still cover the synthetic
executables rather than the complete translated game. The standalone runner's
paced effects/CD case passed in 31.162 seconds and its movie/effects case in
29.195 seconds (`work/standalone-paced-final/results.json`).

The final packaged executable's source hash matches the latest native build.
A fresh temporary relocation passed both standalone cases (31.114 seconds for
effects/CD and 29.198 seconds for movies/effects), loading all five runtime
libraries from the relocated bundle in each case, with no Homebrew/build or
external SDL-framework runtime loads. The temporary copy was removed; the
local bundle and `work/package-verification/final/relocation.json` remain.
Finder setup, physical listening and a clean machine were not tested.

Prudent cleanup removed two superseded generated app bundles and 112 assessment
object files (about 270 MiB total). Source checkouts, the current app/build,
external game/disc data, state and diagnostic logs/captures were retained.
