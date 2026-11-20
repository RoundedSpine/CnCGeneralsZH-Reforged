# Merge report: `origin/main` into `Stable_working`

Scratch branch: `merge-main-into-stable-working` (local; not pushed). `Stable_working` and `main` were not updated.
(`MERGE_REPORT.md` in the repo root is the earlier main -> post-single-pass report and is left as it was.)

## SHAs
- merge base: `a181cd3ddeda321effaf762fe8dca9a0ea2a0dcb`
- `main` (first merge): `b17cc5565f06b2fa6c23b7a848465923b935bd36` (354 commits, 1617 files ahead of the base)
- `main` (second merge, v4.1.0 and later): `aab83faa81009c1bc4953d823ba6aea061d0fa47` (143 further files)
- `Stable_working` before: `d898134602a8f375a089fcd5c6b624d52eccd673` (277 commits, 287 files ahead)
- merge commit: `923e25c1a8b8dcdd62ba5b823be0d1290a747cca`; follow-up fix commit `08c334e21`

## Conflicts (14 files) and resolutions
| File | Resolution |
|---|---|
| CHANGELOG.md | Kept Stable_working's iOS sections, then main's new sections. Dropped the leftover "Squads..." heading: main renamed that section ("The Angry Mob in one crowd...") and its body is already there. Main also removed the Ranger/Missile Defender Squad bullet; taken as upstream's decision (verify). |
| BuildFingerprint.manifest | Union of both sides, then regenerated with `Tools/fingerprint-manifest.sh`; `--check` passes (5552 files). |
| WeaponSet.h | Stable_working's `: 0` (not `NULL`) plus main's new `getCurWeaponTemplateSet()`. |
| W3DView.h | Stable_working's `override` keywords plus main's `resyncScriptedClock()`. |
| pointgr.h | `Write_Billboard(..., frame, bool mirror=false, float fireAgeSeed=0, bool writeFireAge=false)`: main's mirror and Stable_working's fire-age both kept. |
| W3DParticleSys.cpp | Kept Stable_working's fire-age path; added main's `p->isFlipbookMirrored()` to that call. |
| DX11GpuParticles.cpp | Stable_working had moved the shader text into `Platform/ParticleShaders.h` (shared with the SDL/Metal path). Main's frame-rate-scaled fade length (`FADE_MAX`) was ported into that shared text; `Evaluation(native, fadeMaxFrames=16)`. Default 16 leaves the text identical at 60 Hz. D3D11 passes `8*LOGICFRAMES_PER_RETAIL_FRAME`; the SDL device cannot include GameEngine headers (CI run 2 failed on `Common/GameCommon.h` not found in `posixd3d9`), so it reads `ParticleShaders::FadeMaxFrames()`, which `W3DParticleSys.cpp` sets from `PARTICLE_CLOSED_FORM_FADE_MAX_FRAMES` at startup (8 at the 30 Hz tick, same as upstream). Note this changes the Metal shader's fade length from the old fixed 16 to upstream's 8 to match the CPU path. |
| dx11runtime_posix.cpp | Stable_working's SceneEffects implementations kept; main's new terrain-relight/heights stubs added. |
| ffshader.cpp | Stable_working's SDL path kept (soft particles, fire look and draw effects stay supported on SDL, plus the ShadowViewport rewrite); added main's `TerrainRelight = false` for SDL. |
| BaseHeightMap.cpp | Both functions kept (`addShoreLineTile`, then `refreshShorelineState`); doc comment re-attached to `renderShoreLines`. Order matters: `test_shoreline_refresh.py` slices the source from `refreshShorelineState` up to `renderShoreLines`, so putting `addShoreLineTile` between them broke the first IPA CI run. Fixed in a follow-up commit; test passes. |
| PosixMain.cpp | Stable_working's crit-section/iOS bootstrap/crash-log lines kept; main's `GameSpeed_markMainStarted`, `-initspeed` and the pre-main-reads assert added around them. |
| OptionsMenu.cpp | Stable_working's catalog-driven `rendererSupportsOption` kept; `GroundFoliage` added to its non-D3D11 list (foliage needs D3D11 calls). |
| optionsmenu_layout.py | Both new check rows kept; the Particle Performance check moved into the new Menus group (see below). |
| test_gameengine.cpp | One `TestParticleSystemManager` (Stable_working's, earlier in the file); main's `newTestParticleSystemManager()` factory kept. |

## Adapted instead of taken as-is
- `FADE_MAX` shader change: ported into shared `ParticleShaders.h` (above).
- `Tools/stage-overlay.sh`: added `copy Audio Data/Audio` (upstream added 21 MB of Turkiye voice wavs; the overlay list had no Audio entry, so the IPA/packaged overlay would have missed them).
- Display page layout: with main's Ground Foliage row the Particle Performance checkbox overlapped the frame-rate combo (caught by `optionsmenu_layout.py selfcheck`), and the Screen/Picture columns end at the page foot (y=462). Moved it to column 3 under the menu-transition slider (`OptionsMenuClassic.wnd` rect edited by hand, script updated to match). Selfcheck now passes (79 rows).

## Deliberately not brought over
None.

## Protected items
| Item | Status | How checked |
|---|---|---|
| A. build-ios.yml, package-common.sh, macos-app-licenses.txt, Info.plist.in, entitlements | Identical to Stable_working | `git diff` empty |
| A. iOS CMake (iosstartup, IOSBootstrap.mm, bundle props, frameworks) | Intact | only CMake line removed is an include line main extended |
| A. online stack / mbedtls / licences | Intact, untouched by main | no diff in those paths |
| A. new upstream deps | None added | diff of main |
| B. Apple/arm64 guards | `ZH_PLATFORM_IOS` 128/128, `__APPLE__` 106/106, `__aarch64__` 14/14; no guard line removed | grep before/after |
| B. Apple-only files | all 6 `Platform/iOS` sources referenced once in CMake | grep |
| B. new upstream sources | picked up by `file(GLOB_RECURSE)`; new tests registered | grep |
| B. touch, lifecycle, paths | files unchanged by merge | no diff in SdlIOSTouch / IOSLifecycle |
| C. Metal/SDL renderer path | kept; two upstream hunks layered in (ffshader, SunShadow grass-shader allowance from main) | diff review |
| C. Particle Performance diagnostic | present (counts identical); checkbox relocated | grep, selfcheck |
| C. Options.ini keys | catalog unchanged except upstream `GroundFoliage` | diff |
| C. Particles Classic/Reforged setting, single-pass renderer | not present on this branch | grep |
| C. render scaling / heat distortion | no file touched by the merge in those paths beyond the above; **not exercised** | review only |

Non-portable grep on added lines: only `_WIN32`-guarded blocks and `<intrin.h>` inside `#if _DEBUG` + `_MSC_VER`.

## Build verification
- CI run 1 failed in `test_shoreline_refresh.py` (cause above, fixed). Re-ran locally with the CI's own commands: all python tests in the workflow except `test_host_tools.py` (needs cmake) and `test_miniaudio_recovery.py` (vendored miniaudio absent in my sandbox; both fail identically on unmodified Stable_working), plus the refpack, particle-policy, ios-touch and ios-camera-pan C++ tests with ASan/UBSan and `artpack.py selfcheck`: all pass.
- Could run: portability grep, fingerprint manifest check, `optionsmenu_layout.py selfcheck`, a g++ test of `ParticleShaders::Evaluation` (no `FADE_MAX` left; 16u/8u substitute correctly), patch applies cleanly to Stable_working.
- **Could not run**: no CMake, Xcode or macOS here; nothing was compiled. The iOS workflow was read, not executed. Desktop build not run. Untested compile risks: the changed `Write_Billboard` signature; `dx11runtime_posix.cpp` stubs.

## On-device checklist
IPA installs and launches; background/resume; Metal particles, shadows, water; render scaling on with heat haze and screen effects; touch controls and camera pan/zoom; skirmish match (incl. new AI/sidebar); online/lobby; Options > Display shows Particle Performance (right column) and Menus slider; Ground Foliage disabled on iOS; Turkiye voices play.

## Risks / follow-ups
- Run the iOS CI workflow on the pushed scratch branch before approving; it is the real compile check.
- Upstream sidebar, hotkey and load-screen UI are new; their touch behaviour is untested.
- Confirm upstream's removal of the Squad changelog entry is intended.

## Second pass: main moved to `aab83faa8` (v4.1.0 plus later fixes)
Merged into the same scratch branch on top of the first merge.
- **Conflicts (2):** `CHANGELOG.md` (Stable_working's iOS sections first, then upstream's new sections) and `Tests/test_gameengine.cpp` (upstream's new `OnlineQueues.h`/`<fstream>`/`<sstream>` includes taken; two harmless duplicate includes remain). Manifest regenerated (5571 files, `--check` passes).
- **New upstream files:** `Common/InstallFiles.{h,cpp}`, `WWLib/sha256.{h,cpp}`, `Tools/online_layout.py`, 7 online `.wnd` menus, 7 Cinema map shot lists. New `.cpp` files are picked up by the CMake globs; `sha256.cpp` was added to upstream's WWLib list and merged cleanly. All Windows-only calls in the new code are behind `_WIN32` (InstallFiles uses `<unistd.h>` otherwise); no new third-party dependency. Packaging/CI/licence files are still identical to Stable_working.
- **Online stack on iOS:** untouched in structure (mbedtls/onlineconnection still built). New: the client sends every install file's sha256 before online login (`InstallFiles`). Needs an on-device online test.
- **D3D11-only changes not mirrored on SDL/Metal:** `dx11backend.cpp` now keeps the 2D interface out of the sun's shadow (Projection[15] check). I did not port this to the SDL path (cannot verify without a device); look for the interface taking shadow/smoke tint on iOS when the sun shadow box covers the map corner.
- **File-path behaviour changed upstream** ("serve retail textures only from the game's own sources", `ForkTextures.inc` generated by CMake, Reforged*.big/language archive routes closed): platform-neutral code, but on-device check that textures and skins still load from the Documents folders.
- **Checks re-run:** all CI-listed python tests pass except `test_host_tools.py` (needs cmake, unavailable here); particle-policy and touch C++ tests pass under ASan/UBSan; `artpack.py selfcheck` OK; `optionsmenu_layout.py selfcheck` OK (80 rows). `online_layout.py selfcheck` fails the same way on upstream `main` itself (it needs EA's original `.wnd` files) and is a ctest, not an IPA CI step.
- Still not compiled here: the full iOS build.

## Third pass: main moved to `b33038308` (v4.2.0)
Clean merge, no conflicts (22 files): LAN, ranked Quick Match and vs-AI games at 1x speed, Quick Match mark fix, version bump to v4.2.0, string updates, re-recorded D3D11 shader cache (D3D11 only), `GameCommon.h` speed constants. No new files, dependencies, packaging or platform code; no non-portable constructs added. Python CI tests re-run: same results as before (only `test_host_tools.py` fails locally, needs cmake).

## CI fixes found by running the merged branch
1. `SdlGpuFrameParticles.cpp` could not include `Common/GameCommon.h` (posixd3d9 target has no GameEngine include path): replaced by `ParticleShaders::FadeMaxFrames()`.
2. `W3DAssetManager.cpp:442` (upstream code): `?:` between `StringClass` and `NULL` is ambiguous on Apple clang: `.Peek_Buffer()`.
3. `test_shoreline_refresh.py`: function order in `BaseHeightMap.cpp`.
4. `test_gameengine`: `a_template_maps_rules_are_served_beside_it_and_in_its_name` used raw backslash paths with `fopen`; test now converts separators off Windows (fix unverified).
