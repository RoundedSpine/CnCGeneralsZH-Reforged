#include "PosixDevice9.h"
#include "SdlGpuFrame.h"
#include "SdlAoShader.h"
#include "SdlResourceMirror.h"
#include <cstring>
#include "SdlRenderMatrix.h"
#include "Platform/SceneEffects.h"
#include "PosixDevice9Scale.h"
#include <cmath>
bool PosixDevice9::Supports_Ambient_Occlusion() const
{
    return Gpu && Gpu->Supports_Ambient_Occlusion();
}

bool PosixDevice9::Ambient_Occlusion(IDirect3DBaseTexture9 *shroud,const float shroudRect[4])
{
    // The main view can be redirected to the scene texture by Glow/screen filters.
    // Scene scope and caller eligibility exclude reflections and custom views.
    if(!Gpu||SunActive||Sun_Scene_Excluded()||!Viewport.Width||!Viewport.Height) return false;
    SdlTarget target;
    std::string error;
    if(!Resolve_Target(target,error)) return false;
    Posix_Scene_Viewport scaledViewport(Viewport,Gpu,target);
    SdlAo::Constants constants={};
    float clip[16];
    multiply(&Transforms[D3DTS_VIEW]._11,&Transforms[D3DTS_PROJECTION]._11,clip);
    if(!render_inverse(clip,constants.WorldFromClip)) return false;
    for(float value:constants.WorldFromClip) if(!std::isfinite(value)) return false;
    const float range=Viewport.MaxZ-Viewport.MinZ;
    const float x=std::fabs(Transforms[D3DTS_PROJECTION]._11),y=std::fabs(Transforms[D3DTS_PROJECTION]._22);
    if(!std::isfinite(x)||!std::isfinite(y)||x<=0||y<=0||!std::isfinite(range)||range<=1e-8f) return false;
    constants.Viewport[0]=float(Viewport.X);constants.Viewport[1]=float(Viewport.Y);
    constants.Viewport[2]=float(Viewport.Width);constants.Viewport[3]=float(Viewport.Height);
    constants.Projection[0]=x;constants.Projection[1]=y;
    constants.Projection[2]=Viewport.MinZ;constants.Projection[3]=1/range;
    SDL_GPUTexture *shroudTexture=nullptr;
    if(shroud && shroudRect) {
        for(unsigned i=0;i<4;++i) if(!std::isfinite(shroudRect[i])) return false;
        if(shroudRect[2]<=0 || shroudRect[3]<=0) return false;
        shroudTexture=Mirrors->Texture(shroud,error);
        if(!shroudTexture) return false; // Do not ignore an unavailable shroud.
        std::memcpy(constants.ShroudRect,shroudRect,sizeof(constants.ShroudRect));
        if(Gpu->Is_Scene_Target(target)) {
            const float scaleX=float(target.Width)/float(Gpu->Width());
            const float scaleY=float(target.Height)/float(Gpu->Height());
            constants.ShroudRect[0]*=scaleX;constants.ShroudRect[1]*=scaleY;
            constants.ShroudRect[2]/=scaleX;constants.ShroudRect[3]/=scaleY;
        }
        constants.Shroud[0]=1;
    }
    return Gpu->Ambient_Occlusion(target,constants,shroudTexture);
}
namespace SceneEffects {
bool SupportsAmbientOcclusion(IDirect3DDevice9 *device) {
    return device&&static_cast<PosixDevice9*>(device)->Supports_Ambient_Occlusion();
}
bool AmbientOcclusion(IDirect3DDevice9 *device,IDirect3DBaseTexture9 *shroud,const float rect[4]) {
    return device&&static_cast<PosixDevice9*>(device)->Ambient_Occlusion(shroud,rect);
}
}
