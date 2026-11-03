#pragma once
struct IDirect3DDevice9;
struct IDirect3DBaseTexture9;
namespace SceneEffects {
// Opaque main view only. Unsupported devices keep the unmodified scene.
bool AmbientOcclusion(IDirect3DDevice9 *device,IDirect3DBaseTexture9 *shroud,const float rect[4]);
}
