# POD Gold: ARM64/macOS port assessment

Update, 2026-11-11: this assessment is historical. The native host now runs
with PCM effects, lifted Indeo playback and two-file CUE CD music. Current
verification and remaining timing/input/packaging work are documented in
[MACOS_NATIVE.md](MACOS_NATIVE.md).

Assessed locally on 2026-11-09, on Apple Silicon with Apple Clang 21.

This is the initial assessment. See [MACOS_NATIVE.md](MACOS_NATIVE.md) for
the subsequent native prototype and its measured limits.

## Decision

A native port is technically plausible and worth a staged bring-up. It is
not a build-system-only change. Keep POD's existing lift and replace its
Windows-only host with **pcrecomp's existing `runtime/win32hle`**, extending
that layer only where POD needs it. Do not start a new Win32 implementation
or transplant Moto Racer's separate recompiler/runtime.

This assessment does not deliver a runnable Mac game. No menu, race, audio,
controller, frame-rate or visual-conformance result on macOS is claimed.

## Revisions and local inputs

- POD upstream: `ece77fdf3e19c4c0f2e21b07bb34db5015970ea8`.
- pcrecomp: `a255839d0b78688aa98e677f05d0f0f74710d8aa`, the revision pinned
  by POD's installer. Located in ignored `work/pcrecomp/`.
- Inspected pcrecomp main at `2adc02fec0702933325801aa7b29a7646321857f`:
  the HLE loader defect described below is still present. Main has other HLE
  improvements, but POD's roadmap documents a lift/input regression after
  the pin. Do not update the complete toolkit without comparing conformance.
- User's `POD Gold (Europe) (En,Fr,De,Es,It).zip`: track 1 and CUE extracted
  into ignored `work/disc/`, ISO9660 files into ignored `game/` using upstream's
  extractor. External archive unchanged. Track 2 remains in the archive.
- `PODMMX.EXE` and `INSTALL/IR41_32.DLL` have the exact sizes and PE timestamps
  expected by `run_lift.py`. This establishes eligibility for upstream's
  version check, not byte identity with the maintainer's private disc.
  Local hashes and import/section census are in `work/macos-assessment/`.
- Upstream extraction produced 949 files / 447.1 MiB; the README's example
  has a different file count. No missing-data conclusion follows from that
  count alone; full game-data compatibility remains to be tested in a run.

The checked-out README/CHANGELOG identify **v0.1.0**, with a scripted One
Player race already checked on Windows. Earlier web summaries describing
that feature as unverified are stale. Windowed physical input and CD music
remain open upstream. These are upstream reports, not local reproductions.

## Findings and evidence

| Area | Evidence | Consequence |
|---|---|---|
| Current build | Unmodified CMake rejects a 64-bit host at line 16; it also requires MSVC | Add a separate native host target while preserving Windows |
| CPU model | Upstream MMX selftest passes unchanged as Mach-O ARM64; flag selftest passes after adapting its stack to based memory | MMX does not inherently require an x86 host; real rendering/performance remain unmeasured |
| Lift pipeline | Local macOS catalog + lift: 11,863 entries, zero lift errors; 4,304,049 C lines in 111 chunks | The supported disc can be translated on this Mac; execution correctness remains a separate check |
| ARM64 compilation | All 112 generated C files (111 chunks plus dispatch) pass Clang syntax checking and compile at `-O0` to Mach-O ARM64 objects | No generator/backend blocker found; this is not a linked host, optimized build or game run |
| Memory | HLE spine, guest allocator and PE loader compile and run on ARM64 | Use a based 4 GiB guest address space; never truncate native pointers into guest registers |
| PE loader | Actual POD image maps but five file-backed sections compare unequal; all have `VirtualSize=0` | Fix Watcom handling before trusting IAT binding or boot |
| Loader experiment | An ignored copy using the existing Windows loader's Watcom rules passes all five POD section comparisons, and the codec comparisons | Small, concrete loader repair; experiment is not installed in the toolkit |
| Imports | Static scan: 48/231 POD import names absent from HLE; 6/110 codec names absent | This is a name census, not 183 working APIs or a prediction that all 48 execute |
| Display | Existing HLE DirectDraw and SDL2 presenter support based memory | Reuse them; test POD-specific palette, RGB565, flip, GetDC and surface padding behavior |
| Input | POD's host patches a real Windows DirectInput object; HLE has no DirectInput implementation | Implement explicit guest COM methods, buffered DIK events and notification signaling |
| Effects | HLE `waveOut` is a silent device that immediately retires buffers | A successful import/open is not sound; implement asynchronous SDL output with correct buffer ownership and callbacks |
| CD music | POD's host emulates silent MCI CD commands; archive has a separate audio track with a CUE pregap | Implement playback later, honoring the actual CUE layout; a concatenated-disc offset assumption is wrong for this archive |
| Movies | Codec is already lifted, but the host depends on AVIFile/AVIStream/DrawDib/VfW | Port this hosting path or explicitly skip movies for the first boot milestone |
| Original-code oracle | `--original` and `--codec-native` execute x86 machine code | Retain those on Windows; compare captures from a Windows reference against the native port |

Particular hazards:

- `src/runtime/host.c` and the COM wrappers cast guest integers directly to
  native pointers. They cannot simply be compiled with 64-bit typedefs.
  HLE provides `hle_ptr`, `hle_va`, guest allocations and explicit callbacks.
- The working Windows loader already knows both Watcom rules: use raw size
  when virtual size is zero, and treat file offset zero as uninitialized
  storage. POD's `.bss` has a nonzero raw-size field but no backing file data.
- POD's blitter writes two bytes before a locked surface; its host deliberately
  reserves padding on both sides. The pinned HLE allocates tail slack only.
  Preserve the title's established accommodation rather than silently
  allowing heap-header corruption.
- SDL/Cocoa presentation should stay on the main thread. The Windows
  presenter's dedicated window thread is not a portable template.
- SEH/C++ exceptions are not supported by the HLE. Imports present in a
  table can also be deliberate stubs. Reachability and observed behavior,
  not the import count, determine remaining work.
- The unmodified flag selftest assumes flat 32-bit memory and crashes on
  ARM64 (exit 139) because it truncates its static stack pointer. The ignored
  `flags_based_selftest.c` changes only that memory setup; it passes with
  `flags selftest: ok`. This is test-harness adaptation, not an upstream
  runtime fix or an unmodified-suite pass.

## Proposed milestones

1. **Foundation:** repair the HLE PE loader with reusable synthetic Watcom
   coverage; compile POD's generated C as ARM64 against the matching based
   memory runtime; provide explicit POD path/CD/INI behavior and diagnostics.
   First proof: correct mapped sections, honest import binding, no truncated
   pointers. No window or sound required.
2. **Visible menu:** use existing DirectDraw + SDL presentation, apply the
   surface-padding accommodation, supply enough DirectInput keyboard/event
   semantics to navigate and skip movies through an explicit development
   mode. First proof: captured menu and key down/up response.
3. **Race:** follow the upstream scripted Single Race and attract sequences,
   compare frames/state milestones with a Windows reference. Then verify
   real keyboard, focus, exit and configuration persistence on macOS.
4. **Complete media/input:** effects with real asynchronous `waveOut`, AVI
   and lifted Indeo hosting, CUE-based CD music, then physical controllers
   and app packaging. Validate listening and device behavior separately.

The largest unknown is runtime/API behavior during real game execution,
not whether ARM64 can express MMX integer operations. A menu is a useful
early gate; a playable and audible port is a materially larger integration.
There is no measured basis yet for a delivery-time or FPS estimate.

## Reproducing the bounded checks

From the repository root, with the pinned toolkit in `work/pcrecomp/`:

```sh
work/venv/bin/python tools/macos/audit_imports.py
clang -O2 -Iwork/pcrecomp/runtime/recomp32 \
  work/pcrecomp/runtime/recomp32/mmx_selftest.c -o work/macos-assessment/mmx_selftest
work/macos-assessment/mmx_selftest
clang -O2 -Iwork/pcrecomp/runtime/recomp32 \
  work/macos-assessment/flags_based_selftest.c -o work/macos-assessment/flags_based_selftest
work/macos-assessment/flags_based_selftest
clang -O1 -fno-strict-aliasing \
  -Iwork/pcrecomp/runtime/recomp32 -Iwork/pcrecomp/runtime/win32hle \
  tools/macos/loader_probe.c work/pcrecomp/runtime/win32hle/host_lite.c \
  work/pcrecomp/runtime/win32hle/guest_mem.c \
  work/pcrecomp/runtime/win32hle/pe_loader.c -o work/macos-assessment/loader_probe
work/macos-assessment/loader_probe game/PODMMX.EXE
work/macos-assessment/loader_probe game/INSTALL/IR41_32.DLL
```

The unmodified loader probe is **expected to return 1 for POD**, exposing
the defect, and 0 for the codec. It does not execute the guest or bind its
imports. The source-copy experiment is retained under
`work/macos-assessment/pe_loader-watcom.c` with its comparison logs.

`SDL2_ttf` is not installed in the current Homebrew environment, so the
complete SDL HLE selftest suite was not run. The isolated core probes above
do not depend on it. Nothing was installed globally.

Catalog and lift used upstream's commands with `PCRECOMP` pointing to the
pinned checkout and Python from `work/venv/` (`pefile` and `capstone`). Logs
are `work/macos-assessment/catalog.log` and `lift.log`; aggregate lift
statistics are in `work/lift_stats.json`. The lifter reports 1,066 bodies
without a clean terminator; zero lift errors does not mean those bodies or
the complete catalog have been exercised. Generated code stays ignored.

`syntax.json` and `compile.json` record commands and every compiler exit
code (112/112 successful in each pass). Compilation took 14.6 seconds with
four workers; this is build time, not game performance. The objects are in
`work/macos-assessment/objects/`. Both passes used based-memory access, local
guest registers, and `RECOMP_RETADDR=0u`; neither used `RECOMP_FLAT_MEMORY`.

The object compilation can be repeated without the Windows CMake target:

```sh
mkdir -p work/macos-assessment/objects
for source in src/recomp/gen/*.c; do
  clang -arch arm64 -std=c11 -w -O0 -fno-strict-aliasing -c \
    -DRECOMP_LOCAL_REGS -DRECOMP_RETADDR=0u \
    -Iwork/pcrecomp/runtime/recomp32 -Isrc/recomp/gen \
    "$source" -o "work/macos-assessment/objects/$(basename "$source" .c).o" || break
done
```

No production source changed and no commit/push was made by this assessment.

Repository setup completed on 2026-11-11 using the same convention as the
user's other projects:

- `origin`: `https://github.com/Zer0one/pod-recomp.git` (user's GitHub fork).
- `upstream`: `https://github.com/sp00nznet/pod-recomp.git` (original project).
- Local `main` tracks `origin/main`; `remote.pushDefault` is `origin`.
- Working directory remains `/Users/andrea/dev/pod-recomp`.
