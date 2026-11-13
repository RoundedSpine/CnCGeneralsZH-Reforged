# Merge report: `origin/main` into `post-single-pass`

Scratch branch: `merge-main-into-post-single-pass`
`post-single-pass` and `main` were not updated.

## SHAs

| Point | SHA |
|---|---|
| Merge base | `b13d45b681cbcf6bffba3691c26b3eabd463531c` |
| `main` | `a181cd3ddeda321effaf762fe8dca9a0ea2a0dcb` |
| `post-single-pass` before | `ad8bd2d85309163c822390ecb5e6cef0ef533ed8` |
| Merge result | `b9bcadedc2afefdb829da37e073c4ee78bce2833` |

Divergence at the base: `main` was 119 commits / 355 files ahead; `post-single-pass` was 242 commits / 284 files ahead. 38 files were edited on both sides. Git reported 4 textual conflicts. Nothing was resolved with `--ours` or `--theirs`.

## What `post-single-pass` had to keep

Grouped from `b13d45b..post-single-pass`. These commits are still ancestors of the merge, so they are not dropped. Where `main` also edited the same file, every added line from this side that is at least a short statement is still present, except the old `setFreeCameraPose` signature, which was widened on purpose (see conflicts).

- **Unsigned device IPA and CI.** `.github/workflows/build-ios.yml` (macOS 15, Xcode selected on the runner, iphoneos SDK, `build-ios.sh`, artifact `Reforged-iOS-unsigned-${{ github.sha }}`). Host gesture, lifecycle, shader, packaging and fingerprint checks before the device build. Not in the push-trigger list today (`experiment/ios-port`, `Claude-update`, `particle-perf-test`, plus `workflow_dispatch`). Unchanged by this merge.
- **CMake / toolchains.** `CMAKE_SYSTEM_NAME=iOS`, `arm64`, deployment target 16.0, host tools compiled for the Mac SDK, FFmpeg cross-compile. Unchanged.
- **Packaging.** `make-ios-ipa.sh`, `package-common.sh`, entitlements, fonts, bundle layout. Unchanged.
- **Online stack.** `mbedtls` and `onlineconnection` are still built and linked on every platform, including iOS (`CMakeLists.txt`). No new third-party dependency arrived from `main`.
- **Metal renderer.** Single-pass SDL GPU path, depth submission, soft particles, sun shadows, water, heat, AO, MSAA, MetalFX spatial upscale (opt-in), internal render scale, GPU flush and pass instrumentation. `GeneralsMD/Code/Platform/iOS/` and the `SdlGpuFrame*` sources were not edited by `main`.
- **Particle Performance.** Options.ini key, Display checkbox, `ParticlePerfToggle`, per-template and GPU timing. Still wired in `OptionsMenu.cpp` and `GameLOD.cpp`.
- **Lifecycle and networking.** `ZH_IOS_WaitForForeground` via an SDL event watch (background is not queued), GPU idle before UIKit takes the app, disconnect ping servers, local-network permission copy.
- **Touch.** One-finger selection, two-finger pan (`panByScreenPoints`), pinch zoom, three-finger menu. `W3DView::panByScreenPoints` override is still there.
- **Warning policy and Apple-only sources.** Clang-scoped diagnostics, `IOSAudioSession.mm`, `IOSBootstrap.mm`, `IOSWindowDiagnostics.mm` still listed in `CMakeLists.txt` (lines 963 and 2134).

## Conflicts

### `CHANGELOG.md`

Both sides inserted a new top section under the same 128-change banner. Kept the iOS notes (experimental build, lifecycle, quit, two-finger pan, render-target probe, heat-haze skip) and then `main`'s notes (squads, ranks 6–10, talent trees, pathfinding and logic-thread performance, client height reads). Shared history below the insertion is untouched.

### `GeneralsMD/Code/BuildFingerprint.manifest`

Two insertions collided: `Data/Scenarios/missileflyon_drone.txt` (main only) and the test list (`test_package_link_libraries.py`, `test_particle_perf_policy.cpp` from this branch, `test_pathfind_slicing.inc` from main). After the union, `Tools/fingerprint-manifest.sh` rewrote the manifest from `git ls-files` so it is exactly the tracked set. `--check` passed: 4730 files, no duplicates.

### `W3DView.h` free-camera overrides

`post-single-pass` marked the freecam virtuals `override` (Clang warning policy). `main` added `Real roll = 0.0f` and `setFreeCameraScripted`. The merged declarations are the upstream signatures, still `override`, matching `View.h` and `W3DView.cpp` (the `.cpp` auto-merged: roll is stored, scripted shots skip `stepFreeCamera`). The old 3-argument declaration is gone because the base now has a defaulted fourth argument; `GameConsole` still calls the 3-argument form.

### `dx8renderer.cpp` draw effect

`main` drops `DRAW_EFFECT_CLOAK` on alpha meshes so a blended card is not drawn solid. This branch calls `Direct3D11_Set_Draw_Effect` on every non-Windows build, including iOS, and clears it after the draw. The cloak test runs first; the POSIX call and the later clear stay. The existing `#if !defined(_WIN32)` team-glow split was not in the conflict and was left as on this branch.

## Upstream changes adapted rather than taken byte-for-byte

| Change | Why it was adapted |
|---|---|
| Freecam API (`View.h`, `W3DView.h`, `W3DView.cpp`) | Roll and scripted mode taken; `override` kept so the iOS Clang policy still holds. |
| Cloak-on-alpha skip in `dx8renderer.cpp` | Taken, and still fed through the POSIX/Metal draw-effect setter. |
| Fingerprint manifest | Regenerated from the merged tree instead of either side's list. |
| `Direct3D11_Wait_For_Frame` | Windows waits on the flip queue. `dx11runtime_posix.cpp` (the iOS/macOS/Linux switchboard) gained the empty stub `main` added. `W3DDisplay::waitForFrameQueue` calls it. No D3D11 queue exists on Metal. |
| Logic thread is always on | Taken as `main` wrote it, including the non-Windows `pthread_create` with an 8 MB stack (macOS `std::thread` is 512 KB; iOS has the same default). Not wrapped in a new Apple `#if`. See risks. |
| OS pump moved from `PosixGameEngine::update` to `GameEngine::execute` after the picture cap | Taken. iOS background is an SDL event watch (`IOSLifecycle.cpp`), not the poll, and `SdlGpuFrame*` still calls `ZH_IOS_WaitForForeground` before GPU submits. `SdlGameEngine::serviceWindowsOS` still runs every frame, so touch dispatch, focus, and `SdlInput_flushIOSTouch` still happen. They happen after the cap instead of at the end of `update`. |

## Deliberately not brought over

None. `main` did not edit `.github/`, `CMakeLists.txt`, presets, packaging, or `Platform/iOS/` since the merge base, so there was nothing iOS-specific to reject. New `main` sources are `GroupPanel.cpp` / `GroupPanel.h` only; `GAMEENGINE_SOURCES` is `GLOB_RECURSE` of `GameEngine/Source/*.cpp`, so the panel is in the iOS link with no CMake edit. `test_pathfind_slicing.inc` is included from `test_gameengine.cpp`.

## Both-sides files that auto-merged

Checked by replaying each `post-single-pass` added line (length ≥ 8) against the merge. All of them are still present. Incoming `main` hunks on the iOS-touching subset were read:

- `GameEngine.cpp` — logic thread always started; `RankReforged.ini`, `TalentsReforged.ini`, `SquadReforged.ini` loaded; client height reads use `Thing::CachePeek`; network sockets serviced during the frame cap; input pumped after it.
- `GlobalData` / `OptionsCatalog` / `OptionsMenu` / `optionsmenu_layout.py` / `OptionsMenuClassic.wnd` — `MinimapSize` 100–200 added beside the existing Particle Performance checkbox.
- `ParticleSys.cpp` — a missing master/slave system no longer throws away the whole save.
- `W3DDisplay.cpp` — squad and order-flash snapshots taken before the draw; `waitForFrameQueue` added.
- `W3DScene.cpp` — occluded-player list is static (the 32-player table is ~192 KB) and the 16th silhouette no longer wraps the stencil to 0.
- `W3DVolumetricShadow.cpp` / `W3DProjectedShadow.cpp` — comments only, plus the live-model lock the logic thread already required. Tree and aircraft shadow eligibility from this branch is unchanged.
- `sortingrenderer.cpp` — triangles inserted while `_InsertEmissive` is set skip shadow and smoke shade. `W3dWaypointBuffer.cpp` turns it on only around the building outline and turns it off again.
- `Network.cpp` / `DisconnectManager.cpp` — slot masks widened past 8 bits (`slotBit`, `allSlotsBut`). The iOS disconnect-ping block is still in `DisconnectManager.cpp`. Send grouping is 1.
- `StateMachine.h` — states are a sorted vector. The Clang null-`deleteInstance` diagnostic bracket from this branch is still there.
- `CommandXlat.cpp` / `MetaEvent.cpp` — F11 toggles simple radar colours. Default is added only if that key is free.
- `AIPathfind.cpp`, `GameLogic.cpp`, `ScriptEngine.cpp`, `ControlBar.cpp`, `W3DModelDraw.cpp`, `test_gameengine.cpp` — upstream performance, sleepy-heap order, squads, and tests. This branch's edits in those files were small and are still present.

## Non-portable constructs in the incoming diff

Searched added lines against `post-single-pass` for `windows.h`, `<intrin.h>`, `__asm`, `d3d8.h`, `d3d9.h`, `Direct3DCreate9`, `CreateWindow`. No hits. The two `-d3d9` mentions are comments in the cloak fix. New Win32 calls in the logic-thread and frame-wait code are under `#if defined(_WIN32)`; other platforms use `pthread` and `std::this_thread`.

## Protected iOS items

| Item | Present | How it was checked | Not checked |
|---|---|---|---|
| Unsigned IPA workflow (runner, Xcode, SDK, artifact) | Intact, byte-identical to `post-single-pass` | `git diff ad8bd2d` on `.github/workflows/build-ios.yml` is empty. Read end to end. | Device configure, compile, link, `make-ios-ipa.sh`, artifact upload. This machine is Linux, not macOS. `build-ios.sh` exits unless `uname` is Darwin. |
| Push triggers | Intact | Same file. Triggers remain `experiment/ios-port`, `Claude-update`, `particle-perf-test`, and `workflow_dispatch`. This scratch branch will not build on push. | A dispatched run. |
| CI preflight steps | Not relaxed | Workflow diff empty. Re-ran the steps that do not need CMake or Apple headers. | `test_host_tools.py` (needs `cmake`, not installed). Graphics/GPU ctests inside `build-ios.sh`. |
| CMake iOS flags, deployment target 16.0, arm64, host SDK split | Intact | `build-ios.sh` and `CMakeLists.txt` identical to `post-single-pass`. | A real `cmake -DCMAKE_SYSTEM_NAME=iOS` configure. |
| Packaging, entitlements, fonts, bundle id | Intact | Packaging scripts identical. `test_package_link_libraries.py`, `test_ios_network_permissions.py`, `artpack.py selfcheck` passed. | An IPA was not produced. |
| mbedtls + onlineconnection on iOS | Intact | Still compiled and linked in `CMakeLists.txt`, which did not change. No new dependency to add. | Link of the device binary. |
| New upstream sources registered | Yes | Only `GroupPanel.cpp`. Covered by `GLOB_RECURSE`. Fingerprint lists it once. | — |
| `#if` Apple / `ZH_PLATFORM_IOS` guards | Intact | Counts of those markers in every both-sides file are unchanged. No `.mm` file deleted. | — |
| `.mm` files in CMake | Intact | `IOSAudioSession.mm`, `IOSWindowDiagnostics.mm`, `IOSBootstrap.mm` still referenced. | Compile. |
| Metal single-pass, depth, soft particles, shadows, water, MetalFX | Intact in source | `Platform/iOS` and `SdlGpu*` not in the diff. Shadow and shader python tests passed (`test_ios_shadow_path.py`, `test_heat_haze.py`, `test_shoreline_refresh.py`, `test_sdl_present_mode.py`, `test_ios_shader_constants.py`, `test_native_particle_admission.py`). | On-device frames. |
| Particle Performance and Options.ini | Intact | Checkbox and `GameLOD` load still present next to the new minimap slider. `test_particle_perf_policy` and `test_graphics_preferences.py` passed. | Toggling it on a device. |
| Suspend / resume, pings | Intact, pump call site moved | Event watch and GPU waits unchanged. `test_ios_lifecycle.py`, `test_disconnect_ping.py`, `test_ios_startup_pump` passed. | Backgrounding a running match. |
| Touch and camera | Intact | `panByScreenPoints` override remains. `test_ios_touch` and `test_ios_camera_pan` passed. | Fingers on a device. |
| Paths, sandbox, permissions | Intact | `test_ios_global_new.py`, `test_ios_main_exit.py`, `test_ios_symbols.py`, `test_ios_memory_pressure.py` passed. | Files app on a device. |

### Preflight results on this machine

Passed: fingerprint `--check`; `artpack.py selfcheck`; the python tests named in the workflow except `test_host_tools.py`; `test_particle_perf_policy`, `test_ios_touch`, `test_ios_camera_pan`, `test_ios_startup_pump`, `test_feedback_diagnostic`, `test_refpack` (133025 checks, 0 failed).

Failed only because the tool is absent: `test_host_tools.py` (`cmake` not installed).

Could not run: iOS configure/compile/link/IPA, host graphics ctests (`test_sdl_ao`, heat, native effects, HDR, MSAA, post, particles, `test_gameengine`, hazard cloud, `test_online`), and any desktop `generals` target. `cmake` is not installed and the SDK is not Darwin.

## Host ctest failure (run 43388513757)

Dispatched **Build iOS unsigned IPA** on this branch at `d0307767`. Shader and GPU checks passed (33 tests, 687507 checks). `test_hazard_cloud_bake`, `test_hazard_cloud_shared`, and `test_online` passed. `test_gameengine` was `***Exception: SegFault` in 0.10s with no test output, and the device cross-compile never started. The same test on `post-single-pass` (run 43365500015) passed in 46.87s. The failing binary is the macOS host, not the iOS device.

`ZH_SYSTEM_GLOBAL_NEW` (calloc/free, 16-byte aligned) was on for Linux and for `ZH_PLATFORM_IOS` only. The host is `__APPLE__` and does not define `ZH_PLATFORM_IOS`, so its global `operator new` still came from `TheDynamicMemoryAllocator`, whose blocks are 4-byte aligned. arm64 Clang stores into that pointer as if it had the ABI's 16-byte alignment. A block that lands 4-mod-16 is a SIGSEGV before any `CHECK` prints. `main`'s libc++ use (vectors, maps, function objects) is what made a previously lucky host run fault immediately. Pools and `AsciiString` still allocate from the engine allocator by name. iOS device code takes the same path it already did.

The host `ctest` now sets `ZH_TEST_PROGRESS=1`. The test runner installs a `SIGSEGV`/`SIGBUS` handler (constructor priority 102) that writes a backtrace and re-raises, so a miss still shows up as SegFault. Neither change weakens a check or a warning.

This machine still cannot run the host suite or produce the IPA. Re-dispatch the workflow on this branch after the fix is pushed. `post-single-pass` is still `ad8bd2d8`.

## Must-preserve diff

`git diff post-single-pass HEAD` is the incoming `main` work (355 files, +31968/−4855) plus the four conflict resolutions. No file that existed on `post-single-pass` was deleted. iOS platform, workflow, and CMake files do not appear in that diff. The iOS-touching engine files that do appear are the rows above; each difference is an upstream feature layered onto the existing Apple code, not a replacement of it.

## On-device checklist

- [ ] Re-dispatch **Build iOS unsigned IPA** on `merge-main-into-post-single-pass` after the macOS-host `operator new` fix (push will not start it). The previous dispatch, run 43388513757 at `d0307767`, died in host `test_gameengine` (SIGSEGV, 0.10s) before the device build.
- [ ] Install the unsigned IPA. Cold launch to the main menu.
- [ ] Background during a menu, and during a match. Resume. Confirm GPU work stops and touch does not stick.
- [ ] Metal: particles, sun shadows, water, an explosion, a cloaked blended prop (Stealth yard emblem). Optional: MetalFX and a non-native render scale.
- [ ] Touch: select, two-finger pan, pinch zoom, three-finger menu. Minimap size slider and F11-equivalent radar colours if a keyboard is attached.
- [ ] Skirmish against one AI, then a larger one. Save and load.
- [ ] LAN or online lobby, including a disconnect ping. Do not expect replays recorded before this merge to play.

## Risks and follow-ups

- The logic tick always runs on its own thread, including on iOS, overlapped with the single-pass renderer. Upstream's lock and census are in the tree; they have not been timed on a phone. A suspend during an overlapped tick is the main device risk.
- The macOS host `operator new` fix is in `GameMemory.cpp` and has not been executed here. If the next dispatch still SIGSEGVs, the log should now name the test (`ZH_TEST_PROGRESS`) or print `fatal signal in test runner` plus a backtrace. Do not treat a green host ctest as an on-device pass.
- Match slots are wider than 8. Network and save/replay compatibility with older builds changes, as `main`'s changelog already says.
- Squads, talent cards, and the larger minimap are new HUD. They were not laid out against the iOS touch UI.
- `commit-messages.yml` rejects merge commits on pull requests. This branch is a merge on purpose. Do not rebase it away without a new review.
- Stop here. `post-single-pass` was not moved. Say if this branch should be merged into it.
