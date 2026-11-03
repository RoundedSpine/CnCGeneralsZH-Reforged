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
The shared engine runtime suite and actual Metal readbacks await the workflow.
