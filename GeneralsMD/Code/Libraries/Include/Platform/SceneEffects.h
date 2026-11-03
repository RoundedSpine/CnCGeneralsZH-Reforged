#pragma once
struct IDirect3DDevice9;
struct IDirect3DBaseTexture9;
namespace SceneEffects {
// Visual-only parameters shared by the native and Windows atmosphere paths.
struct AtmosphereParameters {
    float Haze[4] = {0,1,0,1}; // top, thickness, strength, reach
    float HazeColour[4] = {0,0,0,-1.0e6f}; // rgb, water height
    float HazeSun[4] = {0,0,1,0}; // direction, warmth
    float Fade[4] = {0,1,0,0}; // start, scale, strength, ground height
    float Horizon[4] = {}, Tint[4] = {}, MapSize[4] = {};
};
constexpr unsigned MaxFirelights=12;
struct PointLight {float Position[4],Diffuse[4],Ambient[4];};
bool SupportsFirelight();
bool SupportsFirelight(IDirect3DDevice9 *device);
bool Firelight(IDirect3DDevice9 *device,const PointLight *lights,unsigned count);
bool SupportsHeatHaze();
bool SupportsHeatHaze(IDirect3DDevice9 *device);
bool BeginHeatHaze(IDirect3DDevice9 *device,IDirect3DBaseTexture9 *source);
void EndHeatHaze(IDirect3DDevice9 *device);
class HeatHazeScope {
    IDirect3DDevice9 *Device;
    bool Active;
public:
    HeatHazeScope(IDirect3DDevice9 *device,IDirect3DBaseTexture9 *source):Device(device),Active(BeginHeatHaze(device,source)){}
    ~HeatHazeScope(){if(Active)EndHeatHaze(Device);}
    explicit operator bool() const{return Active;}
    HeatHazeScope(const HeatHazeScope&)=delete;
    HeatHazeScope &operator=(const HeatHazeScope&)=delete;
};
bool SupportsLighting();
bool SupportsLighting(IDirect3DDevice9 *device);
bool SupportsAtmosphere();
bool SupportsAtmosphere(IDirect3DDevice9 *device);
void SetLightingMode(IDirect3DDevice9 *device,bool modern,bool hemisphere);
void SetTeamGlow(IDirect3DDevice9 *device,bool enabled);
bool Atmosphere(IDirect3DDevice9 *device,const AtmosphereParameters &parameters,
    IDirect3DBaseTexture9 *sky,IDirect3DBaseTexture9 *shroud,const float rect[4]);
// Queries capabilities without allocating effect resources. False after a runtime failure.
bool SupportsAmbientOcclusion(IDirect3DDevice9 *device);
// Current display, for game code that does not depend on a device API.
bool SupportsAmbientOcclusion();
// Opaque main view only. Unsupported devices keep the unmodified scene.
bool AmbientOcclusion(IDirect3DDevice9 *device,IDirect3DBaseTexture9 *shroud,const float rect[4]);
}
