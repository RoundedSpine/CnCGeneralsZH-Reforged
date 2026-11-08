# iOS / Metal particle performance investigation

Branch: `particle-perf-test` (based on `Claude-update` at `e4d3f7a8b4c87b397429587d5cc926f2a89b3625`)

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
