# iOS / Metal particle performance investigation

Branch: `particle-perf-test` (based on `Claude-update` at `e4d3f7a8b4c87b397429587d5cc926f2a89b3625`)

The code pass dated 2026-11-11 is at the bottom of this file (`Phases 1–6 code pass`). The notes above it describe the branch through the 4.0.0 sync and the device logs captured before that pass. Do not treat those logs as an after-measurement of the new commits.

## Status and measurement integrity

This is an **instrumentation-first** investigation. The previous device report noted `constrained=0`, headroom in available memory, and zero GPU ring waits; those observations do not prove that Metal shader/fragment work is cheap. **No controlled baseline-versus-optimized FPS result is available in this branch yet.** Do not infer a speedup from the counters.

| Phase | Implemented | Baseline | After | Outstanding |
|---|---|---|---|---|
| 1 – Instrumentation | Opt-in CPU frame aggregation of particle draw preparation, smoke/fire gathering, CPU time in GPU particle submission, visible system and particle counts, native live count | Not captured | Not captured | True GPU elapsed and depth-capture timing; separate CPU fill timing currently includes other particle render work |
| 2 – Template attribution | Opt-in top-eight CPU billboard preparation totals per template and smoke-light evaluation count | Not captured | Not captured | GPU draw cost, depth capture, overdraw and per-template fire-light gathering time |
| 3 – Shared smoke lighting | **No lighting approximation or cache enabled** | Not captured | Not captured | Validate hot templates and baseline before altering lighting |
| 4 – GPU eligibility | Code audit below | Not captured | Not captured | Visual A/B; no CPU-only types migrated |
| 5 – FireLook | CPU FireLook path audited below | Not captured | Not captured | Separate FireLook GPU/CPU profiling and validated improvement |
| 6 – Dynamic quality | **Not enabled** | Not captured | Not captured | Instrument fragment overdraw and screen-space contribution before implementing time-scaled policy |

Do not describe the unfinished stages above as optimizations or verified successes. The repo still needs a successful macOS iOS Actions run plus on-device validation.

## Turn on diagnostics

Set `ZH_PARTICLE_PERF=1` in the game process environment. With the flag absent or not exactly `1`, the added timing and per-template accumulation paths are bypassed. Every **120 particle render invocations** a summary appears on stderr:

```
[PARTICLE_PERF] frames=120 cpu_render_ms=... fire_smoke_gather_ms=... gpu_submit_cpu_ms=... active_systems=... processed=... drawn=... native_live=... smoke_light_candidates=... gpu_elapsed_ms=unavailable depth_capture_ms=unavailable
[PARTICLE_COST] stage=billboard template=FXFooExplosion ms_per_frame=... drawn_per_frame=... smoke_light_tests_per_frame=...
```

Values are interval averages, **not worst-frame** values. `cpu_render_ms` covers much of `doParticles` before native GPU submission, including the fire-light gathering, billboard worker jobs, and CPU fallback drawing. `fire_smoke_gather_ms` covers fire-light preparation, heat-haze grouping and picking smoke lights. `gpu_submit_cpu_ms` is CPU-side work leading up to and including GPU evaluate/draw calls; it must **not** be interpreted as Metal GPU duration. `processed` is the number of CPU-backed particles in frame-visible systems; `drawn` is the manager's on-screen count (including field particles). `native_live` is live records, not proven visible GPU fragments. `smoke_light_candidates` is the number of candidate lights for the scene's billboard fill jobs, not the number of evaluated smoke-particle/light pairs; the per-template `smoke_light_tests_per_frame` counts those attempted pairs.

The top-eight template entries measure **billboard CPU worker elapsed wall time**; concurrent workers can make summed per-template wall times exceed the overall frame's critical-path CPU time. They include colour, billboard packing, smoke lighting, and FireLook age-seed packing. A template not listed in the top eight may still dominate non-billboard or GPU costs.

## Existing path audit and cautions

- GPU-native path: bounded spawn ring, compute evaluation, per-system entries, atlas, two additive/alpha draws. Native participation is controlled by `ParticleSystemTemplate::isGpuEligible` and runtime admission.
- On non-Windows platforms, eligibility currently additionally requires **additive**, **non-emissive**, and **not weapon-trail**. All platforms reject non-PARTICLE types, SMUD textures, ground-aligned particles, wind-driven systems, attached child particles, FireLook, flipbooks, and particles using up-toward-emitter.
- The current CPU smoke-lighting path calls `pickSmokeLights` once per billboard system, then `lightSmoke` on visible smoke particles. It already restricts lights by the system AABB and caches gathered fires between logic updates. A further quantized spatial cache could introduce visible bands or popping; test with A/B screenshots before enabling one.
- FireLook is **not** a generic GPU-budget problem. Its CPU billboard path writes an age-and-seed parameter to `v2` for each of four vertices. The device's FireLook sampling and fire/heat integrations require separate equivalence checks; simply deleting `!m_fireLook` is unsafe.
- Actual GPU elapsed time needs a supported Metal command-buffer/device timing source, and transparent overdraw needs a controlled capture (Xcode GPU Frame Capture or equivalent). Neither is measured by the current aggregated CPU logging.
- Screen/depth capture may execute outside `doParticles`; do not attribute its time to a template without causal capture instrumentation.

## On-device test plan

1. Build the **unsigned ARM64 device IPA** from `particle-perf-test` and check that the online libraries remain linked in the normal build output. Install in the same way as the `Claude-update` control.
2. Use the same map, camera, graphic settings (Reforged lighting, smoke-fire lighting, heat haze, resolution), device temperature, and scene sequence. Run with `ZH_PARTICLE_PERF=1`; retain timestamped stderr and frame-time measurements.
3. Capture at least three intervals each: idle/no fire, a few simultaneous fires, and a heavy smoke/fire fight. Repeat with Reforged lighting off but otherwise unchanged. Compare median, P95 frame time and the per-template averages; don't compare FPS averages alone.
4. Inspect top templates, `fire_smoke_gather_ms`, `cpu_render_ms`, native particle count, and CPU submission time. A CPU bottleneck should show increased wall time at a consistent scene load; a GPU bottleneck requires Metal elapsed/capture evidence, not speculation.
5. For future changes, A/B toggles must preserve the original code path and be logged alongside all measurements. Compare screenshots for alpha ordering, smoke gradients, FireLook animation, glow, trails, and heat haze before making any quality change the default.
6. Confirm memory metrics stay unconstrained and that hardware GPU ring waits remain zero; if either changes, revisit the original hypothesis.

## Device log review — revision 4305204e (2026-11-08)

This run booted the SDL GPU Metal renderer and exited normally. It did **not** print `[PARTICLE_PERF]` or `[PARTICLE_COST]`; the process environment `ZH_PARTICLE_PERF=1` was apparently not enabled. It also reported `[SDL_PARTICLES] requested=0 available=0 max_records=8192 CPU_alpha_and_weapon_trails=1`: native particle admission was not used, so this capture is **not** a GPU particle-path A/B baseline.

**Important correction to the earlier GPU-ring hypothesis:** shutdown statistics for this run show **12,726 ring waits**, **11,919 mid-frame flushes**, **1,891,135 draws**, and **1,238 presents**. Earlier logs showing zero waits must not be extrapolated to this run. The count is strong motivation to instrument ring-wait duration and submissions per pass, but counts alone do not distinguish CPU scheduling, GPU saturation, and synchronous wait duration. There are approximately **1,528 recorded draws per present** (not necessarily actual hardware draw calls after batching).

Metal also rejected shared hazard-cloud shader 4: `TerrainImage` and `SceneDepth` both mapped to `[[texture(0)]]`; the shared cloud renderer failed initialization. Fix and validate the shader's resource binding translation separately before attributing performance or appearance to shared volumetric effects. Reforged lighting and volumetric smoke options were enabled, and output resolution was 2868x1320.

Available memory remained plentiful (about 4.6 GiB at the latest periodic sample); `constrained=0` was logged. This supports prioritizing render and submission profiling, though not a conclusion about GPU elapsed time.

**Next measured comparison:** enable the diagnostic environment for device launches and capture two otherwise identical scenarios; add ring-wait wall time, GPU stage timing and true transparent overdraw before making the dynamic particle budget default.

## Next work required

Add genuine Metal GPU timestamp measurements and depth/heat capture instrumentation at their own render passes; per-template cost for CPU fire gathering and GPU submission; safe shared-light computation A/B; additive-type GPU eligibility matrix; FireLook-specific CPU-vs-fragment analysis; dynamic time-based visible-area and overdraw budgets. Each must receive separate verified performance and visual evidence before claiming a completed optimization.

## Build validation — GitHub Actions 42470226285 (2026-11-08)

The uploaded `Reforged-iOS-build-logs-42470226285.zip` was examined against this branch. The Actions job ended **failure** at *Build device IPA*, and the unsigned IPA artifact step was skipped. However, the device C++ build reached `[1623/1624] Linking CXX executable generals.app/generals` and `ios-symbols.py` emitted `iOS symbol map: 56169 text symbols`; particle-related files including `W3DParticleSys.cpp`, `SdlGpuParticles.cpp`, and `PosixDevice9Particles.cpp` compiled in the device build. Host `test_sdl_particles` reported `failures=0 backend=metal` and `Graphics shader/GPU readback checks passed`. This is **compile/link and host-test evidence only**, not a verified packaged device IPA or performance improvement.

The last logged failure was two HTTP 504 responses from `curl` immediately after the iOS symbol map, corresponding to the network-only Liberation Fonts download in `Tools/stage-ios-fonts.sh`, invoked from `Tools/make-ios-ipa.sh` before archive creation. Both download endpoints target GitHub-hosted fonts and the download's exit status prevents packaging. Retrying the workflow under working network conditions, or making the verified font archive available from a cached, SHA-256-checked source, is needed before claiming IPA success. The earlier vendor Mbed TLS download initially encountered HTTP 504 as well but was later recovered, as evidenced by compilation/linking of `libmbedtls.a`.

Do not use the Action's failure as evidence that the particle renderer fails to compile. Conversely, do not interpret a successful cross-link as proof that on-device rendering, Metal elapsed-time queries, or phases 3–6 pass validation. This artifact is a build log, not a controlled on-device particle diagnostic capture; no before/after FPS statistics or worst-template rankings can be extracted from it.

## Follow-up instrumentation — commits 14a442e and 21e0858 (2026-11-08)

Opt-in CPU billboard instrumentation now measures the wall time spent specifically in `lightSmoke` and FireLook age/seed packing for each billboard fill, independently of total billboard packing time. `[PARTICLE_COST] stage=billboard` gains `smoke_light_cpu_ms_per_frame`, `firelook_cpu_ms_per_frame` and `firelook_particles_per_frame`. Additional top-eight rankings use `stage=smoke_light_cpu` and `stage=firelook_pack_cpu` and report `ms_per_frame` and `particles_per_frame`. The pre-existing billboard ranking and visibility rejects remain available. `stage=smoke_light_cpu` uses drawn billboards as its denominator, not only those receiving smoke light; use the accompanying `smoke_light_tests_per_frame` to understand light workload.

These are diagnostic timers embedded in a per-particle loop: they introduce intentional observer overhead *when enabled*, and can overstate absolute costs due to per-call clock reads. They are absent on the disabled path except for a predictable branch. Compare stage rankings within a single diagnostic build and measure true user-facing FPS with instrumentation **disabled** as a control. There is no approximation, FireLook migration, particle count reduction or dynamic quality policy in these commits.

The Liberation Fonts packaging download now tries independent MacPorts/OmniOS release mirrors with the same pinned SHA-256 (commit ae38b50); no font substitutions or online-link changes were made. A macOS Actions and on-device rerun **has not yet been observed** for these changes. No baseline/after improvements are claimed.

**Device reading order:** enable the in-game Particle Performance diagnostic option (or `ZH_PARTICLE_PERF=1` where the environment is settable); collect 120-render-call intervals; compare the `stage=smoke_light_cpu` and `stage=firelook_pack_cpu` rankings, then cross-check the parent billboard total and frame-time P95. At identical graphics settings compare several idle, fire and dense-smoke runs, both with and without the profiling flag. Do not interpret `gpu_submit_cpu_ms` as GPU elapsed time; `gpu_elapsed_ms=unavailable` and `depth_capture_ms=unavailable` remain outstanding.


## On-device particle diagnostic capture — generals-stderr(4).log (2026-11-08)

Capture identifies `[IOS_BUILD] revision=a6c554e8f1de renderer=SDL_GPU_Metal`, native 2868×1320 output, Reforged lighting and volumetric smoke enabled. **Note:** revision `a6c554e8` predates the later smoke/FireLook diagnostic source edits in the repository, but the log **does contain** `smoke_light_cpu_ms_per_frame`, `firelook_cpu_ms_per_frame` and the separate `stage=smoke_light_cpu` / `stage=firelook_pack_cpu` lines. The apparent mismatch between reported build fingerprint and logged source features should be checked for build-fingerprint stamping/cached-object behavior before claiming exact commit attribution.

Eighteen `[PARTICLE_PERF]` summaries were present (120 render invocations per interval). The first interval: `cpu_render_ms=0.133`, `fire_smoke_gather_ms=0.004`, active systems 37.9, CPU particles processed 106.3, drawn 12.4. At the peak reported interval: `cpu_render_ms=1.629`, `fire_smoke_gather_ms=0.372`, active systems 640.6, processed 2866.7 and drawn 555.3. These are **interval averages**, not worst-frame latency or end-to-end frame time. Across every interval `native_live=0.0`. Separate initialization confirms `[SDL_PARTICLES] requested=0 available=0 max_records=8192 CPU_alpha_and_weapon_trails=1`: the native compute path is **not active**, so this run cannot evaluate GPU particle migration.

Top observed *interval-average per-template* CPU billboard costs were `CarpetBombExplosionShockwave` 0.080ms with 138.1 drawn/frame and `BuggyNewAirDeathSubExplosionSmoke` 0.078ms with 62.9 drawn/frame. The largest separately measured `lightSmoke` costs were 0.009ms (`CarpetBombExplosionShockwave`) and 0.008ms (`BuggyNewAirDeathSubExplosionSmoke`). The largest measured FireLook seed/age packing cost was 0.004ms (`BuggyNewAirDeathSubExplosionFlameSlave`). **These small self-times do not establish that lighting/FireLook fragment shaders are cheap**; no GPU elapsed data exists. The light-picking phase can exceed the actual per-particle `lightSmoke` cost in the high-count templates (e.g. around 0.047ms vs 0.008ms for `BuggyNewAirDeathSubExplosionSmoke`). Because per-particle timers are expensive, evaluate diagnostic overhead before assuming absolute values.

There is a much larger *unattributed* render scheduling signal: shutdown reports **1,998,747 recorded draws across 1,562 presents**, **13,677 mid-frame flushes**, and **15,132 ring waits**, with **zero flushes due to the 2,048-draw limit**. This is approximately 1,280 recorded draws per present. These lifetime counters are not milliseconds and cannot identify particle-specific overhead; instrument time waited for the ring, command buffer submissions, and per-pass draw/fragment cost. Logging 2,243 `[IOS_SHADOW_PASS]` lines also adds avoidable stderr traffic and should be disabled or interval-aggregated for a clean profiling comparison.

The Metal hazard-cloud shader still fails to create because two fragment parameters (`TerrainImage` and `SceneDepth`) both occupy `[[texture(0)]]`; shared hazard-cloud initialization fails. This is **independent of the successful particle CPU profiling** but confounds visual comparisons when volumetric smoke is enabled. Fix resource bindings independently and verify the shared cloud path.

At periodic frame 1342: `footprint_mib=1563.8`, `available_mib=4580.2`, `constrained=0`. Memory pressure remains an unlikely primary explanation **for this capture**. Startup and game shutdown completed normally (`[IOS_EXIT] normal process exit`).

### Prioritized next validation

1. Fix and independently test the shared hazard-cloud shader texture-binding collision, preserving scene/depth semantics.
2. Instrument ring-wait **duration**, command-buffer submissions and per-render-pass CPU/GPU cost before attributing the severe FPS collapse to smoke or FireLook.
3. Make native-particle activation explicitly controllable on iOS and validate eligibility before claiming an A/B comparison. Do not globally enable ineligible alpha, FireLook or trails.
4. Repeat the same scenes with Reforged lighting on/off and diagnostic logging on/off; capture actual frame-time P50/P95 and matching particle counts. No baseline-versus-optimized speedup can yet be reported.


## Follow-up: shared-cloud Metal binding repair and GPU submission timings (2026-11-08)

**Changes are experimental and require build/device validation.** The device log `generals-stderr(4).log` shows the shared hazard-cloud **Heat** program refusing Metal creation: `TerrainImage [[texture(0)]]` and `SceneDepth [[texture(0)]]`. The generated HLSL originally declares them at distinct sampled slots (terrain t6 and depth t0); the shadercross path, however, categorizes fetch-only `.Load` images separately and can assign Metal texture indices that collide with normally sampled textures. Shared shader source now uses `SampleLevel` with the existing point samplers at the exact centers of source, bin, index and terrain texels. The texture coordinates are computed from integer atlas coordinates and each texture's actual dimensions. This changes the fetch API but is intended to return the **same nearest texel values**: it does not change density, noise, raymarch steps, palette, coverage, depth comparison, or effect count. This equivalence still requires a GPU image comparison and Metal build validation.

The `test_shader_sdl` shared-cloud test now checks the `slots_kept` contract across generated MSL, rejecting duplicate `[[texture(n)]]` indices as well as missing samplers and out-of-range slots. The iOS host workflow now builds/runs this test **before** cross-compilation. A passing static translation test does not prove a successful device run, pixel equivalence, or GPU performance.

**New opt-in log:** `[GPU_SYNC_PERF] intervals=120 flushes=... ring_waits=... ring_wait_cpu_ms=... submit_calls=... submit_cpu_ms=... note=cpu_wall_time_not_gpu_elapsed`. Collection is controlled by the existing `ParticlePerfToggle` runtime flag, usually selected by the in-game Particle Performance setting or `ZH_PARTICLE_PERF=1`. `ring_wait_cpu_ms` is elapsed CPU time waiting on oldest mid-frame fence when the flight limit is exceeded. `submit_cpu_ms` is CPU wall time inside `SDL_SubmitGPUCommandBufferAndAcquireFence` in `Submit_Flush`. These counters are sums over 120 calls to `Take_Timing`, **not per-frame averages**, and do not include every submission route or measure Metal GPU elapsed time. `ring_wait_cpu_ms` is already included in existing `FlushMs`; do not add them to obtain frame duration. The existing `ZH_GPU_FLUSH_LIMIT` behavior and fence queue ordering are unchanged.

**Next A/B:** collect matching 120-interval logs in low particle and high fire/smoke scenes with profiling enabled, then compare `ring_wait_cpu_ms/intervals` and `submit_cpu_ms/intervals`; test actual P50/P95 frame time with profiling disabled. Keep graphics settings, view, unit actions and resolution unchanged. Confirm shared heat/cloud shaders initialize, render and match visually. Only after those measurements should we change batching/fence policy or make particle quality tradeoffs. No speedup has been measured from these commits.


## Build 42482624855 — host test environment failure (2026-11-08)

The uploaded `Reforged-iOS-build-logs-42482624855.zip` reports 1,656 successful macOS host compilation/link steps (including `SdlGpuFrame.cpp`, `SdlHazardCloudSharedShader.cpp`, `test_shader_sdl`, and particle test binaries). The user-folder test printed `4 tests, 44 checks, 0 failed`; then the new direct invocation of `test_shader_sdl` stopped with `FAIL: ZH_USER_DATA_DIR is not set` before exercising its shader assertions. The test harness intentionally refuses to use the CI runner's real user-data directory. Commit `981cad94` passes `ZH_USER_DATA_DIR="$host/userfolders-scratch"` into the shader test invocation, matching the existing scratch-directory convention.

**Status:** successful host compilation is verified; the new shader regression test, device cross-compilation, unsigned IPA packaging, and on-device Metal shared-cloud initialization were **not** validated by this log. A subsequent successful workflow and device log are needed before declaring either the shader binding fix or performance profiling validated.

## Build 42483971377 — shared shader test succeeded, unrelated water tests failed (2026-11-08)

The uploaded `Reforged-iOS-build-logs-42483971377.zip` includes only `build.log` and stops after the macOS host graphics checks. `test_userfolders` passed (4 tests, 44 checks). `test_shader_sdl` ran (33 tests, 687,486 checks, 10 failed): all eight cases in `classic_water_uses_the_original_program_and_separate_cache_identity` failed at the macOS-only `actual==modern` assertion, and both `direct3d11_modern_water_source_matches_the_upstream_baseline` hardcoded hash comparisons failed. The shared-cloud SPIR-V/MSL translation and texture-binding assertion emitted no failures. The overall shader suite also reports 46 reference shaders and 23 additional shaders accepted by the Metal compiler, with 1,034 vertex/pixel pipeline pairs linked. That Metal acceptance count does not by itself prove the new shared-cloud shader is creatable on the device; shared-cloud initialization still requires on-device validation. `spirv-val` was unavailable (skipped).

Comparison against `Claude-update` shows no water shader generator modifications on `particle-perf-test`; the new test additions are restricted to shared-cloud binding checks. The failing water expectations are therefore inherited relative to the protected branch, not caused by the shader fix in this branch. To keep this diagnostic branch's CI gate scoped to its changed behavior, commit `944aad2f` runs `test_shader_sdl shared_cloud_programs_compile_validate_and_translate` in the macOS host preflight instead of gating on the complete suite. This does **not** change the shader code or the existing water test expectations; the water regressions remain visible and unresolved and should receive an independent fix after verifying their intended contract. This build never reached iOS device cross-compilation or IPA packaging; the next run remains the validation gate.

## Metal GPU completion timing and render-operation attribution (2026-11-08)

On `particle-perf-test`, the pinned SDL3 3.4.16 Metal source now receives an additional explicit `sdl3-metal-gpu-timing.patch` after the existing headless-device patch. The patch is recorded in `BuildFingerprint.manifest`. It registers a completion handler **before** each Metal command buffer is committed, but only while `ParticlePerfToggle` profiling is enabled. The callback reads Apple's `MTLCommandBuffer.GPUStartTime` and `GPUEndTime` after completion, adds nonzero intervals to an opt-in aggregate, and reports absent/invalid timestamps separately. No extra `waitUntilCompleted`, fence polling, render pass, or synchronous GPU query is added. The patch is limited to the SDL Metal backend; Vulkan/D3D12 code paths are untouched. Profiling enable transitions bump an epoch so callbacks from a previous disabled run are discarded.

At each 120-call `[GPU_SYNC_PERF]` interval the renderer now adds `gpu_cmd_completed`, `gpu_cmd_unavailable`, `gpu_cmd_total_ms`, `gpu_cmd_max_ms`, and `gpu_time_source`. **These measurements are genuine GPU execution durations for completed Metal command buffers, not a frame's end-to-end GPU wall-clock time and not a particle-only GPU timing**. The completed command buffers may be submitted in earlier reporting intervals (asynchronous completion); their duration sum may overlap and includes graphics, copy, compute and presentation-associated GPU work. Do not divide that sum by frame count and describe it as per-frame GPU latency. When unavailable, zero samples **must not** be interpreted as a zero-cost frame. Compare matching controlled scenes, normalizing by completed submission count while checking the sample coverage.

To attribute command volume, the same log gains `render_passes` (including separately emitted MSAA depth clear passes), `depth_snapshots`, `blits`, `copy_passes`, `recorded_draws`, and `upload_mib`. These are **CPU-side recorded operations during the interval**, not guaranteed completed device work. `upload_mib` counts staged byte volume including stream padding, not texture residency. Counts are gated on the profiler flag to minimize overhead when disabled. Other direct GPU operations outside `Record_Batch` may not be reflected in these counts, though the Metal completion hook sees all Metal submissions on this backend.

**Verification remains required** for the Objective-C hook on the pinned SDL backend, final unsigned ARM64 IPA, and nonzero/accurate GPU timestamps on the iPhone 16 Pro Max. Compare profiling on/off to measure handler overhead. If timing is unavailable, do not fabricate GPU elapsed time; use host Metal GPU tests and device instruments to diagnose API support. For fragment overdraw or separate particle-vs-lighting GPU pass timings, a later pass-labeled Metal counter sample or Xcode GPU frame capture is still necessary.

## Combined iPhone GPU pass + fragment diagnostic (2026-11-08)

On `particle-perf-test` the Metal source patch `sdl3-metal-pass-timing.patch` extends the
opt-in asynchronous command-buffer profiler with render-pass stage-boundary counter
samples. It is applied **after** `sdl3-metal-gpu-timing.patch` and requires the
same `ZH_PARTICLE_PERF=1` / in-game Particle Performance diagnostic switch.
It does not intentionally alter gameplay shaders, blend modes, render order,
pass boundaries, or normal resource lifetime. The profiler adds counter-sample
buffer allocations and completion callbacks; measure its own overhead with a
profiling-disabled control.

The engine labels render passes as `scene_backbuffer`, `intermediate`,
`depth_only`, or `unlabelled`. Every 120 `Take_Timing` intervals the
`[GPU_PASS_PERF]` lines report:

- `completed`, `unavailable`, `gpu_ms`, `max_ms`: real, **asynchronously completed**
  Metal render-stage timestamp samples, summed per bucket; not one frame's GPU latency.
- `stats_completed`, `stats_unavailable`, `fragment_invocations`, `fragments_passed`:
  actual Metal fragment statistics where the GPU supports the standard
  `MTLCommonCounterSetStatistic` at render-stage boundaries.
- `fragment_invocations_per_passed`: **pass-wide** fragment work to visibility output.
  It is an overdraw/fragment-discard indicator, not the literal number of smoke
  fragments per screen pixel, and isn't alpha-only because a pass can contain
  opaque geometry and transparency. Zero when `fragments_passed=0` is not
  a verified ratio; consult the sample and unavailable counts.

The separate `[GPU_TRANSPARENCY_PERF]` line reports accepted
`blended_draws` and `additive_draws`; its
`backbuffer_blended_scissor_mpixels` and
`backbuffer_scissor_fullscreen_equiv` are a **very loose upper bound**
assuming every pixel of the viewport/scissor were shaded by every accepted
back-buffer blended draw. These fields can vastly overestimate transparent
fragment execution when small sprite quads use a full viewport. Do not call
them a measured heatmap or divide them into the GPU fragment count to infer
pixel-exact overdraw. They are useful as a transparent-workload marker beside
the hardware counters.

**One controlled device diagnostic capture:**

1. Install the unsigned experimental IPA; confirm `[IOS_BUILD]`, `[SDL_PARTICLES]`,
   shader creation, Reforged lighting, and current resolution in the log.
2. Enable the in-game Particle Performance diagnostic and capture stderr.
   Hold an idle scene for at least 3 full 120-call windows.
3. Without changing graphics options, generate several repeatable fires and
   dense smoke plumes for at least 3 windows. Then produce repeated explosions,
   projectiles and other overlapping alpha effects for at least 3 windows.
   Keep the camera, resolution, unit count, and zoom as consistent as possible.
4. Capture at least 3 windows of a final idle/recovery scene. Retain
   `[PARTICLE_PERF]`, `[PARTICLE_COST]`, `[GPU_SYNC_PERF]`,
   `[GPU_PASS_PERF]`, `[GPU_TRANSPARENCY_PERF]`, `[SDL_MEMORY]` and
   shader startup messages; also record actual frame-time/FPS from an
   independent frame-time source. Do not equate recorder time with GPU time.
5. Repeat equivalent scenes with profiling **disabled** to isolate the
   overhead of counters and callbacks. For lighting A/B, do a separate run
   with Reforged lighting off instead of toggling in the middle of a window.

If `stats_unavailable` dominates, the hardware statistics are not usable
on this device/configuration; if `completed=0` with `unavailable>0` it
means no usable pass timestamps, not zero GPU cost. GPU completion callbacks
can land in the next 120-call interval. Don't infer a per-frame P95 from the
aggregate or blame a single shader from a mixed render pass.

Exact transparent-only overdraw heatmaps or attribution to a specific smoke
shader require additional controlled GPU capture or a separate instrumented
render pass. This diagnostic is designed to choose the next intervention
without lowering graphical quality prematurely.

**Validation:** this implementation is experimental until GitHub Actions
compiles the pinned Objective-C Metal backend, packages an IPA, and an actual
A18 Pro run provides nonzero supported timing/fragment samples.

## Calibrated GPU pass timing and flush-cause attribution (2026-11-09)

The first A18 Pro device capture (revision `fd82e36e5e0f`) verified that GPU
command-buffer execution timestamps and render-pass boundary counter samples
can be collected on-device. **Fragment statistics were unavailable**:
`stats_completed=0` throughout that capture. That is a lack of hardware
data, not evidence of zero fragment overdraw.

That capture's raw pass-counter totals were not directly comparable to
`GPUStartTime/GPUEndTime` command-buffer totals. Metal's GPU counter timestamps
are ticks on a GPU clock, **not necessarily nanoseconds**. The experimental
patch now calls `MTLDevice sampleTimestamps:gpuTimestamp:` twice at least
250 ms apart to derive nanoseconds per GPU tick. It reports

- `[GPU_COUNTER_CALIBRATION] ns_per_gpu_tick=... valid=...`: treat
  `GPU_PASS_PERF.gpu_ms` as calibrated time only when `valid=1`.
  Samples recorded before the scale is ready are counted unavailable.
  Asynchronous callback arrivals mean adjacent 120-call buckets need not match.
- `[GPU_PASS_PERF]` now has 12 buckets: unlabelled, scene backbuffer,
  intermediate, depth only, ocean FFT, heat, MSAA colour, native particles,
  post-processing, sun smoke shadow, gamma, and MSAA depth. These are
  *render-pass sites*, not exact per-shader GPU times. Some mixed/unknown passes
  remain unlabelled.
- `[GPU_FLUSH_CAUSES]` counts calls to `SdlGpuFrame::Flush` by explicit
  originating subsystem: unclassified, batch limit, ocean, scene depth,
  ambient occlusion, atmosphere, heat, MSAA, native particles, smoke,
  sun shadow and maintenance. Counts are attempts, including failures; do
  not interpret them as time spent or as a safe list of flushes to remove.

**Next controlled comparison:** Allow the main menu to cycle several times
with profiling enabled, keeping lighting, resolution, and thermal state
consistent. Collect `GPU_COUNTER_CALIBRATION`, all 12 `GPU_PASS_PERF`
buckets, `GPU_SYNC_PERF`, `GPU_FLUSH_CAUSES`, `GPU_TRANSPARENCY_PERF`,
`PARTICLE_PERF` and memory samples. Then run the same menu workload with
profiling disabled; compare independent frame times/FPS to quantify the
instrumentation overhead. The per-pass profiler still allocates sample
buffers per pass and uses asynchronous handlers, and can itself be expensive.

Do not change blend/shader behavior or reduce particle quality on the basis
of uncalibrated samples or scissor-derived overdraw estimates.

## 4.0.0 wreck FX and earlier device numbers (2026-11-10)

The v4.0.0 sync (merge `44043069`, upstream `61dbc593`) does not change
Particle Performance instrumentation. `ParticlePerfToggle.h`, `SdlGpuFrame.cpp`,
`SdlGpuFrame.h`, `SdlGpuFrameBatch.cpp`, `PosixDevice9.cpp` and the three
pinned SDL Metal patches are identical to `2b90ddbc`. The Options.ini key,
the Display checkbox, the default-off state, the six-submit `FlushLimit`, and
the `[PARTICLE_PERF]`, `[PARTICLE_COST]`, `[GPU_SYNC_PERF]`, `[GPU_PASS_PERF]`,
`[GPU_FLUSH_CAUSES]`, `[GPU_TRANSPARENCY_PERF]` and `[GPU_COUNTER_CALIBRATION]`
formats are unchanged.

Upstream added death and impact FX lists: `MortarDust`, `CarCrushDebris`,
`JetExplosionGroundDust`, `JetExplosionGroundDebris`, `ArtilleryBarrageDust`
and `AirF_AuroraDustRing`. They are ordinary event bursts on the existing
particle path, including smoke-light and FireLook where those templates use
them. Device numbers captured before this sync are not an A/B baseline against
a 4.0.0 build. Compare only runs that both include these FX, on the same map,
camera and graphics settings.

## Phases 1–6 code pass (2026-11-11)

No new on-device FPS numbers were captured for these commits. The host check
`test_particle_perf_policy` passes and is run by the unsigned IPA job before
cmake. It does not execute Metal. Run `42958526043` on `f3d51940` stopped in
that host step: `test_native_particle_admission` still compiled only the old
`isGpuEligible` wrapper, which now calls `gpuStayReasons` and
`ParticleGpuClass`. The harness compiles both production functions and still
requires weapon trails, alpha smoke, emissive bodies, FireLook, flipbooks,
wind, ground-aligned cards, up-towards-emitter cards, SMUD names and attached
children to stay on the CPU. The same step would next have rejected
`BuildFingerprint.manifest`, which did not list the five new policy files.
The unsigned device IPA was not produced by that run. Do not quote a speedup.

The earlier device captures still stand as the **baseline**, and only for
builds before this pass. Peak interval in `generals-stderr(4).log`: about
1.63 ms average CPU particle render, 0.37 ms fire/smoke gather, 0 native
particles, and smoke-light CPU under 0.01 ms on the hottest template. Shutdown
showed on the order of 15k GPU ring waits and ~1,280 recorded draws per
present. That is a submission/GPU signal, not a smoke-light CPU signal.
`native_live` was 0 because the native path was not requested.

| Phase | What changed | Bottleneck it can address | A/B | Measured after |
|---|---|---|---|---|
| 1 | `[PARTICLE_PERF]` now includes smoke-light evaluations, CPU heat-gather time, present interval, and calibrated Metal native-particle / depth / heat totals when a profiling window has them | CPU vs GPU attribution | `ZH_PARTICLE_PERF=1` or the Display checkbox. Off has no log and no per-particle timers | Not re-run on device |
| 2 | `Particle cost: NAME ms / particles` plus `[PARTICLE_STAGE]` for CPU lighting, billboard prep, Metal particles, depth, overdraw | Finding the worst template and stage | Same diagnostic flag | Not re-run. Previous worst CPU templates were `CarpetBombExplosionShockwave` and `BuggyNewAirDeathSubExplosionSmoke`, under 0.1 ms |
| 3 | One unscaled flame moment per system per frame, shared by fire lights, heat splats and smoke lights | CPU fire-light gathering. Exact, not an approximation | `ZH_FIRE_MOMENT_SHARE=0` restores the three walks. Default is share on. `fire_moment_reuses` counts hits | Not re-run |
| 4 | Eligibility reasons logged once. No new template class was moved by default | GPU offload of safe additive cards, only if opted in | `ZH_GPU_OFFLOAD_ADDITIVE=1` admits already-eligible additive spawns below the CPU cap and turns the native path on. `-gpuparticles 0` forces it off | Not re-run. Default picture is unchanged |
| 5 | FireLook age/seed is written inside `Write_Billboard`. Formula unchanged. Still forbidden on the GPU path | Removes a second 4-vertex touch. Does not touch the fragment shader | None beyond the diagnostic stage timer. There is no FireLook GPU switch | Not re-run. Prior pack cost was ~0.004 ms |
| 6 | Draw budget | GPU transparent overdraw, by not shading cards that cannot show, then small cards and small alpha filler when presents miss 60 Hz | `ZH_PARTICLE_BUDGET=0` disables it. Default on | Not re-run. Until a present interval exists, only the exact invisible cull runs |

### Approximations

- Phase 3 is exact. Callers scale a copy of the shared moment. `ZH_FIRE_MOMENT_SHARE=0` is the control.
- Phase 4 default does not change which particles are born on the CPU. The offload flag can change composite order: native additive draws are not in the CPU sorted-billboard pool, so overlapping smoke can cover a GPU flame differently. That is why it is off.
- Phase 5 is exact. FireLook is not moved to the GPU because the native particle pixel shader does not sample the age/seed in `v2`. Removing `!m_fireLook` would flatten the flame noise. The fragment cost is still inside the mixed scene pass.
- Phase 6 invisible cull (alpha 0, or additive vertex color 0) does not change the picture. `ALWAYS_RENDER` is never cut. Sub-pixel cull and alpha density cull start only after the present interval exceeds 16.7 ms and 20 ms. Density keeps the large cards in an alpha system and drops ones smaller than a fraction of that system's largest visible particle (down to half, at a 60 ms present). Very small particles are culled rather than merged: a merged quad would change the sort key and the cover. There is no global particle-count cap and no effect is removed while the frame is on time.

### What stays on the CPU, and why

`[GPU_PARTICLE_AUDIT]` prints the template counts at startup. A template can carry more than one reason.

| Reason | Why it stays |
|---|---|
| not_particle, smud, ground, wind, attached, shader, flipbook, up_emitter | The GPU closed form does not evaluate or draw these |
| firelook | Needs the CPU `v2` age/seed. See phase 5 |
| weapon_trail | iOS contact-fade exemption is applied when CPU billboards are inserted |
| emissive | The GPU record has no emissive flag |
| alpha_lighting | Alpha smoke is smoke-lit and sorted with the other CPU billboards. The native alpha draw is a separate newest-first pass and does not run `lightSmoke` |

Eligible native templates remain finite-lifetime, non-emissive, non-trail **additive** particles without FireLook, flipbooks, wind, ground alignment, smudge names or attached children. Below the CPU cap they still spawn on the CPU unless `ZH_GPU_OFFLOAD_ADDITIVE=1`.

### What to run on device

1. Build the unsigned ARM64 IPA from `particle-perf-test` the same way as before. Confirm the log still shows the online libraries linked and `[IOS_BUILD]`.
2. Turn on **Particle Performance** in Display (or `ZH_PARTICLE_PERF=1`). Leave `ZH_PARTICLE_BUDGET` unset and `ZH_GPU_OFFLOAD_ADDITIVE` unset for the first capture. Play the same heavy fire/smoke beat as the baseline, then an idle beat.
3. Read, every 120 particle renders:
   - `[PARTICLE_PERF]` — `cpu_render_ms` and `fire_smoke_gather_ms` are per-frame averages. `smoke_light_evals` is particle-light pairs. `fire_moment_reuses` should be > 0 when fires are also lighting smoke or heat. `budget_invisible` may be > 0; `budget_small` and `budget_density` should stay 0 while `present_ms` is about 16.7.
   - `gpu_elapsed_ms`, `depth_capture_ms`, `heat_gpu_ms` — `unavailable` until `[GPU_COUNTER_CALIBRATION] valid=1`. After that they are **sums over the latest Metal window**, not this CPU interval's average. Do not add them to `cpu_render_ms`.
   - `Particle cost:` — worst billboard templates. `[PARTICLE_STAGE]` — `billboard_prep_ms_per_frame` is worker wall time and can exceed the frame. `scene_overdraw` is fragment invocations per passed fragment for the whole scene pass, not smoke-only.
   - `[GPU_PARTICLE_AUDIT]` once at startup, and `[SDL_PARTICLES] requested=0` unless the offload or `-gpuparticles` / `ZH_GPU_PARTICLES=1` is set.
4. Repeat with `ZH_PARTICLE_BUDGET=0` if you need the old draw of zero-alpha cards. Repeat a slow scene with the budget left on and compare `budget_small` / `budget_density` against `present_ms`.
5. Only after that, try `ZH_GPU_OFFLOAD_ADDITIVE=1` on the same camera. Expect `requested=1`, `offload_additive=1`, and `native_live` above 0 in a fight. Screenshot additive flames against alpha smoke before trusting it. `ZH_FIRE_MOMENT_SHARE=0` is the lighting control; the picture should match the default.

Anything still open: device A/B numbers, a particle-only GPU timestamp (the native bucket is the particle pass, the scene bucket is mixed), fragment statistics if the A18 Pro again reports `stats_completed=0`, and a safe way to draw offloaded additive particles inside the CPU sort instead of underneath it.


