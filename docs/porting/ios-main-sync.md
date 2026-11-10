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

## 2026-11-10: v4.0.0 / main sync on `particle-perf-test`

Before the merge, `particle-perf-test` was
`2b90ddbc802dec2cd0b627aecc6ea836b735c36f` ("Fix typo in print statement"),
clean and matching `origin/particle-perf-test`. That SHA was pushed as
`backup/particle-perf-test-pre-main-merge` and was not moved afterward.

The merge-base with the previous sync is
`6a651eb67c596d091fce2db0e9de76d296ef73ae`. Incoming RoundedSpine `main` is
`b13d45b681cbcf6bffba3691c26b3eabd463531c`, which contains upstream
`olcayseygan/CnCGeneralsZH-Reforged`
`61dbc593fb4eecb7af891b80c4c491c3ad2b791d` (v4.0.0). The two-parent merge
commit is `44043069a678099abde43703f4f519f8fe524107`.

Fork-only files on RoundedSpine `main` versus upstream `main` were the seven
`ios-*` diagnostic workflows, `helicopter-shadow.patch`, and
`.github/workflows/build-ios.yml`. The diagnostic workflows and the helicopter
patch were unchanged since the merge-base, so this merge does not reintroduce
them. They are not on this branch. `build-ios.yml` was already this branch's
workflow and stays.

### Conflicts

Eleven content conflicts, all resolved in `44043069`:

- `CHANGELOG.md`: both histories kept (iOS notes, then the upstream 4.0 notes).
- `Patch.str`: Particle Performance strings kept, then the upstream online
  strings. Each block closes with its own `END`.
- `OptionsMenuClassic.wnd`: Detailed Models at 288,322–512,346, HD Textures at
  288,350–512,374, then the frame-rate label and combo. `CheckParticlePerfDiagnostics`
  is the same checkbox as before, moved to 288,434–512,458 so it sits under the
  combo and still ends inside the page (page content bottom is 462). Touch
  size is unchanged.
- `OptionsMenu.cpp`: upstream Detailed Models / HD Textures init and the
  classic-next-start lighting and smoke restore. The Particle Performance
  checkbox still reads and writes `ParticlePerfDiagnostics` and calls
  `ParticlePerfToggle::setEnabled`.
- `W3DParticleSys.h` `BillboardFill`: the performance fields and `hardEdge`.
- `W3DParticleSys.cpp`: `Insert_Sorted_Billboards` passes both contact fade and
  the mine hard edge. The iOS weapon-trail contact-fade exception is unchanged.
  `fill.hardEdge` is `sys->isAttachedToMine()`.
- `Win32BIGFileSystem.cpp`: `theClassicGraphics = getEarlyClassicGraphics()`
  (not a raw `ClassicGraphics` default), then the early Detailed Models and
  HD Textures options.
- `pointgr.h` / `pointgr.cpp` / `sortingrenderer.h` / `sortingrenderer.cpp`:
  one signature carries both `contactFade` (default true) and `hard_edge`
  (default false). Same-run state hashes `contactFade` and not `hard_edge`,
  which is what upstream did for the hard edge. Soft particles still follow
  contact fade. Mine glints still write the plain-depth half-width.

### Particle Performance

These files are identical to `2b90ddbc`:

- `Libraries/Include/Platform/ParticlePerfToggle.h`
- `SdlGpuFrame.cpp`, `SdlGpuFrame.h`, `SdlGpuFrameBatch.cpp`, `PosixDevice9.cpp`
- `sdl3-metal-windowless.patch`, `sdl3-metal-gpu-timing.patch`,
  `sdl3-metal-pass-timing.patch`

`vendor.sh` still applies those Metal patches in that order, after the
windowless patch. `FlushLimit` still defaults to 6 (`ZH_GPU_FLUSH_LIMIT`).
The Options.ini key is still `ParticlePerfDiagnostics`. The Display checkbox
name, label and tooltip are unchanged. A host program that only includes the
toggle header prints `default=0`, `on=1`, `off=0` with the environment unset.
The `[PARTICLE_PERF]` and `[PARTICLE_COST]` `fprintf` lines were not edited.
The merge adds no new `SdlGpuFrame::Flush` call site; altitude lines and lamp
trays go through existing draws.

`m_useShadowVolumesForSkins` is still present and still defaults to FALSE.
Tree silhouette shadows and skinned volume meshes read the top HLOD
(`Get_LOD_Count()-1`) because upstream stores the coarsest LOD first. That is
the Nov 7 silhouette path updated for the new LOD order, not a reintroduction
of the Nov 6 aircraft or skinned-volume experiment.

### iOS adaptations

Detailed Models and HD Textures stay visible and default on, matching upstream
and `test_gameengine`'s `CHECK_EQ` of both to 1. `build-ios.sh` still calls
`vendor.sh --skip-art`, so the optional archives are not downloaded.
`mountOverRetail` mounts nothing when those files are absent, which is the
current IPA. They are inert until someone bundles the archives. Device
low-memory warnings were already recorded without these assets; adding the
archives later is a separate memory decision. They are not hidden and not
forced off.

Archives outside the executable folder are still not mounted. No new static
library was added. Online sources that were already linked stay linked.
`test_package_link_libraries.py` still accepts the licensed onlineconnection
and mbedTLS set and rejects an unknown library. The merge diff adds no thread
and no `SDL_OpenURL`.

The bridge vertex buffer is upstream's 32000 vertices (64000 indices, under
65535), about 1 MB. That is not treated as an iOS memory change.

`CFBundleShortVersionString` stays the hardcoded `0.1.0` in
`Platform/iOS/Info.plist.in`. CMake's game version string is v4.0.0. The IPA
version was not bumped to follow it.

`SdlMouse` asks the cursor decoder for display content scale times 32. On an
iOS scale of 2–3 that is 64–96, and the decoder picks the nearest of 32 and
48. That is the drawn cursor only. Touch hit-testing was not device-checked.

New wreck FX in `FXListReforged.ini` (`MortarDust`, `CarCrushDebris`,
`JetExplosionGroundDust`, `JetExplosionGroundDebris`, `ArtilleryBarrageDust`,
`AirF_AuroraDustRing`) use the existing particle path. They add event bursts,
so earlier Particle Performance device numbers are not an A/B baseline against
this build. Instrumentation strings are unchanged. Mines still skip GPU spawn.

`optionsmenu_layout.py selfcheck` reports 75 catalog rows agreeing with the
window and `Patch.str`. Particle Performance remains menu-only, not a catalog
row. The fingerprint was regenerated in the merge commit: 4685 files,
`--check` ok, including `ParticlePerfToggle.h` and the three Metal patches.

### Host follow-up

`test_native_particle_admission.py` extracts `spawnGpuParticle`, which casts
to `size_t`. Debian g++ 12 does not put `size_t` in the global namespace from
`<vector>` or `<cstdint>`. The harness now includes `<cstddef>` and
`using std::size_t`. The production function is unchanged. The workflow test
step runs `artpack.py selfcheck` (no retail archives) before
`test_host_tools.py`.

### Local validation

This host has no iOS SDK, no CMake and no vendored SDL. Passed here: refpack
(6 tests, 133025 checks; upstream `refencode.cpp` only warns about misleading
indentation), every Python helper in the `build-ios.yml` test step except
`test_host_tools.py` (it invokes `cmake`, which is not installed), the C++
touch, camera-pan, feedback-diagnostic and startup-pump helpers, the options
menu selfcheck (75 rows), `artpack.py selfcheck`, fingerprint `--check`
(4685), and the Particle Performance toggle default/on/off program.

Not run here, and not claimed: a `ZH_PLATFORM_IOS=1` engine compile, the
shader suite, `test_gameengine`, the hazard-cloud and online CTest suites,
Metal host GPU tests, and IPA packaging. Those are the macos-15 job.
`test_host_tools.py` was not run. E1 replay pins were not remeasured and were
not edited. There is no original game data here. Upstream gameplay (death
momentum, jet break-off, missiles that fly on, bridge pathfinding,
`DeathMomentumDriftShare`) can move replay CRCs. That movement is upstream's,
not a resolution added in this merge.

### CI

The macos-15 ARM64 workflow result, its Actions URL and the unsigned IPA
artifact name are filled in after the push of this sync. A green package does
not establish on-device gameplay, touch or safe-area behavior, lifecycle or
audio recovery, memory with the faction assets or the optional art archives,
water or hazard visuals, or live online or LAN sessions.

