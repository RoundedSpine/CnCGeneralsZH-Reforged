// GPL-3.0; see LICENSE.md. Opt-in bounded overflow, while the device's normal particles stay on CPU.
#include "W3DDevice/GameClient/GpuParticles.h"
#include "Common/GlobalData.h"
#include "dx8wrapper.h"
#include "PosixDevice9.h"
#include "SdlGpuFrame.h"
#include <cstdlib>
#include <cstdio>
namespace {
PosixDevice9 *device(){return static_cast<PosixDevice9*>(DX8Wrapper::_Get_D3D_Device());}
SdlGpuFrame *frame(){auto *d=device();return d?d->Get_Gpu():nullptr;}
bool requested(){
    if(TheGlobalData&&TheGlobalData->m_gpuParticleCapacityOverride>=0)return TheGlobalData->m_gpuParticleCapacityOverride>0;
    const char *value=std::getenv("ZH_GPU_PARTICLES");return value&&value[0]=='1'&&value[1]=='\0';
}
}
namespace GpuParticles {
bool Supported(){auto *f=frame();const bool asked=requested(),active=asked&&f&&f->Particles_Available();
    static unsigned last=~0u;const unsigned status=unsigned(asked)*2+unsigned(active);
    if(last!=status){last=status;fprintf(stderr,"[SDL_PARTICLES] requested=%d available=%d max_records=%u CPU_alpha_and_weapon_trails=1\n",int(asked),int(active),NativeParticleBudget::Records);}
    return active;
}
bool Prepare(unsigned capacity,bool *fresh){auto *f=frame();if(fresh)*fresh=false;return Supported()&&f->Prepare_Particles(capacity,fresh);}
void Write_Spawns(unsigned first,const ParticleGpuSpawn *data,unsigned count){if(auto *f=frame())f->Write_Particles(0,first,data,count);}
void Write_Systems(unsigned first,const ParticleGpuSystemEntry *data,unsigned count){if(auto *f=frame())f->Write_Particles(1,first,data,count);}
void Write_Fades(const float *data,unsigned count){if(auto *f=frame())f->Write_Particles(2,0,data,count);}
void Set_Ground(const float *data,unsigned width,unsigned height,const float map[4]){if(auto *f=frame())f->Particle_Ground(data,width,height,map);}
void Set_Slice_Texture(unsigned slice,IDirect3DBaseTexture9 *texture){if(auto *d=device())d->Native_Particle_Slice(slice,texture);}
void Evaluate(unsigned first,unsigned count,unsigned now,float smooth){if(auto *f=frame())f->Evaluate_Particles(first,count,now,smooth);}
void Draw(unsigned shader,const Camera &camera){if(auto *d=device())d->Draw_Native_Particles(shader,camera.view,camera.projection);}
void ReleaseDevice(){if(auto *f=frame())f->Release_Particles();}
}
