# G7 — iOS Hybrid water reflection adaptation audit (2026-11-08)

Target: `ios-main-merge`. Do not merge into `Claude-update` without approval.

## Verified source comparison

- Upstream `W3DWater.h` declares `isModernFullMirror()`, includes `Platform/WaterScreenReflection.h`, and stores `WaterScreenReflection::Mode m_waterReflectionMode`.
- Upstream `W3DWater.cpp` defines the predicate as `m_renderingMirror && m_useModernWater && m_waterType == WATER_TYPE_0_TRANSLUCENT && m_waterReflectionMode == WaterScreenReflection::Hybrid`. It also synchronizes the reflection mode at the render boundary, chooses mirror target sizes, controls mirror eligibility, and sets water-shader reflection parameters.
- iOS `W3DWater.h/.cpp` have `m_renderingMirror` and `m_useModernWater`, but lack the reflection-mode state and synchronization. `WaterScreenReflection.h` is absent from the integration branch.
- iOS `SdlGpuFrame` already exposes `Water_Background`, `Scene_Depth`, `Ready_Scene_Depth`, depth-to-R32F resolve, bounded snapshot caching, and invalidation. `Scene_Depth` flushes before recording sampling consumers to preserve Metal submission ordering.
- The upstream `WaterScreenReflection.h` contains an HLSL screen-space ray-tracing shader fragment. Merely copying its enum and method does not implement the rendering feature.
- The iOS ARM64 diagnostic run 42391766013 failed at `W3DTreeBuffer.cpp:2361`: `isModernFullMirror` was missing. The temporary iOS guard in commit `2ed9a038` is not the final integration.

## Integration sequence and acceptance criteria

1. **Render contract:** inventory the water pixel shader generation/binding paths and existing scene colour/depth texture slots; identify how the iOS shader compiler consumes HLSL and how uniform blocks map to SDL GPU. Preserve the existing depth-snapshot flush and resource lifetime rules.
2. **Mode and settings:** port `WaterScreenReflection::Mode`, preference synchronization and mode resolution with an explicit capability check for the iOS GPU backend. Do not advertise Hybrid as active before its shader, scene textures and fallback are working.
3. **Shader and resources:** adapt upstream SSR shader source, scene colour/depth bindings, matrices, viewport and reflection material parameters. Validate HLSL-to-SPIR-V-to-Metal compatibility and sampler/texture slots; retain planar mirror fallback when snapshots/depth are unavailable.
4. **Water passes:** adapt mirror sizing, eligibility and reflection reuse without breaking existing map-water reflections, sky rendering, render-target restores or Metal submission ordering.
5. **Tree shadows:** restore the upstream `isModernFullMirror()` call with the real mode-aware implementation only after the mode is synchronized and the reflection pass is functional. Verify shadows remain enabled outside the active Hybrid mirror pass.
6. **Validation:** compile the coordinated water and renderer translation units with the iPhoneOS SDK, link the full target, build the unsigned IPA, then test Mirror/Hybrid/SkyOnly, paused settings changes, maps with water at multiple elevations, tree silhouettes, and memory/resize/background-resume behavior on device.

## Current risks

- The current iOS code has no `m_waterReflectionMode`; copying the upstream predicate without the associated state would be incorrect.
- The current snapshot cache captures on demand; SSR must consume opaque-scene snapshots taken before water modifies the render target.
- The depth resolve uses a separate submission for Metal safety; do not merge it into a simultaneous read/write render pass.
- iOS renderer and water implementation differ from upstream; a wholesale file replacement would lose iOS-specific functionality.
- Native iOS compile success is not a substitute for device visual testing.

## Branch safety

This document records investigation only. No unverified Hybrid renderer or water-mode changes are applied by this audit.
