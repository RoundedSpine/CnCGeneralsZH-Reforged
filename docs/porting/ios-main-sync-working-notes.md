# iOS main sync working notes — 2026-11-08

Claude-update start `003b6fe261ccbcc79ced6f04672bf67f25555de8`; Claude-working `d4682cd9ef25ae637a1565c9a5859f6f63ebafca` (update ahead 1, behind 0). Prior upstream base `5d6a328764a7bef8f977387077776ae6835750a7`. RoundedSpine main `6a651eb67c596d091fce2db0e9de76d296ef73ae`; authoritative upstream tip `287ef500994a07fd5535d198bef1b3c57750d681`, 104 upstream commits.

## iOS invariants
Preserve simulation behavior except upstream-authored gameplay/data; foreground GPU submission/startup/exit/touch/audio/sandbox rules; HLSL→SPIR-V→MSL and native-pass constant restoration; depth-capture submission ordering and sparse MSAA; native lighting/particles and bounded GPU overflow; memory-pressure policy; iOS shadow fallback/aircraft/tree silhouettes; LAN entitlement opt-in; unsigned ARM64 IPA workflow/tests. Platform differences stay behind named seams.

## Commit groups
(a) gameplay/data/logic: Turkiye, target/aircraft/vehicle/blast/fog/path/no-superweapon changes, mod removal. Take as upstream and revalidate E1.
(b) renderer/platform/UI: water reflection/refraction/FFT/shore reuse, shared hazard volume, heat/tracer/laser, side HUD/tooltips/tab/observer, frame cap, file I/O, AppleClang fix, shadow skinned-mesh guard. Reviewed for iOS/Metal.
(c) online/control: relay, mbedTLS, -online adapters/screens, OnlineBridge/TransportRoute/lobby, -relay, -control and LAN/online hardening. Apple applicability reviewed separately.
(d) tests/tooling/CI/docs: build/version, fixtures/pathlab, POSIX/GCC portability and integration commits.

## Tree audit
Recursive Git trees show 180 Claude-update changes and 430 main changes. Divergent/shared overlap is 33 paths; identical shared edits are accepted directly. The renderer-heavy overlaps include PosixDevice9, SdlGpuFrame, water, hazard, shadows/model/tree/particles, dx11runtime_posix, engineshader, test_gameengine and vendor.sh. The seven diagnostic workflows and helicopter-shadow.patch are excluded; the patch is evidence only.

## Phase 3 compatibility findings

| File / area | Finding | Resolution |
| --- | --- | --- |
| `PosixDevice9*`, `SdlGpuFrame*` | Upstream refactors selectable FFT resolution and per-channel water snapshots; iOS needs foreground GPU gating, low-memory suppression, real scene colour format, tracked transients, depth-capture submit ordering and sparse-MSAA behavior. | Three-way merge retains upstream architecture and reattaches each iOS invariant at the refactored helper boundary. |
| `W3DWater.cpp`, `W3DWaterTracks.cpp` | Upstream SDL reflection/refraction and shoreline snapshot logic was Linux-oriented; iOS already has native scene effects/shore history. | iOS uses the upstream path only when the SDL GPU device and native atmosphere capability are available; shoreline support remains compiled for iOS. |
| hazard-cloud sources | Upstream adds a shared-volume model while iOS has native effect capability stubs and ember path. | Shared model and iOS stubs coexist; portable hazard tests are added to Apple host validation. |
| shadow/model/tree sources | Upstream adds current-pose/skinned-mesh safety; iOS has aircraft/tree silhouette and sun-map fallback work. | Both are retained. The fork-only `helicopter-shadow.patch` is not imported. |
| `ParticleSys.cpp` | Upstream particle changes overlap iOS native GPU admission policy. | Upstream behavior retained; POSIX native admission still excludes weapon trails, emissive particles and non-additive shader classes from the validated GPU path. |
| `OptionsMenu.cpp` / option catalog | Upstream adds Water page, Heat Haze and frame-rate controls; iOS requires renderer capability filtering and touch-safe existing layout behavior. | New options are retained but Water/Heat Haze are filtered through actual renderer capabilities; existing iOS graphics-startup/preferences/present-mode tests remain gates. |
| `test_gameengine.cpp` | Upstream timing edits overlap the prior Windows-only timing portability fix; standard library headers are vulnerable to the engine min/max macro if included late. | Use upstream portable `Clock_Ticks` helper and keep standard C++ headers before engine headers. |
| `CMakeLists.txt` / `vendor.sh` | Upstream adds mbedTLS/onlineconnection and Windows-only GeneralsOnline stack; iOS needs iosstartup and shadercross patching preserved. | `iosstartup` links are retained. GeneralsOnline stays Windows-only. POSIX onlineconnection/mbedTLS is provisionally retained and must compile on the Apple host and ARM64 device target; vendor keeps the iOS shadercross patch and `--skip-art`. |
| `ControlServer.cpp` | New `-control` WebSocket is a platform/network surface. | No iOS change: it is opt-in, non-blocking and bound to loopback only; it adds no LAN entitlement. |
| URL/browser path | Legacy Windows URL launcher is not an Apple implementation; POSIX browser body remains a null stub. | No change needed; no new `SDL_OpenURL` dependency was introduced into the iOS path. |
| removed mod support | iOS scripts/docs/packaging were checked for `-mod` assumptions. | No iOS-specific references found; no cleanup required. |
| frame-rate limit | Upstream adds picture-rate limiting above the fixed simulation rate. | No simulation edit; retain it and validate with `test_sdl_present_mode` plus the Apple host/device build. |
| new upstream tests | `test_hazard_cloud_bake`, `test_hazard_cloud_shared`, `test_online` are portable; `test_refpack` is already in the iOS workflow. | Add the first three to `build-ios.sh` Apple-host targets and CTest selection. |
