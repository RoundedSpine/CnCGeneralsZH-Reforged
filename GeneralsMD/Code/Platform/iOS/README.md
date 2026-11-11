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

Initial touch controls, Classic mouse (a tap orders; a long-press is the right click,
which deselects): tap to left click, drag to select, hold a stationary finger for
0.55 seconds to right click while it is still down (a short ring and a haptic mark
the moment; lifting does not click again). A second tap within 0.3 seconds and
30 points is a double click. Or tap with two fingers (the first finger sets the
target). **Drag two fingers to pan the camera**; pinch to zoom, including in the
same gesture, about the point between the fingers. The gesture begins over the map;
menus and command-bar panels do not start a pan. A fast pan keeps gliding and
stops when you touch the screen again. A three-finger tap sends Escape for the menu.
`ZH_IOS_HOLD_MS` (200–2000), `ZH_IOS_MOMENTUM=off|short|normal` and
`ZH_IOS_ONE_FINGER_PAN=1` (one finger pans instead of selecting). Hardware mouse and keyboard
input and the minimap remain available. Touch controls and background/resume behavior still need device validation. Some Windows Direct3D 11 effects remain outside the SDL renderer's capabilities;
see `docs/porting/ios-feature-audit.md` at the repository root.

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

On iOS the desktop fixed-function rendering probe and D3D9 combiner compiler are
disabled. Metal builds its own stage programs; the D3D9 compiler stub cannot
produce bytecode. This also avoids sampling draw states for an `ffprobe.txt` dump
that cannot be written to the read-only install. The legacy Windows patch cleanup
of `Data/INI/INIZH.big` is skipped on iOS.

A device log from revision `1c0f0bb2218a` recorded five UIKit appearance-transition
warnings during game-client initialization, followed by successful rendering and
normal shutdown. SDL replaces rendering views by detaching and reattaching its
root view controller; this is a possible source, not a confirmed cause. A device
trace of view replacement and appearance callbacks is needed before changing
window setup. The log does not demonstrate background/resume behavior or rule
out rendering defects.

Audio uses an explicit playback-only iOS session category (Playback, including
when the silent switch is on). Interruption notifications request recovery on
the audio service thread. Recovery waits for foreground activation and Apple's
`ShouldResume` option, reactivates the session, and retries failed device starts
at most once per second. A newer interruption cancels an in-flight recovery;
shutdown joins the worker before uninitializing the device. Offline audio never
starts a realtime device. `[IOS_AUDIO_SESSION]` and `[AUDIO_RECOVERY]` records
show resume permission, activation failures and restart results. Host tests use
the real null device with a native-session shim; calls, Siri, lock/unlock and
headphone/Bluetooth changes still need device validation.

Window diagnostics write `[IOS_WINDOW]` before/after window creation, sizing,
GPU claim/release and destruction, plus native window visibility/key changes.
Each record includes a sequence number, SDL monotonic timestamp, window/device
identity, controller/view classes and addresses, attachment and transition state.
The first 16 records include up to 12 `[IOS_WINDOW_STACK]` frames with image-relative
PCs for the exact build's `Reforged-symbols.tsv`; window snapshots stop after 128
records. These observations do not change the view controller or patch SDL.
For appearance warnings, retain the entire startup log so warnings can be placed
between the surrounding claim/release and window records. These diagnostics
narrow the cause but do not establish that a UIKit transition completed correctly.

Device creation prepares the CPU back buffer and depth surface before claiming
an SDL GPU window. A rejected surface format or allocation therefore does not
attach and remove a UIKit Metal view during an engine fallback attempt.
`[DEVICE_CREATE]` records the attempted formats, dimensions, multisampling and
result stage; `[DEVICE_SURFACE]` identifies the rejected surface and reason.
A new device log is needed to establish whether avoiding the failed attempt's
view replacement removes the appearance-transition warnings.

The iOS renderer now attempts the existing SDL sun-shadow path, including its
caster/camera state restoration and capability checks. Failed capture or receiver
setup retains the existing legacy fallback. `[SDL_SHADOW] receivers_ready` confirms
that a map was accepted by the receiver setup; failure reasons remain logged.
Classic graphics and the geometry-shadow settings still control eligibility.

Ambient Occlusion now has an SDL GPU path at the opaque-scene boundary, before
water, particles and UI. It reconstructs world positions from independent scene
depth, uses 14 taps at a 22-world-unit radius at half resolution, filters with
depth-aware horizontal/vertical passes, and multiplies scene RGB while preserving
alpha. Reflection/custom views are excluded; unsupported formats or failed shader
setup disable AO with a diagnostic. The pass respects the AO option and Classic
graphics. The AO control queries device capability; unavailable effects keep their
saved preferences. `[GRAPHICS_CAPS]` reports AO, Firelight, heat and other native
capabilities when opening Options; MSAA remains unavailable.
`[IOS_GRAPHICS]` logs the requested settings; `[SDL_AO] ready` identifies
successful resource setup. The shroud texture attenuates AO with the same cubic visibility factor as the
Windows pass; missing GPU shroud data skips AO instead of ignoring the fog.

Actions enables `ZH_IOS_GRAPHICS_TESTS=1` to build and run `test_sdl_ao` on the host
before cross-compilation. It compiles the production shaders to SPIR-V and MSL,
and reads back synthetic flat ground and a raised object when a GPU is available.
No GPU is an explicit skip, not evidence of correct pixels. Actual map appearance,
shadow quality and frame time still require device validation.

The native atmosphere pass adds ground haze, distance fade and the time-of-day sky
at the opaque-scene boundary. It shares Windows' map/light parameter calculations,
uses shroud attenuation, excludes reflected/custom views and preserves alpha for
water. `[SDL_ATMOSPHERE] ready` confirms resource setup; failures disable that path
with a reason. Hemisphere ambient and opaque team-colour glow use the existing
generated lighting shaders. `[SDL_LIGHTING]` records the modern/hemisphere mode.
Weather, ground marks and original-water splashes now accept native capabilities
instead of requiring Direct3D 11; iOS snow/weather uses quads.

AO accepts the main scene texture used by Glow as well as the back buffer;
reflection/custom views remain excluded. MSAA remains unavailable and its control
is disabled. Heat haze uses a scoped immutable scene copy. See
[the 44-control graphics audit](../../../../docs/porting/ios-graphics-options.md)
for the full supported subset and when each choice takes effect.

Classic Graphics requires an app restart to change. While a Classic run forces
lighting and volumetric smoke off, Options edits and displays their saved choices
for the next Reforged start. Reforged lighting changes map lights on the next map;
its Windows post-process grade is not yet ported. If these choices revert after a
restart, `[IOS_OPTIONS]` records the selected, written, loaded and active values,
the preferences path, and any write failure. Volumetric Smoke is the sun-shadow
smoke path; it is separate from atmosphere and the hazard-cloud style.
Choosing a Reforged-only effect now unticks Classic for the next start without
changing the rest of the custom settings. Accept, restart, and load a new map for
map lighting and team-colour asset changes.

Startup resets the early user-directory cache after UIKit sets its sandbox.
Missing legacy Application Support entries move to `Documents/UserData` within
the same container; existing Documents entries win, and legacy conflicts/links
remain untouched. `[IOS_USER_DATA]` records migration and the selected directory.
iOS also uses the UIKit-owned window's format-selection path, allowing D24S8-first
selection instead of falling back from an unsupported desktop D32 request.


Native Firelight reconstructs opaque surfaces from depth and applies up to twelve
flame lights before AO and atmosphere, preserving alpha and black shroud. The
legacy dynamic-light fallback remains active. This is a partial port, excluding
Windows headlights, HDR and translucent/reflected fire lighting.
`[SDL_FIRELIGHT] ready` and `[SDL_HEAT] ready size=...` report resource setup;
failures disable the corresponding capability with a reason. Heat requires the
scene render texture and eligible smudge particles. Only its scoped stage-0
binding substitutes the background copy; other target feedback remains refused.
Actions runs the production fire shader pixel checks and `test_sdl_heat` capture
readback on the ARM64 host when Metal is available.

For older iOS settings without `ClassicGraphics`, explicitly saved Reforged
lighting or volumetric smoke now selects Reforged before archives mount and
preferences load. Explicit Classic selections retain priority. Accept writes the
mode, and `[IOS_OPTIONS] phase=mode` reports whether it was explicit, default or
inferred from legacy Reforged choices. A new/empty file retains the Classic default.
