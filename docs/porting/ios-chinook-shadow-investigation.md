# iOS Chinook: sun-map versus legacy aircraft shadows

## Repository findings (2026-11-08)

The original Zero Hour `AmericaVehicleChinook` object uses `Shadow = SHADOW_VOLUME`, `ShadowSizeX = 89`. Verify modifications/variants in the user's installed game archives when testing. The source tree does not contain the full game-data INI archives.

- `GlobalData.cpp` starts Reforged with `m_shadowMap = TRUE` and `m_shadowMapOnly = TRUE` (Classic Graphics disables the sun map).
- `RTS3DScene::Render` creates a `SunShadow::SceneScope` covering the main scene and flush; nested reflections are excluded.
- `W3DVolumetricShadowManager::renderShadowMap` captures registered shadow-volume casters into sun depth; its success is tracked by the device.
- `W3DVolumetricShadowManager::renderShadows` has an early return when `m_shadowMapOnly && SunShadow::OwnsSceneShadows(device)`. Its per-aircraft path also avoids creating a stencil volume when map ownership exists (or in Classic).
- `W3DProjectedShadowManager::renderShadows` **does not consult map ownership** for ordinary object decals; therefore an aircraft carrying an additional object decal can draw it alongside the map. However, the stock Chinook's `SHADOW_VOLUME` alone does not establish that an extra projected decal exists. Marker/selection decals in `m_decalList` are deliberately separate.
- The earlier on-device stderr shows `[IOS_GRAPHICS] ... shadow_map=1 volumes=1 decals=0 ...` and `[SDL_SHADOW] receivers_ready ...`. That reports graphics state and receiver readiness, **not proof that the capture owned every later shadow pass**. No `[AIRCRAFT_SHADOW]` line was seen in that capture, but the mapped-only early return also prevents that per-aircraft log when ownership succeeds.

## Added diagnostics

When the legacy stencil shadow pass is reached and its relevant state changes:

```
[IOS_SHADOW_PASS] shadow_map=1 map_only=1 scene_owns=1 volumes=1 decals=0 stencil_suppressed=1
```

If `scene_owns=0`, the engine is retaining the legacy shadow fallback; find out why the capture failed or whether a nested/alternate render target is being used. If `scene_owns=1` and `stencil_suppressed=1`, the original stencil shadow pass is already suppressed; investigate any visible second shadow's origin rather than removing it blindly.

If an aircraft-specific ordinary object decal is present:

```
[IOS_AIRCRAFT_DECAL] shadow_type=1 sun_map_owns=1 map_only=1
```

This line is emitted only when the state changes, and only for aircraft object-shadow decals in `m_shadowList` (not selection markers or other generic decals in `m_decalList`). The absence of this entry is not a complete proof of no decals: the decal pass only runs if invoked, and printing is limited by the state-change guard.

## Device comparison

Use the **same Chinook, altitude, camera, view, and map** across all captures. Test Reforged with sun map on; use the shadow report and these diagnostic lines alongside screenshots. Compare with `UseShadowVolumes=No` and `UseShadowDecals=No` separately, then with both, keeping Sun Shadow Map enabled. Also verify a Classic-mode screenshot for the intended fallback. Preserve shadows when the sun capture is unavailable.

Do not globally disable stencil shadows, projected decals or aircraft shadows: this would hide legitimate fallbacks and unrelated status overlays. If extra shadow exists with `scene_owns=1`, next inspect the precise draw source/caster and Metal submission before removing it.
