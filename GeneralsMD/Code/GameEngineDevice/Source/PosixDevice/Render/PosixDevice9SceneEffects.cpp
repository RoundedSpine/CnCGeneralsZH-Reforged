// Distributed under the repository's GPL-3.0 terms; see LICENSE.md.
#include "PosixDevice9.h"
#include "SdlGpuFrame.h"
#include "SdlAtmosphereShader.h"
#include "SdlResourceMirror.h"
#include "SdlRenderMatrix.h"
#include <cmath>
#include <cstdio>
#include <cstring>

void PosixDevice9::Set_Lighting_Mode(bool modern,bool hemisphere)
{
    hemisphere=modern&&hemisphere;
    if(ModernLighting!=modern||HemisphereAmbient!=hemisphere)
        fprintf(stderr,"[SDL_LIGHTING] modern=%d hemisphere=%d\n",int(modern),int(hemisphere));
    ModernLighting=modern;HemisphereAmbient=hemisphere;
}
bool PosixDevice9::Supports_Atmosphere() const {return Gpu&&Gpu->Supports_Atmosphere();}
bool PosixDevice9::Atmosphere(const SceneEffects::AtmosphereParameters &parameters,IDirect3DBaseTexture9 *sky,
    IDirect3DBaseTexture9 *shroud,const float shroudRect[4])
{
    if(!Gpu||SunActive||Sun_Scene_Excluded()||!Viewport.Width||!Viewport.Height
        ||(parameters.Haze[2]<=0&&parameters.Fade[2]<=0))return false;
    float values[sizeof(parameters)/sizeof(float)];std::memcpy(values,&parameters,sizeof(parameters));
    for(float value:values)if(!std::isfinite(value))return false;
    if(parameters.Haze[1]<=0||parameters.Haze[3]<=0||parameters.Fade[1]<=0
        ||(parameters.Fade[2]>0&&(!sky||parameters.MapSize[0]<=0||parameters.MapSize[1]<=0)))return false;
    SdlTarget target;std::string error;if(!Resolve_Target(target,error))return false;
    SdlAtmosphere::Constants constants={};constants.Parameters=parameters;
    float clip[16];multiply(&Transforms[D3DTS_VIEW]._11,&Transforms[D3DTS_PROJECTION]._11,clip);
    if(!render_inverse(clip,constants.WorldFromClip))return false;
    for(float value:constants.WorldFromClip)if(!std::isfinite(value))return false;
    const float range=Viewport.MaxZ-Viewport.MinZ;
    if(!std::isfinite(range)||range<=1e-8f)return false;
    constants.Viewport[0]=float(Viewport.X);constants.Viewport[1]=float(Viewport.Y);
    constants.Viewport[2]=float(Viewport.Width);constants.Viewport[3]=float(Viewport.Height);
    constants.DepthInfo[2]=Viewport.MinZ;constants.DepthInfo[3]=1/range;
    SDL_GPUTexture *skyTexture=nullptr,*shroudTexture=nullptr;
    if(sky) {skyTexture=Mirrors->Texture(sky,error);if(!skyTexture)return false;constants.Flags[1]=1;}
    if(shroud) {
        if(!shroudRect)return false;
        for(unsigned i=0;i<4;++i)if(!std::isfinite(shroudRect[i]))return false;
        if(shroudRect[2]<=0||shroudRect[3]<=0)return false;
        shroudTexture=Mirrors->Texture(shroud,error);if(!shroudTexture)return false;
        std::memcpy(constants.ShroudRect,shroudRect,sizeof(constants.ShroudRect));constants.Flags[0]=1;
    }
    return Gpu->Atmosphere(target,constants,skyTexture,shroudTexture);
}
namespace SceneEffects {
bool SupportsLighting(IDirect3DDevice9 *device) {return device&&static_cast<PosixDevice9*>(device)->Get_Gpu();}
bool SupportsAtmosphere(IDirect3DDevice9 *device) {return device&&static_cast<PosixDevice9*>(device)->Supports_Atmosphere();}
void SetLightingMode(IDirect3DDevice9 *device,bool modern,bool hemisphere) {
    if(device)static_cast<PosixDevice9*>(device)->Set_Lighting_Mode(modern,hemisphere);
}
void SetTeamGlow(IDirect3DDevice9 *device,bool enabled) {if(device)static_cast<PosixDevice9*>(device)->Set_Team_Glow(enabled);}
bool Atmosphere(IDirect3DDevice9 *device,const AtmosphereParameters &parameters,
    IDirect3DBaseTexture9 *sky,IDirect3DBaseTexture9 *shroud,const float rect[4]) {
    return device&&static_cast<PosixDevice9*>(device)->Atmosphere(parameters,sky,shroud,rect);
}
}
