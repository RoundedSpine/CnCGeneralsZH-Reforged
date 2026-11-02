# Experimental iOS port

Branch: `experiment/ios-port`. This port builds Reforged's existing POSIX engine
with its native SDL GPU Metal backend. It does not use DXVK or require CPU JIT.
The first device build and real-device validation are still in progress.

On a Mac with Xcode, CMake, Ninja and Python 3:

```sh
GeneralsMD/Code/Tools/build-ios.sh
```

The result is `build/ios/Reforged-iOS-unsigned.ipa`. GitHub Actions runs the same
script on `macos-15` when this branch is pushed. Install with a sideloading tool;
the IPA's ad-hoc signature is not an installation signature. Increased-memory-limit
and extended-virtual-addressing entitlements are requested. Their grant depends
on the signing tool and provisioning profile.

Launch once to create folders under **Files > On My iPhone/iPad > Reforged**:

- `GameData/`: your Zero Hour archives and loose game data.
- `GameData/ZH_Generals/`: the original Generals archives and loose game data.
- `UserData/ReforgedArt/`: optional upstream `Reforged*.big` art archives.
- `UserData/`: saves, options, registry and engine logs.
- `generals-stderr.log`: startup stages, elapsed times and memory snapshots,
  rotated to `generals-stderr-prev.log`. Signal crash reports are mirrored here.
- `UserData/ReleaseCrashInfo.txt`: the separate signal/fatal-error report, when
  the process can produce one. An iOS watchdog or memory kill may leave no report.

Both original games are required. The IPA contains the engine's source-controlled
Reforged overlay, bundled Liberation fonts and dependency licenses. It contains
no original game data or bulk Reforged art download. Keep your existing working
game installation and saves separate while testing.

Initial touch controls: tap to left click, drag to select, hold a stationary finger
for at least 0.55 seconds then release to right click, or tap with two fingers
(the first finger sets the target). Pinch zooms. A three-finger tap sends Escape
for the menu. Hardware mouse and keyboard input remain available. Use the minimap
or screen edges to move the camera; a dedicated touch camera-pan gesture is not
implemented. Touch controls and background/resume behavior still need device validation. Windows-only Direct3D 11 effects
remain outside the SDL renderer's capabilities, as on desktop POSIX.

The build publishes a bounded compiler report to `ci/ios-port-logs` in addition
to the Actions artifacts, allowing debugging through Git without Actions API
access. This generated branch contains only build diagnostics and run metadata.

Startup services UIKit at most once per 50 ms during archive loading without
polling or dispatching game input. The startup pump stops after engine initialization.
Global C++ allocations use the existing zeroed system allocator path to meet
arm64's 16-byte alignment contract; the game's explicit memory pools retain their layout.
These changes address porting defects but do not establish the cause of the first
device's early exit without a follow-up log.

The iOS entry adapter explicitly exits after the engine function and its local
cleanup have returned. SDL's UIKit delegate intentionally stays alive when
`SDL_main` returns; a desktop-style return alone leaves this game showing a black
window. This adapter preserves the return status and runs normal exit handlers.
