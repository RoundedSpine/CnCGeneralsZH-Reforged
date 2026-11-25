# iOS layer on top of upstream main

This branch is upstream `main` plus the files below, and nothing else. It builds the unsigned device IPA
(`.github/workflows/build-ios.yml`, `Tools/build-ios.sh`). When upstream moves, merge `main` into this branch and
re-check this list; do not add features here.

## Added files (iOS-only)
- `CMake/HostTools.cmake`
- `GameEngine/Include/GameClient/ScreenPan.h`
- `GameEngineDevice/Include/SdlDevice/GameClient/IOSTouch.h`
- `GameEngineDevice/Source/SdlDevice/GameClient/SdlIOSTouch.cpp`
- `Libraries/Include/Platform/IOSAudioSession.h`
- `Libraries/Include/Platform/IOSLifecycle.h`
- `Libraries/Include/Platform/IOSMemoryDiagnostics.h`
- `Libraries/Include/Platform/IOSStartup.h`
- `Libraries/Include/Platform/IOSStartupPump.h`
- `Libraries/Include/Platform/IOSWindowDiagnostics.h`
- `Platform/iOS/IOSAudioSession.mm`
- `Platform/iOS/IOSBootstrap.mm`
- `Platform/iOS/IOSLifecycle.cpp`
- `Platform/iOS/IOSMemoryDiagnostics.cpp`
- `Platform/iOS/IOSStartup.cpp`
- `Platform/iOS/IOSUserDataMigration.h`
- `Platform/iOS/IOSWindowDiagnostics.mm`
- `Platform/iOS/Info.plist.in`
- `Platform/iOS/README.md`
- `Platform/iOS/Reforged.entitlements`
- `Tests/test_ios_camera_pan.cpp`
- `Tests/test_ios_global_new.py`
- `Tests/test_ios_lifecycle.py`
- `Tests/test_ios_main_exit.py`
- `Tests/test_ios_network_permissions.py`
- `Tests/test_ios_report.py`
- `Tests/test_ios_startup_pump.cpp`
- `Tests/test_ios_symbols.py`
- `Tests/test_ios_touch.cpp`
- `Tests/test_miniaudio_recovery.py`
- `Tools/build-ios.sh`
- `Tools/ios-symbols.py`
- `Tools/make-ios-ipa.sh`
- `Tools/publish-ios-report.py`
- `Tools/sdl-shadercross-ios.patch`
- `Tools/stage-ios-fonts.sh`

## Edits to upstream files (all behind `ZH_PLATFORM_IOS` or `__APPLE__` unless noted)
- `.github/workflows/build-ios.yml` (+12/-36)
- `BuildFingerprint.manifest` (+36/-0)
- `CMakeLists.txt` (+72/-5)
- `GameEngine/Include/GameClient/LookAtXlat.h` (+18/-1)
- `GameEngine/Include/GameClient/View.h` (+3/-0)
- `GameEngine/Source/Common/GameEngine.cpp` (+12/-0)
- `GameEngine/Source/Common/GlobalData.cpp` (+5/-1)
- `GameEngine/Source/Common/System/ExecutableDirectory.cpp` (+4/-0)
- `GameEngine/Source/Common/System/GameMemory.cpp` (+3/-1)
- `GameEngine/Source/Common/UserPreferences.cpp` (+37/-4)
- `GameEngine/Source/GameClient/MessageStream/LookAtXlat.cpp` (+57/-0)
- `GameEngineDevice/Include/SdlDevice/GameClient/SdlInput.h` (+8/-0)
- `GameEngineDevice/Include/W3DDevice/GameClient/W3DView.h` (+78/-72)
- `GameEngineDevice/Source/PosixDevice/Render/SdlGpuFrame.cpp` (+19/-0)
- `GameEngineDevice/Source/PosixDevice/Render/SdlGpuFrameSmoke.cpp` (+4/-0)
- `GameEngineDevice/Source/PosixDevice/Render/SdlGpuFrameSunShadow.cpp` (+2/-0)
- `GameEngineDevice/Source/PosixDevice/Render/SdlGpuParticles.cpp` (+7/-0)
- `GameEngineDevice/Source/PosixDevice/Render/SdlGpuStorage.cpp` (+6/-0)
- `GameEngineDevice/Source/PosixDevice/Render/SdlReachOverlay.cpp` (+5/-0)
- `GameEngineDevice/Source/SdlDevice/Common/SdlGameEngine.cpp` (+25/-0)
- `GameEngineDevice/Source/SdlDevice/Common/SdlPanel.cpp` (+1/-1)
- `GameEngineDevice/Source/SdlDevice/GameClient/SdlInput.cpp` (+4/-0)
- `GameEngineDevice/Source/SdlDevice/GameClient/SdlMouse.cpp` (+8/-0)
- `GameEngineDevice/Source/W3DDevice/GameClient/W3DView.cpp` (+23/-0)
- `GameEngineDevice/Source/Win32Device/Common/Win32BIGFileSystem.cpp` (+5/-0)
- `Libraries/Source/WWVegas/Miles6/miniaudio/miles_miniaudio.cpp` (+130/-2)
- `Libraries/Source/WWVegas/WW3D2/glyphrasteriser.cpp` (+8/-0)
- `Main/PosixMain.cpp` (+66/-9)
- `Tools/ffmpeg-build-posix.sh` (+4/-0)
- `Tools/macos-app-licenses.txt` (+2/-0)
- `Tools/package-common.sh` (+1/-1)
- `Tools/vendor.ps1` (+2/-2)
- `Tools/vendor.sh` (+79/-3)

## Not carried from Stable_working (deliberately; not needed to build or launch the IPA)
Render scale and MetalFX upscaling, the Stable_working SDL particle/heat/AO/atmosphere pipelines and their tests,
iOS shadow and water paths, the options catalog and render-option hiding, GPU memory-pressure trimming
(`Handle_Memory_Pressure`), Particle Performance diagnostics, disconnect/ping fixes, and Stable_working's gameplay changes.
Upstream's own SDL/Metal renderer is used as it is, with only the iOS foreground gate (`ZH_IOS_WaitForForeground`)
before every GPU command-buffer acquire or submit, and GPU register/unregister with the lifecycle code.
