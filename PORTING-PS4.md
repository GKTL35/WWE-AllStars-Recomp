# PS4 port of WWE All Stars Recomp: audit, plan and groundwork

Date 2026-11-20. **Status: groundwork only. Nothing here has been compiled, built or
run on a PS4.** No PS4 toolchain, console or game files were available when this was
written. **[V]** marks a fact checked against source in this working copy, in ReXGlue
v0.10.0 (`f5337cdc`) or in the OpenOrbis headers. **[I]** marks inference or an estimate.

The goal: run this recomp (ReXGlue v0.10.0, Native Vulkan renderer) on a jailbroken PS4
(GoldHEN), as a home-screen app built with OpenOrbis.

## 1. Prior art, and what it means

Three PS4 recomp ports by iHaiDeeZ already exist, two of them ReXGlue/Xbox 360 games:

| Port | Source console | Notes |
| --- | --- | --- |
| MP3-PS4 (Max Payne 3) | Xbox 360, ReXGlue | GPL-3.0 repo; builds from `rexglue-sdk-nx` + `patches/sdk-ps4.patch`; D3D9 calls drawn by "Renut" on Vulkan through Mesa RADV on the PS4 GPU. 15-25 fps in busy fights (30 fps game). **[V]** |
| MGR-PS4 (Metal Gear Rising) | Xbox 360, ReXGlue | Same base. Release notes only; not read in full. |
| SMW (Mario Wonder) | Switch | MIT. Publishes prebuilt OpenOrbis libc++ and RADV-for-PS4 static libraries as "SMW PS4 SDK 1". **[V]** |

All of them build on **ps4-kit**, "the shared PS4 base of these ports (toolchain setup,
runtime, Mesa RADV for the PS4, VideoOut swapchain, packaging)". **ps4-kit is
public** (https://github.com/iHaiDeeZ/ps4-kit, MIT, read at commit `79d173b`). **[V]** See section 1a.

Findings in MP3-PS4's README that apply to any ReXGlue game on PS4 **[V]**:

- The SDK MP3-PS4 builds on (its `rexglue-sdk-nx` base; not checked against upstream v0.10.0) ignored guest thread priorities and affinities by default. The PS4 does not
  time-slice equal-priority threads, so applying them took busy fights from 4-5 to 13-17 fps.
- The 360 clock came from `clock_getres`, which the PS4 answers with 2 ns: the game ran
  too fast. Fix: use the PS4's time stamp counter. (This repo already has
  `patches/rexglue-guest-tick-source.patch`; check how it interacts.)
- Guest memory: the 360 address window is reserved at once, physical memory is PS4
  direct memory mapped on first use and aliased into every mirror. The `0xE0000000`
  view sits 4 KB higher with 64 KB granularity, so hooks must read guest memory
  through `GuestPtr`, never `base + address`.
- Code generation for the Jaguar CPU: `-march=btver2`, and PPC fused multiply-adds
  become a multiply and an add (no FMA unit). Compare with this repo's
  `-ffp-contract=off` in `CMakeLists.txt`.
- The main thread, not the GPU, limits frame rate on the PS4's 1.6 GHz Jaguar cores.

## 1a. ps4-kit, as read (commit 79d173b) **[V]**

Layout: `toolchain/` (clang 21 + libc++ 21, libc header overlay, sysroot scripts), `runtime/`
(dlmalloc heap, stdio lock, `*at` file layer, crash log, sampler), `gpu/` (`build-mesa.sh` plus 32 patches on
Mesa 26.0.8, `ps4_vk_wsi.*` Vulkan swapchain on VideoOut), `scripts/` (pkg, upload, crash log),
`samples/kit-test/` (template). A new port is `include(ps4-kit.cmake)`, `ps4_kit_link_mesa(app)`,
`ps4_kit_app(app)`. Setup order: `scripts/seed-build-tree.sh`, `gpu/build-mesa.sh`, `samples/kit-test/build.sh`
(WSL, then upload from Windows). It mentions no ReXGlue or Xbox 360 code; the 360 side (guest memory, fibers,
fault handling) lives in the ports, not the kit.

What it settles for this plan:

- **Fault handling (section 3, item 4):** the toolchain overlay fixes the PS4 `ucontext_t` (machine context at
  byte 0x40, rip = word 20) and adds `machine/fpu.h`, which RPCS3 and Vita3K fault handlers already use. A ReXGlue
  fault handler can read and edit registers through it. Still needs writing and testing for ReXGlue.
- **Threads:** `ps4_boost_thread` (priority 650), `ps4_kit_set_min_thread_stack`, `ps4_new_thread_cores`
  (0x7F when the 7th core is available). Matches the thread-priority finding in section 1.
- **Memory:** a dlmalloc heap capped at 256 MiB in the system pool by default, overflow heaps and direct
  memory beyond that, `PROT_NONE` reservations optionally treated as address-space-only
  (`ps4_kit_set_mmap_reserve_prot_none`, from xemu), and the Mesa patch set turns on
  `sceKernelEnableDmemAliasing` for placed maps. So aliasing and large reservations have prior art; the exact
  guest-memory layout is still MP3-PS4's code (GPL) or new work.
- **GPU:** Mesa 26.0.8 RADV, built `-O3 -march=btver2`, Vulkan 1.3 in the kit's own benchmark. PS4-specific
  patches I read: no conditional rendering (`VK_EXT_conditional_rendering` off, CP firmware hang), a
  `QUAD_LIST` topology, a 4x larger GS ring. **Nothing in the kit lists the Vulkan features the Native renderer
  needs** (`bufferDeviceAddress`, runtime descriptor arrays, partially bound, update-unused-while-pending,
  `shaderInt64`, dynamic rendering). RADV exposes them on stock Mesa, and `EXT_buffer_device_address` appears in
  the PS4 extension list, but it must be confirmed on the console with a `vkGetPhysicalDeviceFeatures2` dump.
- **Render thread cost:** `docs/gpu-thread-performance.md` measured about 6 microseconds per draw on the
  render thread (about 2 microseconds in `vkCmdDrawIndexed` itself) on Jaguar. That sets the draw budget for 60 fps.
- **Still open:** fibers. `makecontext`/`swapcontext` do not appear in the kit; MP3-PS4 or a hand-written
  switch is still needed. Also XMA/FFmpeg cross-build, and the audio and input drivers in this repo (the kit has
  no audio or pad code; written against the OpenOrbis headers only).

## 2. What in this repo is platform-specific

The repo's own code is small and mostly portable already (ADR-0006: "no Win32-only APIs
outside platform layers"). **[V]** (grep of `src/`).

**Runs on the console, needs work:**

| File | Issue | Plan |
| --- | --- | --- |
| `src/native/pipelines.cpp:645-652` | `SetThreadPriority` under `_WIN32` | Use pthread priorities on PS4 (the thread-priority finding above applies to the SDK too). |
| `src/native/vk_context.cpp:153-159` | Refuses devices without `bufferDeviceAddress`, `runtimeDescriptorArray`, `descriptorBindingPartiallyBound`, `descriptorBindingUpdateUnusedWhilePending`, `shaderInt64` and dynamic rendering | **Check RADV-for-PS4 exposes all of these.** It is the first thing to test; if a feature is missing the Native renderer needs a fallback. Optional extras (graphics pipeline library, present wait) are negotiated and may be absent. |
| `src/native/vk_context.h:29` | Bindless heaps of 8192 2D textures, 512 3D, 512 cube, 1024 samplers | Check descriptor limits and memory use on the PS4 driver. |
| `src/native/write_watch.cpp` | Write-protects guest pages through the SDK's physical-memory callbacks | Relies on the SDK's fault handler (section 3, item 4). |
| `src/startup_paths.cpp` | Non-Windows branch uses `rex/system.h` | Probably fine; add `/data/WWE` paths. |
| `src/settings/schema_export.cpp` | `REX_PLATFORM_WIN32` guarded | Likely PC-only; confirm. |
| `src/crash_report.cpp` | Win32 minidump and zip | PC-only; PS4 needs its own crash log (MP3-PS4 writes `boot-trace.log`, `crash.log`, `hang.log`). |

**Runs on the PC only (never on the console):**

- `launcher/` (Avalonia): replace with a PC "maker" tool plus a small on-console check, as SMW and MP3-PS4 do.
- `src/native/xenosrecomp_dxc.cpp` (`LoadLibraryW("dxcompiler.dll")`), `shader_convert_mode.cpp`, `topup_converter.cpp`: shader conversion (ADR-0029) is a PC step. Ship the converted shader pack. The PS4 compiles SPIR-V to GPU code through RADV and caches pipelines (MP3-PS4 keeps `pipeline_list.bin`).
- `src/resolve_watch.cpp`: research tool, already a no-op off Windows.
- `scripts/*.ps1`, `.tools/`: PC build. The build runs under WSL for the PS4 step in the ports above.

## 3. What the SDK needs (ReXGlue v0.10.0)

A PS4 target is a new platform, not a Linux build: an OpenOrbis target is built with
`-nostdlib`-style toolchains and a FreeBSD-derived kernel. The SDK's file layout makes
the seams clear **[V]**:

1. **Platform macros.** `include/rex/platform.h` and `CMakeLists.txt` (`UNIX AND NOT APPLE` picks Linux). Add `REX_PLATFORM_PS4`, a PS4 CMake branch, and exclude SDL.
2. **Memory.** `src/core/memory_posix.cpp` uses `shm_open`, `mmap(MAP_FIXED_NOREPLACE)` and `mprotect`. PS4 needs the kernel's direct-memory mapping instead (see the MP3-PS4 design above). Check the PS4's per-process memory budget against the 4 GB virtual reservation; MP3-PS4 maps physical memory lazily for that reason.
3. **Fibers.** `src/core/fiber_posix.cpp` uses `makecontext`/`swapcontext`. OpenOrbis declares `makecontext` only as a stub in `LibcInternal.h`. **[V]** Whether it works is unknown. The Android port notes (`docs/research/android-port.md`) describe SVR-PC's hand-written register switch, which is the fallback.
4. **Fault handling.** `src/core/exception_handler_posix.cpp` reads Linux's `gregs[REG_RIP]`. OpenOrbis' headers define no signal-context struct **[V]**, so PS4 needs its own handling for guest SEH and the write-watch faults.
5. **Threads and clock.** `src/core/threading_posix.cpp` and `clock_posix.cpp`: the thread-priority and clock findings in section 1.
6. **Audio.** New backend, `ps4/audio/` (this commit): `sceAudioOut`, stereo fold, same frame format as the SDL driver. XMA decoding uses the SDK's in-tree FFmpeg (`thirdparty/FFmpeg`, `ffmpeg-overlay`); cross-building it for OpenOrbis is a task of its own. This repo's `rexglue-xma-*.patch` files touch that path.
7. **Input.** New backend, `ps4/input/` (this commit): scePad, up to four pads for local multiplayer.
8. **Window and presentation.** `src/ui/window_sdl.cpp`, `surface_*`: replace with PS4 VideoOut. The SDK already has a `headless` cvar (touched by `rexglue-headless-keyboard.patch`), which is useful for the headless boot step. ps4-kit supplies a VideoOut swapchain today; OpenOrbis ships `orbis/VideoOut.h`.
9. **Networking.** `rexglue-online.patch` brings libcurl and miniupnp. Drop online play for the first PS4 build.
10. **This repo's 30 `patches/rexglue-*.patch`** apply to upstream v0.10.0. MP3-PS4 uses a different SDK fork (`rexglue-sdk-nx`) plus its own patch. Decide which base to follow before writing PS4 patches, to avoid three-way conflicts.

## 4. Groundwork added in this working copy

- `ps4/audio/ps4_audio.{h,cpp}`: `Ps4AudioDriver` and `Ps4AudioSystem`, modelled on `rex::audio::sdl`.
- `ps4/input/ps4_input_driver.{h,cpp}`: `Ps4InputDriver`, modelled on the NOP and SDL drivers.

Both are written against the real interfaces in ReXGlue v0.10.0 and the OpenOrbis
signatures (`sceAudioOutOpen`, `sceAudioOutOutput`, `scePadReadState`,
`sceUserServiceGetLoginUserIdList`), but they are **uncompiled and untested**. Known gaps:

- Audio output is stereo float only: the OpenOrbis header defines no 8-channel format, so the console's own 5.1 mix is not used.
- No pad hot-plug, no touch pad or motion, no light bar.
- `audio_mute` is defined in `ps4_audio.cpp`; a PS4 build must not also compile the SDL audio driver.
- Neither is wired into CMake yet, because the SDK picks backends itself (item 1).

## 4a. PS4 platform patch for ReXGlue (`patches/rexglue-ps4-platform.patch`)

Applies to ReXGlue v0.10.0 (`f5337cd`). **None of it has been run on a console; only the fiber
switch was run natively on Linux x86-64 (`ps4/tests/fiber_ps4_selftest.cpp`).** Build switch:
`-DREX_TARGET_PS4=ON` with ps4-kit's toolchain file.

| Piece | What it does | Status |
| --- | --- | --- |
| `platform.h` | `REX_PLATFORM_PS4`; also sets `REX_PLATFORM_LINUX` so the posix sources build | written |
| `fiber_ps4.cpp` | hand-written x86-64 register switch with a guard page | **ran natively** |
| `memory_ps4.cpp` | guest arena: reserve address space, commit with anonymous mappings, the 512 MiB physical area as one direct-memory block aliased at 0xA0000000, 0xC0000000, 0xE0000000, 0x7F000000 and 0x100000000 | uncompiled, untested |
| `clock_posix.cpp` | fixed 1 GHz tick rate (the PS4's `clock_getres` says 2 ns), `CLOCK_MONOTONIC` | uncompiled |
| `threading_posix.cpp` | thread id, names, priorities (guest -15..+15 mapped to PS4 256..767), affinity via libkernel | uncompiled; assumes `pthread_t` is the `ScePthread` |
| `exception_handler_posix.cpp` | fault handler using the FreeBSD register layout from ps4-kit's overlay; SIGBUS handled too | uncompiled |

Known problems in it:

- The guest views at 0x80000000 and 0x90000000 alias the same XEX memory on the 360. Here they are
  two separate reservations (aliasing would cost 256 MiB of real RAM).
- Direct memory needs `sceKernelEnableDmemAliasing`; if the kernel refuses it, the mirrors cannot work.
- The 0xE0000000 view (4 KiB higher) relies on the SDK's existing larger-host-page path.
- Still to adapt: `socket_posix`, `dynlib_posix`, `env_posix`, `console_posix`, `seh_posix`, `atomic_posix`,
  `xthread.cpp`, the kernel's `xam_net.cpp`, and any other `REX_PLATFORM_LINUX` code. They were not read yet.
- The OpenOrbis git repository has no libc headers (they ship in its release bundle), so none of this
  could be syntax-checked here against the real headers.

## 5. Suggested order of work

1. **Get the PC build working first.** The repo's own notes say it has never run on Linux or Proton (`docs/research/performance-and-low-end-hardware.md:57`); it has only been built on Windows. A Linux x86-64 build is the nearest step to PS4 and also helps the Android goal. Needs the disc image and Title Update 2 from your own console.
2. **Set up ps4-kit and run `samples/kit-test` on the console** (proves toolchain, Mesa and upload). **Read MP3-PS4's `patches/sdk-ps4.patch` and `docs/DEVELOPMENT.md`**.
3. **Headless boot on PS4** (platform patch in section 4a): SDK core only (memory, threads, fibers, faults, clock), log output, no graphics. Success is the game's code running to the first GPU call.
4. **Graphics**: bring up Vulkan on the PS4 and test the feature list in section 2 before anything else.
5. **Audio, input, packaging**, using the drivers here.
6. **Performance**: thread priorities and cores, then the main thread. WWE runs at 60 fps, MP3 at 30, so the target is harder.

## 6. Risks

- **ps4-kit is MIT but young** (32 commits, no releases) and its build is WSL-only with a hardcoded OpenOrbis path (`/opt/pacbrew/ps4/openorbis`); expect setup friction.
- **Vulkan features** the Native renderer requires may be missing from RADV-for-PS4.
- **60 fps** may be out of reach on the Jaguar cores (MP3-PS4 does not hold 30 in busy fights). A 30 fps option, or lower detail, may be needed.
- **Memory**: bindless heaps, shader pack, texture caches and the 4 GB reservation all compete for the console's memory.
- **Testing needs a jailbroken PS4 with GoldHEN.** Nothing here can be signed off without one.
- **Licenses**: MP3-PS4 is GPL-3.0 and SMW is MIT. This repo is BSD-3-Clause; copying GPL code in changes what can be distributed.
