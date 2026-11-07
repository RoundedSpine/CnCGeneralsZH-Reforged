# Verified Git merge-tree results (uploaded Actions artifacts, 2026-11-07)

Two uploaded `ios-upstream-merge-audit` ZIP archives contain the seven expected output files. Their path lists, status and conflict messages match; the first output tree hash differs (likely because the integration branch HEAD changed between runs). Do not assume identical merge trees or silently pick one. Both report merge-tree exit status **1**.

## Full net diff counts

- Upstream net changed paths: **423**
- Integration branch net changed paths: **186**
- Common net changed paths: **33**
- Git reports **14 content-conflicted paths**; **32 auto-merging notifications**. Neither count equals the total changed paths.
- This is a Git tree simulation, not a committed merge, compile or iOS runtime test.

## Git-reported conflict paths (14)

1. `CHANGELOG.md`
2. `GeneralsMD/Code/BuildFingerprint.manifest`
3. `GeneralsMD/Code/GameEngine/Source/GameClient/GUI/GUICallbacks/Menus/OptionsMenu.cpp`
4. `GeneralsMD/Code/GameEngineDevice/Include/W3DDevice/GameClient/W3DWaterTracks.h`
5. `GeneralsMD/Code/GameEngineDevice/Source/PosixDevice/Render/PosixDevice9.cpp`
6. `GeneralsMD/Code/GameEngineDevice/Source/PosixDevice/Render/SdlGpuFrame.cpp`
7. `GeneralsMD/Code/GameEngineDevice/Source/PosixDevice/Render/SdlGpuFrame.h`
8. `GeneralsMD/Code/GameEngineDevice/Source/W3DDevice/GameClient/HazardCloudUnsupported.cpp`
9. `GeneralsMD/Code/GameEngineDevice/Source/W3DDevice/GameClient/Water/W3DWater.cpp`
10. `GeneralsMD/Code/GameEngineDevice/Source/W3DDevice/GameClient/Water/W3DWaterTracks.cpp`
11. `GeneralsMD/Code/GameEngineDevice/Source/Win32Device/Common/Win32BIGFileSystem.cpp`
12. `GeneralsMD/Code/Libraries/Source/WWVegas/WW3D2/dx11runtime_posix.cpp`
13. `GeneralsMD/Code/Tests/test_gameengine.cpp`
14. `GeneralsMD/Code/Tools/vendor.sh`

## Resolution sequence and guardrails

1. **Build/dependency gate:** `vendor.sh`, fingerprint manifest; preserve iOS host tools, retry behavior, no hosted-service dependencies.
2. **Renderer and platform gate:** `PosixDevice9.cpp`, `SdlGpuFrame.cpp/.h`, `HazardCloudUnsupported.cpp`, `dx11runtime_posix.cpp`; preserve SDL/Metal, compute particle implementation and iOS device handling.
3. **Water gate:** `W3DWaterTracks.h/.cpp`, `W3DWater.cpp`; preserve iOS platform guards, add upstream water features with associated declarations/shaders/tests.
4. **Gameplay/UI gate:** `OptionsMenu.cpp`; reconcile nine-page upstream options with eight-page iOS/touch UI.
5. **Cross-platform/test gate:** `Win32BIGFileSystem.cpp`, `test_gameengine.cpp`; retain platform-specific behavior and update tests.
6. **Release notes:** `CHANGELOG.md`; resolve without prematurely claiming v3.8 parity.

**Critical:** auto-merged `CMakeLists.txt`, `GameEngine.cpp`, `GlobalData.cpp`, `OptionsCatalog.cpp`, `ParticleSys.cpp`, `W3DScene.cpp`, `W3DTreeBuffer.cpp` and other files still require semantic review. In particular, the upstream hosted online targets must be excluded even if Git auto-merges CMake.

Next: perform a controlled merge on `ios-main-merge` only when conflicts can be resolved with full source and dependency context; run a new audit afterward, then CMake/iOS Actions validation. Neither original branch is to be modified.
