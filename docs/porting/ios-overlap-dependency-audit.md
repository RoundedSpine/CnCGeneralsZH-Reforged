# iOS / upstream overlap: initial semantic audit

This is a **source-content spot audit**, not an automatic merge or a compilation result. The compared refs are `main` and `Claude-update`; integrate into `ios-main-merge` only after dependency review.

| Overlap | Upstream change direction | iOS branch concern | Action |
| --- | --- | --- | --- |
| `GameEngine.cpp` | frame-rate limiting, options and online queues | `IOSStartup` and subsystem startup traces | Split offline frame pacing from hosted online queues; retain startup tracing and lifecycle |
| `GlobalData.cpp` | frame-rate and water reflection defaults | fingerprint/CRC logging and iOS-specific globals | Merge corresponding headers/options as a unit; preserve diagnostics |
| `OptionsCatalog.cpp` | water/reflection/FFT and frame-rate accessors | early options, platform-specific live settings | Pair with `GlobalData` declarations and UI; verify settings persistence |
| `OptionsMenu.cpp` | ninth options page for water | iOS has eight pages and scene effects UI | Reconcile page indices/layout/navigation/touch before introducing water page |
| `ParticleSys.cpp` | box emission behavior and ride-object state | existing iOS box velocity and smoke placement fixes | Review both algorithms; do not replace iOS-specific corrections blindly |

## Verification and next actions

1. Obtain full upstream-side tree diff since merge base (GitHub compare truncates at 300); PR #4's 423 paths are net diff only.
2. Compare complete versions of each overlapping source against merge base and enumerate actual conflicting hunks.
3. Trace shared header declarations, settings serializers, render hooks, UI assets and tests before modifying any of these five files.
4. Keep new hosted online queues/adapters out, but retain legacy LAN networking and any required offline interfaces.
5. Compile and run relevant iOS/device tests before marking each feature integrated.

## Three-revision blob audit checkpoint (2026-11-07)

Compared exact blob SHAs for common ancestor `5d6a328`, upstream `main` and `Claude-update` on a first subset of shared-history paths.

- `Libraries/Include/Platform/ModernWaterBuild.h`: upstream SHA **equals merge-base SHA**, while iOS differs. The upstream historical touch has no surviving net file change; preserve the iOS file.
- `.github/workflows/build-ios.yml`: upstream and iOS blob SHAs are **identical**. Merge-base fetch was unavailable in this pass, so do not assert it is a conflict.
- `CHANGELOG.md`, `BuildFingerprint.manifest`, `CMakeLists.txt`, `Data/Patch.str` and `GameEngine.cpp`: all three blob SHAs differ, requiring a proper three-way content comparison.
- `W3DWaterTracks.h`, `SdlPipelineCache.h` also have distinct blobs at all three revisions in preliminary checks.

**Caution:** three distinct blob hashes establish divergence, not overlapping merge hunks. The 34 shared touched paths are a historical upper candidate set, not an actual conflict count. Full merge simulation remains pending.

## Renderer/water three-revision blob checkpoint (2026-11-07)

Verified the blob SHA at merge base, `main`, and `Claude-update` for six additional files. **All six have three distinct blobs**:

| File | Base characters | Upstream characters | iOS characters | Review priority |
| --- | ---: | ---: | ---: | --- |
| `SdlGpuFrame.cpp` | 46,251 | 49,013 | 53,928 | High: iOS GPU-frame additions |
| `SdlGpuFrame.h` | 22,609 | 23,413 | 28,072 | High: interface compatibility |
| `W3DTreeBuffer.cpp` | 114,837 | 114,874 | 114,884 | High: existing uncompiled integration patches |
| `SdlHazardCloudEmbers.cpp` | 7,639 | 8,141 | 7,735 | High: shared hazard cloud dependencies |
| `W3DWater.cpp` | 237,990 | 250,954 | 238,176 | High: upstream water expansion |
| `W3DScene.cpp` | 101,925 | 103,615 | 108,422 | High: iOS scene/render changes |

The values above are character counts returned by the UTF-8 fetch API (not guaranteed on-disk byte sizes); treat them as approximate relative size indicators. Different SHA values prove different final contents, **not** overlapping line conflicts. The merge tool has not been run. Review `SdlGpuFrame.cpp/.h` together, then water/scene/tree as a coordinated rendering change. Do not copy the upstream files wholesale.
