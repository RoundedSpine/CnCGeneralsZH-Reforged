// Production shader and device integration checks, including redirected scene targets.
#include "PosixDevice9.h"
#include "SdlGpuFrame.h"
#include "Platform/SceneEffects.h"
#include "ffshader.h"
#include "engineshader.h"
#include "ffvertex.h"
#include "sdl3shadercompile.h"
#include <SDL3/SDL.h>
#include <cstdio>
#include <cmath>
#include <cstring>
#include <vector>
#include <regex>
#include <set>
static unsigned failures=0;
#define CHECK(x) do{if(!(x)){++failures;fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);}}while(0)
struct Vertex {float x,y,z;uint32_t colour;float u,v,halfWidth,age;};
static void uniqueMetalTextures(const std::string &msl) {
    std::set<std::string> bindings;std::regex attribute(R"(\[\[texture\(([0-9]+)\)\]\])");
    for(std::sregex_iterator it(msl.begin(),msl.end(),attribute),end;it!=end;++it)CHECK(bindings.insert((*it)[1]).second);
}
static void variants(){
    CombinerDescription base={};base.StageCount=1;auto &s=base.Stages[0];s.TextureBound=true;
    s.ColourOperation=FF_TOP_MODULATE;s.ColourArgument1=FF_TA_TEXTURE;s.ColourArgument2=FF_TA_DIFFUSE;
    s.AlphaOperation=FF_TOP_MODULATE;s.AlphaArgument1=FF_TA_TEXTURE;s.AlphaArgument2=FF_TA_DIFFUSE;
    for(unsigned i=0;i<9;++i){auto d=base;if(i<2)d.SoftParticle=i+1;else if(i<4)d.FireLook=i-1;else d.DrawEffect=i-3;
        std::string source,error,msl;std::vector<unsigned char> spirv;
        CHECK(CombinerShader_Generate(d,COMBINER_SHADER_TARGET_SDL3_GPU,source));
        bool ok=SDL3_Compile_HLSL_To_SPIRV(source,false,spirv,error)&&SDL3_Translate_SPIRV_To_MSL(spirv,false,msl,error);
        CHECK(ok);if(ok)uniqueMetalTextures(msl);if(!ok)fprintf(stderr,"variant %u: %s\n",i,error.c_str());
    }
    for(unsigned i=0;i<4;++i){VertexPipelineDescription d={};d.FVF=FF_FVF_XYZ|FF_FVF_DIFFUSE|FF_FVF_TEX2;
        d.StageCount=1;d.SoftParticle=i&1;d.FireLook=i&2;
        std::string source,error,msl;std::vector<unsigned char> spirv;
        CHECK(VertexShader_Generate(d,VERTEX_SHADER_TARGET_SDL3_GPU,source));
        bool ok=SDL3_Compile_HLSL_To_SPIRV(source,true,spirv,error)&&SDL3_Translate_SPIRV_To_MSL(spirv,true,msl,error);
        CHECK(ok);if(ok)uniqueMetalTextures(msl);if(!ok)fprintf(stderr,"vertex variant %u: %s\n",i,error.c_str());
    }
    for(auto program:{ENGINE_SHADER_WATER_TRAPEZOID,ENGINE_SHADER_WATER_RIVER,
        ENGINE_SHADER_WATER_TRAPEZOID_FOAM,ENGINE_SHADER_WATER_RIVER_FOAM}) {
        std::string source,error,msl;std::vector<unsigned char> spirv;PixelPipelineDescription pipeline={};
        CHECK(EngineShader_Pixel_Program(program,pipeline,source,COMBINER_SHADER_TARGET_SDL3_GPU));
        bool ok=SDL3_Compile_HLSL_To_SPIRV(source,false,spirv,error)&&SDL3_Translate_SPIRV_To_MSL(spirv,false,msl,error);
        CHECK(ok);if(ok)uniqueMetalTextures(msl);if(!ok)fprintf(stderr,"water variant %u: %s\n",unsigned(program),error.c_str());
    }
    printf("NATIVE_EFFECTS: seventeen production variants compiled SPIR-V/MSL failures=%u\n",failures);
}
int main(){
    variants();if(failures)return 1;
#if defined(__APPLE__)
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER,"dummy");SDL_SetHint("ZH_SDL_GPU_METAL_WINDOWLESS","1");
#else
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER,"offscreen");
#endif
    if(!SDL_Init(SDL_INIT_VIDEO)){printf("SKIP NATIVE EFFECTS GPU: %s\n",SDL_GetError());return 77;}
    auto *adapter=Direct3DCreate9(D3D_SDK_VERSION);D3DPRESENT_PARAMETERS p={};p.BackBufferWidth=64;p.BackBufferHeight=48;
    p.BackBufferFormat=D3DFMT_A8R8G8B8;p.Windowed=1;p.EnableAutoDepthStencil=1;p.AutoDepthStencilFormat=D3DFMT_D24S8;
    IDirect3DDevice9 *created=nullptr;CHECK(adapter->CreateDevice(0,D3DDEVTYPE_HAL,nullptr,0,&p,&created)==D3D_OK);
    auto *d=static_cast<PosixDevice9*>(created);
    if(!d||d->Create_Gpu_Frame(true)!=D3D_OK){printf("SKIP NATIVE EFFECTS GPU: %s\n",SDL_GetError());if(d)d->Release();adapter->Release();SDL_Quit();return failures?1:77;}
    IDirect3DTexture9 *texture=nullptr;CHECK(d->CreateTexture(1,1,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&texture,nullptr)==D3D_OK);
    D3DLOCKED_RECT lock={};CHECK(texture->LockRect(0,&lock,nullptr,0)==D3D_OK);*(uint32_t*)lock.pBits=0xffffffff;texture->UnlockRect(0);
    D3DMATRIX identity={};identity._11=identity._22=identity._33=identity._44=1;
    D3DMATRIX projection={};projection._11=projection._22=1;projection._33=100.0f/99;projection._34=1;projection._43=-100.0f/99;
    d->SetTransform(D3DTS_WORLD,&identity);d->SetTransform(D3DTS_VIEW,&identity);d->SetTransform(D3DTS_PROJECTION,&projection);
    d->Set_Lighting_Mode(true,false);d->SetFVF(D3DFVF_XYZ|D3DFVF_DIFFUSE|D3DFVF_TEX2);d->SetRenderState(D3DRS_LIGHTING,0);
    d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE);d->SetRenderState(D3DRS_ZWRITEENABLE,0);
    d->SetRenderState(D3DRS_ALPHABLENDENABLE,1);d->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_SRCALPHA);d->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_INVSRCALPHA);
    d->SetTexture(0,texture);d->SetTextureStageState(1,D3DTSS_COLOROP,D3DTOP_DISABLE);
    const float ground=100.0f/99-100.0f/(99*12.0f);
    auto draw=[&](unsigned soft,unsigned fire,unsigned effect,float age=0.5f){
        d->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,0xff000080,ground,0);
        d->Set_Particle_Flag(SceneEffects::SoftParticles,soft);d->Set_Particle_Flag(SceneEffects::FireParticles,fire);
        d->Take_Particle_Depth();float numbers[4]={1.25f,1,0.3f,0.5f};d->Set_Object_Effect(effect,numbers);
        Vertex v[3]={{-10,-10,10,0xffc08040,0,1,4,age},{-10,30,10,0xffc08040,0,-1,4,age},{30,-10,10,0xffc08040,2,1,4,age}};
        auto before=d->Draws_Recorded();CHECK(d->DrawPrimitiveUP(D3DPT_TRIANGLELIST,1,v,sizeof(Vertex))==D3D_OK);CHECK(d->Draws_Recorded()==before+1);
        std::vector<uint8_t> pixels;CHECK(d->Get_Gpu()->Read_Back(d->Get_Gpu()->Back_Buffer(),64,48,pixels));return pixels;
    };
    auto plain=draw(0,0,0),soft=draw(1,0,0),fire=draw(0,1,0),outage=draw(0,0,DRAW_EFFECT_OUTAGE),emp=draw(0,0,DRAW_EFFECT_EMP);
    CHECK(plain.size()==64*48*4&&soft.size()==plain.size()&&fire.size()==plain.size());
    const unsigned at=(24*64+32)*4;
    if(soft.size()>at+3){CHECK(soft[at+2]<plain[at+2]);CHECK(soft[at]>plain[at]);}
    CHECK(fire!=plain&&outage!=plain&&emp!=plain);
    auto revealed=draw(0,0,DRAW_EFFECT_REVEALED),cloak=draw(0,0,DRAW_EFFECT_CLOAK);CHECK(revealed!=plain&&cloak!=plain);
    d->Allow_Effects(false);CHECK(draw(1,1,DRAW_EFFECT_EMP)==plain);d->Allow_Effects(true);
    // Projectile tracers are additive RGB with authored zero alpha. Exercise the
    // real sorted-billboard FVF, including normal fields, in front of land and sky.
    d->Set_Object_Effect(0,nullptr);d->Set_Particle_Flag(SceneEffects::FireParticles,false);
    d->SetFVF(D3DFVF_XYZ|D3DFVF_NORMAL|D3DFVF_DIFFUSE|D3DFVF_TEX2);
    d->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_ONE);d->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_ONE);
    d->SetTextureStageState(0,D3DTSS_COLOROP,D3DTOP_MODULATE);d->SetTextureStageState(0,D3DTSS_COLORARG1,D3DTA_TEXTURE);d->SetTextureStageState(0,D3DTSS_COLORARG2,D3DTA_DIFFUSE);
    d->SetTextureStageState(0,D3DTSS_ALPHAOP,D3DTOP_MODULATE);d->SetTextureStageState(0,D3DTSS_ALPHAARG1,D3DTA_TEXTURE);d->SetTextureStageState(0,D3DTSS_ALPHAARG2,D3DTA_DIFFUSE);
    struct Billboard{float x,y,z,nx,ny,nz;uint32_t colour;float u,v,halfWidth,age;};
    const Billboard tracer[3]={{-10,-10,10,0,0,0,0x00ffe1a0,0,1,2.5f,0},
        {-10,30,10,0,0,0,0x00ffe1a0,0,-1,2.5f,0},{30,-10,10,0,0,0,0x00ffe1a0,2,1,2.5f,0}};
    for(bool fade:{false,true})for(bool sky:{false,true}) {
        d->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,0xff000000,sky?1:ground,0);
        d->Set_Particle_Flag(SceneEffects::SoftParticles,fade);d->Take_Particle_Depth();
        const auto before=d->Draws_Recorded();CHECK(d->DrawPrimitiveUP(D3DPT_TRIANGLELIST,1,tracer,sizeof(Billboard))==D3D_OK);CHECK(d->Draws_Recorded()==before+1);
        std::vector<uint8_t> pixels;CHECK(d->Get_Gpu()->Read_Back(d->Get_Gpu()->Back_Buffer(),64,48,pixels));
        if(pixels.size()>at+3) {CHECK(pixels[at+2]>100&&pixels[at+1]>80&&pixels[at]>50);
            printf("TRACER: soft=%d sky=%d rgb=%u,%u,%u alpha=%u\n",int(fade),int(sky),pixels[at+2],pixels[at+1],pixels[at],pixels[at+3]);}
    }
    // A glow attached to its opaque shell can be only millimetres in front
    // of the captured surface. Contact fading erases it; authored trail policy
    // must keep that glow out of the fade while retaining its ordinary depth test.
    const float shellDepth=100.0f/99-100.0f/(99*10.001f);
    for(bool fade:{true,false}) {
        d->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,0xff000000,shellDepth,0);
        d->Set_Particle_Flag(SceneEffects::SoftParticles,fade);d->Take_Particle_Depth();
        CHECK(d->DrawPrimitiveUP(D3DPT_TRIANGLELIST,1,tracer,sizeof(Billboard))==D3D_OK);
        std::vector<uint8_t> pixels;CHECK(d->Get_Gpu()->Read_Back(d->Get_Gpu()->Back_Buffer(),64,48,pixels));
        if(pixels.size()>at+3){if(fade)CHECK(pixels[at+2]<2);else CHECK(pixels[at+2]>200);}
    }
    d->Set_Object_Effect(0,nullptr);d->Set_Particle_Flag(SceneEffects::SoftParticles,false);d->Set_Particle_Flag(SceneEffects::FireParticles,false);
    texture->Release();CHECK(SDL_WaitForGPUIdle(d->Get_Gpu()->Device()));
    printf("NATIVE_EFFECTS: failures=%u backend=%s\n",failures,SDL_GetGPUDeviceDriver(d->Get_Gpu()->Device()));
    d->Release();adapter->Release();SDL_Quit();return failures?1:0;
}
