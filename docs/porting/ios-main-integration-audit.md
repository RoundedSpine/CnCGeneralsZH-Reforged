# main → iOS integration audit (in progress)

Integration branch: `ios-main-merge`, based on `Claude-update`. Source branch: `main`.

## Safety constraints
- Do not replace iOS-specific CMake platform detection, iOS 16 deployment target, Objective-C/Objective-C++ enablement, SDL/Metal rendering, iOS lifecycle/touch controls, or unsigned IPA workflow with the macOS/Linux equivalents.
- Do not claim a working merge until the iOS device IPA workflow builds successfully.
- The draft PR #4 is **not mergeable** as of the initial check. It points from `main` to `Claude-update` and is not the integration branch.

## Verified upstream changes
- `W3DTreeBuffer.cpp`: upstream includes `W3DWater.h`; incorporated in integration commit `8f1bc7b`.
- `W3DTreeBuffer.cpp`: upstream suppresses tree shadows in a modern full-mirror water pass; incorporated in `2df4848`.
- `W3DTreeBuffer.cpp`: upstream changes cached weather breeze/pause state; **not yet ported** pending declaration and behavior review.
- `W3DScene.cpp`: upstream changes modern-water reflection visibility, world-space UI exclusions, and reflection/decal behavior; requires joint review with `W3DWater`, `W3DInGameUI`, and iOS renderer.
- `CMakeLists.txt`: upstream introduces `mbedtls` and `onlineconnection` targets, vendor prerequisites, and gameengine linkage; **not yet ported** because the dependency must be integrated together with source files and vendor scripts.
- `CMakeLists.txt`: upstream introduces `SdlHazardCloudShared.cpp`, `SdlHazardCloudSharedShader.cpp`, and new tests; requires related source additions before adding to iOS target lists.
- `CMakeLists.txt`: iOS branch already defines `ZH_PLATFORM_IOS` and enables OBJC/OBJCXX; preserve these changes.

## Next validation gates
1. Compare the three-way merge base and both branches for overlapping files; classify conflicts as source, build, shaders, assets, or packaging.
2. Integrate upstream source dependencies as coherent feature groups; avoid applying partial rendering features that cannot compile.
3. Check CMake configure and compile on the iOS device toolchain, then link and produce the unsigned IPA on a macOS runner.
4. Run iOS touch, lifecycle, graphics, memory, shader, packaging, and regression tests; inspect build logs and artifacts.
5. Only then consider merging into `Claude-update`.

## Current verification
Source comparison and two targeted code commits only. **No full merge, compilation, or IPA validation has occurred.**

## Three-way overlap scan (2026-11-07)
Compared merge-base `5d6a328764a7bef8f977387077776ae6835750a7` independently to `main` and `Claude-update`.
The compare endpoint returned **300** `main` changed-file entries (API cap) and **180** `Claude-update` entries. Therefore the following is a **partial overlap inventory**, NOT the full conflict list; 423 changed files on the draft PR does not mean 423 merge conflicts.

Both branches changed these ten paths in the returned entries:
- `.github/workflows/build-ios.yml`
- `CHANGELOG.md`
- `GeneralsMD/Code/BuildFingerprint.manifest`
- `GeneralsMD/Code/CMakeLists.txt`
- `GeneralsMD/Code/Data/Patch.str`
- `GeneralsMD/Code/GameEngine/Source/Common/GameEngine.cpp`
- `GeneralsMD/Code/GameEngine/Source/Common/GlobalData.cpp`
- `GeneralsMD/Code/GameEngine/Source/Common/OptionsCatalog.cpp`
- `GeneralsMD/Code/GameEngine/Source/GameClient/GUI/GUICallbacks/Menus/OptionsMenu.cpp`
- `GeneralsMD/Code/GameEngine/Source/GameClient/System/ParticleSys.cpp`

The full overlap inventory requires a complete Git three-way merge or untruncated changed-file enumeration. These paths are **candidates for semantic conflicts**, not confirmed conflict markers.

### Additional cautions
- `main` CMake includes mbedTLS/onlineconnection targets and SDL hazard-cloud components absent from the iOS CMake; adding CMake references alone is unsafe.
- The iOS branch adds logging in `GlobalData.cpp` not present in `main`; preserve that behavior.
- Local checkout attempt in this execution environment could not resolve github.com; no Git merge or compilation has been performed.
