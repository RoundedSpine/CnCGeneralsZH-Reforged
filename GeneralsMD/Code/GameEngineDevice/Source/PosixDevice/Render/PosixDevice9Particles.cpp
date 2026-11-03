// GPL-3.0; see LICENSE.md. Native particle submission uses the active scene, not a DX11 mirror.
#include "PosixDevice9.h"
#include "SdlGpuFrame.h"
#include "SdlResourceMirror.h"
bool PosixDevice9::Native_Particle_Slice(unsigned slice,IDirect3DBaseTexture9 *texture){
    if(!Gpu||!Mirrors)return false;
    if(!texture||texture->GetType()!=D3DRTYPE_TEXTURE){Gpu->Refuse_Particles("unsupported slice texture");return false;}
    D3DSURFACE_DESC desc={};std::string error;
    if(Render_Failed(static_cast<IDirect3DTexture9*>(texture)->GetLevelDesc(0,&desc))){Gpu->Refuse_Particles("slice description");return false;}
    auto *source=Mirrors->Texture(texture,error);
    return Gpu->Particle_Slice(slice,source,desc.Width,desc.Height);
}
bool PosixDevice9::Draw_Native_Particles(unsigned shader,const float view[16],const float projection[16]){
    if(!Gpu)return false;
    if(SunActive||Sun_Scene_Excluded()||!ModernLighting||!EffectsAllowed)return true;
    SdlTarget target;std::string error;
    if(!Resolve_Target(target,error)){Gpu->Refuse_Particles("scene target");return false;}
    const float viewport[6]={float(Viewport.X),float(Viewport.Y),float(Viewport.Width),float(Viewport.Height),Viewport.MinZ,Viewport.MaxZ};
    return Gpu->Draw_Particles(target,viewport,view,projection,shader,true,1);
}
