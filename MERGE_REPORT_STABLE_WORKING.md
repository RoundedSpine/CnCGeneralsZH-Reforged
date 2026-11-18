# Merge report: `origin/main` into `Stable_working`

Scratch branch: `merge-main-into-stable-working` (local; not pushed). `Stable_working` and `main` were not updated.
(`MERGE_REPORT.md` in the repo root is the earlier main -> post-single-pass report and is left as it was.)

## SHAs
- merge base: `a181cd3ddeda321effaf762fe8dca9a0ea2a0dcb`
- `main`: `b17cc5565f06b2fa6c23b7a848465923b935bd36` (354 commits, 1617 files ahead of the base)
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
| DX11GpuParticles.cpp | Stable_working had moved the shader text into `Platform/ParticleShaders.h` (shared with the SDL/Metal path). Main's frame-rate-scaled fade length (`FADE_MAX`) was ported into that shared text; `Evaluation(native, fadeMaxFrames=16)`. Default 16 leaves the text identical at 60 Hz. D3D11 and SDL (`SdlGpuFrameParticles.cpp`) both pass `8*LOGICFRAMES_PER_RETAIL_FRAME`. |
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
- **Could not run**: no CMake, Xcode or macOS here; nothing was compiled. The iOS workflow was read, not executed. Desktop build not run. Untested compile risks: `SdlGpuFrameParticles.cpp` now includes `Common/GameCommon.h`; the changed `Write_Billboard` signature; `dx11runtime_posix.cpp` stubs.

## On-device checklist
IPA installs and launches; background/resume; Metal particles, shadows, water; render scaling on with heat haze and screen effects; touch controls and camera pan/zoom; skirmish match (incl. new AI/sidebar); online/lobby; Options > Display shows Particle Performance (right column) and Menus slider; Ground Foliage disabled on iOS; Turkiye voices play.

## Risks / follow-ups
- Run the iOS CI workflow on the pushed scratch branch before approving; it is the real compile check.
- Upstream sidebar, hotkey and load-screen UI are new; their touch behaviour is untested.
- Confirm upstream's removal of the Squad changelog entry is intended.
