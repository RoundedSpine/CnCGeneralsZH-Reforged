#pragma once
struct IDirect3DDevice9;
struct IDirect3DBaseTexture9;
namespace SceneEffects {
// Queries capabilities without allocating effect resources. False after a runtime failure.
bool SupportsAmbientOcclusion(IDirect3DDevice9 *device);
// Current display, for game code that does not depend on a device API.
bool SupportsAmbientOcclusion();
// Opaque main view only. Unsupported devices keep the unmodified scene.
bool AmbientOcclusion(IDirect3DDevice9 *device,IDirect3DBaseTexture9 *shroud,const float rect[4]);
}
