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
- `generals-stderr.log`: startup output, rotated to `generals-stderr-prev.log`.

Both original games are required. The IPA contains the engine's source-controlled
Reforged overlay, bundled Liberation fonts and dependency licenses. It contains
no original game data or bulk Reforged art download. Keep your existing working
game installation and saves separate while testing.

Initial input uses SDL's single-finger mouse emulation, plus hardware mouse and
keyboard support. A complete touch control scheme and background/resume behavior
still need implementation and device validation. Windows-only Direct3D 11 effects
remain outside the SDL renderer's capabilities, as on desktop POSIX.

The build publishes a bounded compiler report to `ci/ios-port-logs` in addition
to the Actions artifacts, allowing debugging through Git without Actions API
access. This generated branch contains only build diagnostics and run metadata.
