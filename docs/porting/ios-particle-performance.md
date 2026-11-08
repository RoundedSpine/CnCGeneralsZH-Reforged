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

## Next work required

Add genuine Metal GPU timestamp measurements and depth/heat capture instrumentation at their own render passes; per-template cost for CPU fire gathering and GPU submission; safe shared-light computation A/B; additive-type GPU eligibility matrix; FireLook-specific CPU-vs-fragment analysis; dynamic time-based visible-area and overdraw budgets. Each must receive separate verified performance and visual evidence before claiming a completed optimization.
