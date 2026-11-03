// GPL-3.0; see LICENSE.md. Compute ABI, consecutive records, wrap, draw, reset and pressure.
#include "Common/GameType.h"
#include "GameClient/ParticleClosedForm.h"
#include "SdlGpuFrame.h"
#include "SdlColour.h"
#include "Platform/ParticleShaders.h"
#include "Platform/ParticleBudget.h"
#include "sdl3shadercompile.h"
#include <SDL3/SDL.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
static unsigned failures=0;
#define CHECK(x) do{if(!(x)){++failures;fprintf(stderr,"FAIL PARTICLES line %d: %s\n",__LINE__,#x);}}while(0)
static std::map<std::string,unsigned> strides(const std::vector<unsigned char> &spv){
    std::vector<uint32_t> w(spv.size()/4);std::memcpy(w.data(),spv.data(),spv.size());
    std::map<unsigned,std::string> names;std::map<unsigned,unsigned> arrays,steps;
    for(size_t at=5;at<w.size();){const unsigned n=w[at]>>16,op=w[at]&65535;CHECK(n&&at+n<=w.size());if(!n||at+n>w.size())break;
        if(op==5)names[w[at+1]]=reinterpret_cast<const char*>(&w[at+2]);
        if(op==29)arrays[w[at+1]]=w[at+2];
        if(op==71&&n>=4&&w[at+2]==6)steps[w[at+1]]=w[at+3];at+=n;
    }
    std::map<std::string,unsigned> result;for(const auto &entry:arrays)if(steps.count(entry.first))result[names[entry.second]]=steps[entry.first];return result;
}
struct Aged {float Position[3],Size;uint32_t Colour[2];float Angle;uint32_t Kind;};
static_assert(sizeof(Aged)==32,"particle readback ABI");
int main(){
    std::string error,msl;std::vector<unsigned char> spv;
    CHECK(SDL3_Compile_HLSL(ParticleShaders::Evaluation(true),SdlShaderStage::Compute,spv,error));
    const auto layout=strides(spv);CHECK(layout.at("Spawn")==sizeof(ParticleGpuSpawn));CHECK(layout.at("SystemEntry")==sizeof(ParticleGpuSystemEntry));CHECK(layout.at("Particle")==sizeof(Aged));
    CHECK(SDL3_Translate_MSL(spv,SdlShaderStage::Compute,msl,error));
    CHECK(SDL3_Compile_HLSL_To_SPIRV(ParticleShaders::Vertex(true),true,spv,error));CHECK(SDL3_Translate_SPIRV_To_MSL(spv,true,msl,error));
    CHECK(SDL3_Compile_HLSL_To_SPIRV(ParticleShaders::Pixel(),false,spv,error));CHECK(SDL3_Translate_SPIRV_To_MSL(spv,false,msl,error));
    if(failures){fprintf(stderr,"%s\n",error.c_str());return 1;}
#if defined(__APPLE__)
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER,"dummy");SDL_SetHint("ZH_SDL_GPU_METAL_WINDOWLESS","1");
#else
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER,"offscreen");
#endif
    if(!SDL_Init(SDL_INIT_VIDEO)){printf("SKIP PARTICLES GPU: %s\n",SDL_GetError());return 77;}
    std::unique_ptr<SdlGpuFrame> frame(SdlGpuFrame::Create(nullptr,33,23,error));
    if(!frame){printf("SKIP PARTICLES GPU: %s\n",error.c_str());SDL_Quit();return 77;}
    bool fresh=false;CHECK(!frame->Prepare_Particles(NativeParticleBudget::Records+1,&fresh));
    CHECK(frame->Prepare_Particles(17,&fresh)&&fresh);CHECK(frame->Prepare_Particles(17,&fresh)&&!fresh);
    ParticleGpuSystemEntry entry={};entry.m_shaderType=1;entry.m_flags=1;entry.m_color[0]=0.6f;entry.m_color[1]=0.3f;entry.m_color[2]=0.2f;
    entry.m_drift[0]=0.02f;entry.m_gravity=-0.02f;CHECK(frame->Write_Particles(1,0,&entry,1));
    const float fade=1;CHECK(frame->Write_Particles(2,0,&fade,1));
    std::vector<ParticleGpuSpawn> spawn(17);
    for(unsigned i=0;i<17;++i){auto &s=spawn[i];s.m_pos[0]=i*0.05f;s.m_pos[1]=-0.1f*i;s.m_pos[2]=2+i;
        s.m_vel[0]=0.1f;s.m_vel[2]=-0.04f;s.m_birthFrame=100;s.m_lifetime=100;s.m_size=1+i*0.01f;
        s.m_sizeRate=0.02f;s.m_velDamping=i%3==0?0:i%3==1?1:0.9999f;s.m_sizeRateDamping=s.m_angularDamping=1;
        s.m_angularRateZ=0.03f;s.m_alphaKeys[0]=0xffffffff;s.m_alphaKeys[1]=0xffffffff;
    }
    CHECK(frame->Write_Particles(0,0,spawn.data(),17));CHECK(frame->Evaluate_Particles(15,5,102,0.25f));
    std::vector<unsigned char> bytes;CHECK(frame->Read_Particles(bytes));CHECK(bytes.size()==5*sizeof(Aged));
    if(bytes.size()==5*sizeof(Aged))for(unsigned i=0;i<5;++i){Aged aged;std::memcpy(&aged,bytes.data()+i*sizeof(Aged),sizeof(aged));
        ParticleClosedFormState cpu;particleClosedFormEvaluate(spawn[(15+i)%17],entry,2.25f,&cpu);
        CHECK(std::abs(aged.Position[0]-cpu.m_pos.x)<0.001f);CHECK(std::abs(aged.Position[1]-cpu.m_pos.y)<0.001f);CHECK(std::abs(aged.Position[2]-cpu.m_pos.z)<0.001f);
        CHECK(std::abs(aged.Size-cpu.m_size)<0.001f);CHECK(std::abs(aged.Angle-cpu.m_angleZ)<0.001f);CHECK((aged.Kind>>16)==1);
        CHECK(std::abs(SdlColour::real(uint16_t(aged.Colour[0]))-cpu.m_color.red)<0.002f);
    }
    // Terrain clamping and zero visibility must affect the evaluated output, not just bookkeeping.
    const float ground[4]={10,10,10,10},mapping[4]={0,0,0.5f,0.5f};CHECK(frame->Particle_Ground(ground,2,2,mapping));
    entry.m_flags=3;CHECK(frame->Write_Particles(1,0,&entry,1));CHECK(frame->Evaluate_Particles(0,3,102,0.25f));CHECK(frame->Read_Particles(bytes));
    if(bytes.size()==3*sizeof(Aged)){Aged aged;std::memcpy(&aged,bytes.data(),sizeof(aged));CHECK(std::abs(aged.Position[2]-10)<0.001f);}
    const float hidden=0;CHECK(frame->Write_Particles(2,0,&hidden,1));CHECK(frame->Evaluate_Particles(0,3,102,0.25f));CHECK(frame->Read_Particles(bytes));
    if(bytes.size()==3*sizeof(Aged))for(unsigned i=0;i<3;++i){Aged aged;std::memcpy(&aged,bytes.data()+i*sizeof(Aged),sizeof(aged));CHECK(aged.Kind==0);}
    CHECK(frame->Write_Particles(2,0,&fade,1));spawn[0].m_lifetime=1;CHECK(frame->Write_Particles(0,0,spawn.data(),1));
    CHECK(frame->Evaluate_Particles(0,1,103,0));CHECK(frame->Read_Particles(bytes));if(bytes.size()==sizeof(Aged)){Aged aged;std::memcpy(&aged,bytes.data(),sizeof(aged));CHECK(aged.Kind==0);}
    // Actual storage-buffer vertex draw: authored zero alpha must retain ONE/ONE additive RGB.
    auto target=frame->Back_Buffer_Target();frame->Clear_Back_Buffer(true,true,true,0xffffffff,1,0);
    CHECK(frame->Particle_Slice(0,target.Colour,33,23));
    entry={};entry.m_shaderType=1;entry.m_flags=1;entry.m_color[0]=0.8f;entry.m_color[1]=0.2f;entry.m_color[2]=0.1f;
    spawn[0]={};spawn[0].m_birthFrame=100;spawn[0].m_lifetime=100;spawn[0].m_pos[2]=0.5f;spawn[0].m_size=1.2f;
    spawn[0].m_velDamping=spawn[0].m_sizeRateDamping=spawn[0].m_angularDamping=1;
    CHECK(frame->Write_Particles(1,0,&entry,1));CHECK(frame->Write_Particles(0,0,spawn.data(),1));CHECK(frame->Evaluate_Particles(0,1,101,0));
    frame->Clear_Back_Buffer(true,true,true,0x99000000,1,0);
    float identity[16]={};for(unsigned i=0;i<4;++i)identity[i*5]=1;const float viewport[6]={0,0,33,23,0,1};
    CHECK(frame->Draw_Particles(target,viewport,identity,identity,1,false,1));std::vector<uint8_t> pixels;
    CHECK(frame->Read_Back(target.Colour,33,23,pixels));if(pixels.size()==33*23*4){const unsigned at=(11*33+16)*4;CHECK(pixels[at+2]>190);CHECK(pixels[at+3]==153);}
    CHECK(frame->Resize(41,27));CHECK(frame->Prepare_Particles(17,&fresh)&&!fresh);
    CHECK(frame->Handle_Memory_Pressure());CHECK(!frame->Particles_Available());CHECK(!frame->Prepare_Particles(17,&fresh));
    std::unique_ptr<SdlGpuFrame> failed(SdlGpuFrame::Create(nullptr,17,11,error));CHECK(failed!=nullptr);
    if(failed){CHECK(failed->Prepare_Particles(17,&fresh));CHECK(!failed->Write_Particles(0,17,spawn.data(),1));
        CHECK(!failed->Particles_Available());CHECK(!failed->Prepare_Particles(17,&fresh));}
    printf("PARTICLES: failures=%u backend=%s\n",failures,SDL_GetGPUDeviceDriver(frame->Device()));
    failed.reset();frame.reset();SDL_Quit();return failures?1:0;
}
