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

The local-network permission prompt explains LAN multiplayer access. Broadcast
discovery additionally requires Apple's multicast entitlement in the final
provisioning profile. Set `ZH_IOS_LAN_ENTITLEMENT=1` when packaging, or select
`lan_multicast` in the Actions dispatch form, to request it. The default IPA
does not request multicast access. An ad-hoc signature cannot grant this
permission; device validation with the chosen signing profile is still required.

If hosted arm64 runners have no capacity, a manual Actions dispatch can select
`host_runner: macos-15-intel`. This changes the host tools' architecture; the
device target remains arm64 and packaging still checks the Mach-O platform.

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
(the first finger sets the target). **Drag two fingers together to pan the camera**;
pinch to zoom. Lift both fingers before switching between panning and pinching.
The gesture begins over the map; menus and command-bar panels do not initiate a
camera pan. A three-finger tap sends Escape for the menu. Hardware mouse and keyboard
input and the minimap remain available. Touch controls and background/resume behavior still need device validation. Windows-only Direct3D 11 effects
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

The first render-target feedback rejection per graphics device writes
`[GPU_FEEDBACK]` context and up to 20 `[GPU_FEEDBACK_STACK]` frames. Logical texture
identity helps distinguish a real engine feedback pass from a GPU mirror alias.
No bindings are changed or pixels read back by the diagnostic. The rejection and
its count remain in place. `Reforged-symbols.tsv` in build diagnostics maps the
exact binary's image-relative code offsets when runtime symbol names are missing.
It is also published with the final compiler report. This is diagnostic evidence,
not yet a rendering fix.

Application lifecycle events are watched immediately, including during startup.
Background transitions drain already submitted GPU work. The main loop, startup
pump and GPU submission paths wait for foreground activation before continuing.
The wait services UIKit without dispatching game input into incomplete subsystems.
Low-memory callbacks write `[IOS_LIFECYCLE] event=low-memory`; they do not free
engine-owned resources reentrantly. OS termination is logged and exits at the
next safe lifecycle check without running game teardown inside the callback.
Hardware tests should cover lock/unlock, app switching, interruptions and memory
pressure; host lifecycle tests do not validate iOS suspension or memory grants.
