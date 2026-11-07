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
