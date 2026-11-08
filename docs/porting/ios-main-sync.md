# iOS integration of main v3.6.0

Integrated upstream-synced `origin/main` at `5d6a3287` into the iOS branch based
on `1735c1d3`. This brings 57 main-branch commits, including radar/HUD and faction
art, aircraft altitude lines, handicap/AI changes, random-map fixes, water-foam and
weather improvements, animation blending and memory-pool double-free detection.
Upstream's removal of the custom rank ladder and its assets/tests is retained.

The merge preserves the iOS target/packaging, ARM64 workflow, startup and lifecycle
handling, touch input, audio recovery, memory-pressure policy, RefPack safeguards,
native graphics, FXAA/sharpen and bounded opt-in additive particle overflow.

Three conflicts required resolution:

- CMake keeps both the iOS `iosstartup` link and upstream's separately gated
  Windows online-client library. Neither condition replaces the other.
- The changelog retains the port entries and the incoming main entries.
- The fingerprint manifest is regenerated from the merged tracked files,
  including main's additions/removals and all port files.

The new online lobby backend remains upstream's Windows x64 feature. Its C++20,
GameNetworkingSockets and static Windows dependency configuration is not enabled
for Apple targets. Existing non-online builds retain their previous network path;
this merge does not claim the new service works on iOS.

Integration validation is performed after conflict and source integration work,
before publication/build dispatch. Full game builds use GitHub Actions on macOS
15 ARM64. Local checks cover native shaders, the port's regression helpers and
selected changed game sources with iOS enabled. Metal execution and physical
device gameplay/UI/network validation remain separate from source integration.
The ARM64 workflow also runs the shared `test_gameengine` CTest suite, with
isolated user/temp folders and null audio, before packaging the device IPA.

Pre-publication checks passed: 20 changed sources with `ZH_PLATFORM_IOS=1`,
the shader suite's 32 tests/392,141 checks, options-layout consistency (66 catalog
rows), the regenerated 4,506-file manifest and the port's graphics-preference,
startup, lifecycle, memory-pressure, shoreline and particle-admission helpers.
Native renderer/test executables compile, including the updated water-foam code.
Their GPU execution explicitly skips on this host because no backend is present.
At merge time, shared engine runtime coverage and Metal readbacks were pending
the workflow.

The first merged ARM64 attempt at `f6291691` stopped while compiling the macOS
host engine tests. The uploaded `logs_112962533253.zip` records six diagnostics
from two test-only portability issues: the map-dump helper used Windows timing
types, and a fallback check cast 99 outside `AIPersonality`'s representable range.
The helper now uses `std::chrono::steady_clock` and portable output separators;
the fallback check uses the defined count sentinel, which has no table entry.
Local GCC validation also exposed `<fstream>` being parsed after the engine's
legacy `min` macro. Standard C++ headers now precede the engine headers, and the
complete shared engine test executable builds locally.
This failure occurred before engine-test execution, Metal readbacks or device
IPA compilation; their status must come from the subsequent workflow.
After the portability fixes, the complete shared engine target built locally and
its CTest entry passed: 705 cases, 623,883 checks, zero failures. Apple Clang,
Metal readback and device packaging still require the ARM64 workflow.

## 2026-11-08: v3.8.x / main sync on `Claude-update`

This sync was prepared on the scratch branch only. `Claude-working` was not
modified. Before the merge, `Claude-update` was
`003b6fe261ccbcc79ced6f04672bf67f25555de8`; it contained every commit from
`Claude-working` (`d4682cd9ef25ae637a1565c9a5859f6f63ebafca`) and was one commit
ahead of it.

The previous upstream synchronization point was
`5d6a328764a7bef8f977387077776ae6835750a7`. The authoritative
`olcayseygan/CnCGeneralsZH-Reforged` main was
`287ef500994a07fd5535d198bef1b3c57750d681`, 104 commits ahead of that
base. RoundedSpine's `main` tip used for the merge was
`6a651eb67c596d091fce2db0e9de76d296ef73ae`; it contained the upstream tip
plus later merge-diagnostic work. The actual two-parent merge commit is
`8b425317ebf18a49d207c682db72e964a7e75ea9`.

### Scope brought forward

The upstream portion includes the v3.7/v3.8 gameplay and data updates,
Turkiye faction data and art, side-styled command bars/tooltips/tab/observer
HUDs, frame-rate limiting, SDL modern-water reflections/refraction/FFT and
shoreline reuse, the shared hazard-cloud volume, path-queue work, heat/tracer
effects, removal of mod support, the relay and mbedTLS-backed online adapters,
the `-online`, `-relay` and `-control` paths, and the later crash fixes for
tunnels, LAN/online lobbies, challenge loading, special-power view objects and
shadow-volume skinned meshes.

The seven RoundedSpine merge-diagnostic workflows were deliberately not
carried onto `Claude-update`:

- `ios-merge-audit.yml`
- `ios-conflict-group-audit.yml`
- `ios-renderer-conflict-review.yml`
- `ios-renderer-adaptation-prototype.yml`
- `ios-water-track-adaptation.yml`
- `ios-water-track-compile-diagnostic.yml`
- `ios-arm64-integration-diagnostic.yml`

The unvalidated standalone `helicopter-shadow.patch` artifact was excluded as
well. The ordinary `.github/workflows/build-ios.yml` was retained and made
dispatchable by pushes to `Claude-update`.

### Recomputed overlap and conflict decisions

The base-relative tree comparison found ten paths changed on both sides. The
iOS workflow was already byte-for-byte identical, leaving nine semantic text
merges:

| Path | Resolution |
| --- | --- |
| `CHANGELOG.md` | Kept both upstream and iOS entries. |
| `BuildFingerprint.manifest` | Kept both sets of tracked-source additions; the manifest check passes after the completed merge. |
| `CMakeLists.txt` | Kept upstream sources/tests/online stack and iOS host-tool, `iosstartup`, device, shader and packaging wiring. |
| `Data/Patch.str` | Took upstream strings/data changes and retained the iOS-specific additions. |
| `GameEngine.cpp` | Took upstream engine/frame-limit/control changes while preserving the iOS lifecycle/startup path. |
| `GlobalData.cpp` | Took upstream gameplay/data and mod-removal changes; retained the iOS/port fields. No merge-authored simulation behavior was added. |
| `OptionsCatalog.cpp` | Added upstream frame-limit, water and heat-haze options while retaining renderer-capability handling used by iOS. |
| `OptionsMenu.cpp` | Added the upstream Water page and side/UI changes, but kept generalized renderer capability filtering rather than assuming every non-D3D11 path lacks the feature. Unsupported iOS options remain hidden or disabled by the real renderer seam. |
| `ParticleSys.cpp` | Took upstream particle changes and retained the POSIX/iOS GPU-admission restriction that keeps weapon trails, emissive systems and non-additive lighting-sensitive particles on the validated CPU/native paths. |

Files previously suspected to be direct conflicts, including
`PosixDevice9.cpp`, `SdlGpuFrame.cpp/.h`, `W3DWater.cpp`,
`W3DWaterTracks.cpp/.h`, `HazardCloudUnsupported.cpp`,
`dx11runtime_posix.cpp` and `Tools/vendor.sh`, were not final-tree
three-way conflicts. They still received compatibility review because they are
renderer/platform-sensitive.

### iOS adaptations after the merge

The merged renderer keeps the existing iOS/Metal invariants: HLSL is still
compiled through glslang to SPIR-V and then through SPIRV-Cross/
SDL_shadercross to MSL; depth-capture submit ordering, sparse-MSAA handling,
memory-pressure MSAA disable/trim behavior, native lighting/particle passes,
bounded GPU-particle admission and the aircraft/tree silhouette-shadow path are
retained.

Upstream modern water and the shared hazard-cloud volume were integrated with
the SDL GPU path instead of replacing the iOS renderer. The new Water controls
are capability-gated. The upstream shadow-volume skinned-mesh guard remains in
place alongside the iOS silhouette-shadow changes.

The online connection library and mbedTLS build on the Apple toolchain. The
Windows-only GeneralsOnline stack remains Windows-gated. The iOS vendor path
still uses the tested SDL/shadercross behavior and does not fetch the separate
Windows online dependency bundle. `-control` remains opt-in and does not add a
LAN entitlement requirement by itself.

Two integration defects were found by the full ARM64 workflow and fixed rather
than bypassed:

1. `SdlHazardCloudEmbers.cpp` referenced an out-of-scope `target` while
   selecting the pipeline MSAA mode. The ember composite is a main-scene pass,
   so it now follows the shared hazard renderer and queries
   `Back_Buffer_Target()` for the active sample mode.
2. IPA packaging stopped immediately after font staging because the license
   gate did not know the new `onlineconnection` and `mbedtls` static
   libraries. `onlineconnection` is now mapped to the game's license,
   mbedTLS to its Apache-2.0 license file, and
   `test_package_link_libraries.py` covers both entries. The gate was not
   weakened or skipped.

The iOS host-validation script also needed a merge-artifact repair: its CTest
regular expression and device-build block had been spliced incorrectly. The
final script runs the existing graphics tests plus portable
`test_hazard_cloud_bake`, `test_hazard_cloud_shared`, `test_online` and
`test_gameengine`, then performs one iPhoneOS ARM64 configure/build/package
pass.

### Validation

The validated code tip is
`2418298acde9604af7e35b05ae10ea89069ad5d6`.

GitHub Actions run
https://github.com/RoundedSpine/CnCGeneralsZH-Reforged/actions/runs/42440092888
completed successfully on `macos-15`.

The workflow's pre-build helper step passed, including all existing
iOS-prefixed helpers, packaging helpers and
`fingerprint-manifest.sh --check`. The host build passed
`test_gameengine`, `test_hazard_cloud_bake`,
`test_hazard_cloud_shared` and `test_online`. The SDL GPU host checks also
completed on the macOS Metal backend, including AO, heat/native effects, HDR,
MSAA and its memory-pressure fallback, post processing, native particles and
the POSIX GPU draw self-check.

The same workflow then configured against the iPhoneOS 18.5 SDK, compiled and
linked the ARM64 device executable, staged the overlay and fonts, generated the
symbol map, checked the iOS load command and entitlements, ad-hoc signed the
bundle for packaging, and created the unsigned IPA. The uploaded artifact is:

`Reforged-iOS-unsigned-2418298acde9604af7e35b05ae10ea89069ad5d6`

The workflow also uploaded
`Reforged-iOS-build-logs-42440092888`.

This environment did not have original game data, so the E1 replay check could
not be run and no new replay CRC pins were measured. The old pins must not be
claimed for this sync: upstream contains intentional simulation and gameplay
changes that can legitimately move them. The exact post-v3.8 E1 values remain
pending a run with game data.

### Not established by this validation

The successful macOS Metal host tests and iPhoneOS cross-build do not establish
on-device gameplay. Real iPhone/iPad rendering, touch and safe-area behavior,
background/foreground transitions, audio-session recovery, full Turkiye memory
pressure, Water/hazard visual parity, live LAN permission behavior, relay or
online-service sessions and network authentication still require device or
service testing. No claim is made that those behaviors ran in this CI job.

The merge introduced no custom simulation-behavior change. Any replay movement
must be attributed and checked against the upstream gameplay/data changes
before updating E1 pins.



### Final post-merge validation

Further compatibility cleanup after the earlier validation pass was completed on
`Claude-update` and is superseded by the final code-validation tip
`3ca97b6bcf2b15dab6797b7d11b1844027d9e10a`.

GitHub Actions run
https://github.com/RoundedSpine/CnCGeneralsZH-Reforged/actions/runs/42455783710
completed successfully on `macos-15`. The helper/test step, host graphics and
engine validation, iPhoneOS ARM64 configure/compile/link, symbol-map generation,
load-command check, entitlement validation, ad-hoc bundle signing and IPA
packaging all completed successfully. The uploaded IPA artifact is:

`Reforged-iOS-unsigned-3ca97b6bcf2b15dab6797b7d11b1844027d9e10a`

The corresponding diagnostic artifact is:

`Reforged-iOS-build-logs-42455783710`

A packaging regression was found immediately before this final run.
`codesign -d --entitlements -` on the current runner emitted the newer
human-readable entitlement representation, which is not a plist and caused
`plistlib.load()` to fail after the entire ARM64 executable had already linked.
The iOS packaging script restored raw XML extraction with
`codesign -d --entitlements :-`, and
`test_ios_network_permissions.py` now asserts that the packaging path keeps a
machine-readable entitlement extraction form. The final run confirms that the
resulting `signed-entitlements.plist` contains both required kernel memory
entitlements and that the IPA is produced.

The runner also warns that the colon-prefixed entitlement-output syntax is
deprecated. Apple documents the modern explicit XML form as
`codesign -d --entitlements - --xml`. This is a packaging-maintenance risk, not
a failure of the validated IPA; changing to the modern form should receive its
own macOS CI validation before replacing the now-green path.

The E1 replay check remains unexecuted because original game data was not
available. No replay checksum pin was changed by the merge work. Physical-device
Metal rendering, gameplay, touch/safe-area behavior, lifecycle/audio recovery,
memory pressure with the new faction assets, modern-water/hazard visual parity,
LAN permission prompts and live relay/online sessions remain unverified and
must not be inferred from the successful cross-build.
