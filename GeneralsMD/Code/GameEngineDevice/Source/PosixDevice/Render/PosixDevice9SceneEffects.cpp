// Distributed under the repository's GPL-3.0 terms; see LICENSE.md.
#include "PosixDevice9.h"
#include "SdlGpuFrame.h"
#include "SdlAtmosphereShader.h"
#include "SdlFirelightShader.h"
#include "ffshader.h"
#include "SdlResourceMirror.h"
#include "SdlRenderMatrix.h"
#include <cmath>
#include <algorithm>
#include <cstdio>
#include <cstring>

void PosixDevice9::Set_Lighting_Mode(bool modern,bool hemisphere)
{
    hemisphere=modern&&hemisphere;
    if(ModernLighting!=modern||HemisphereAmbient!=hemisphere)
        fprintf(stderr,"[SDL_LIGHTING] modern=%d hemisphere=%d\n",int(modern),int(hemisphere));
    ModernLighting=modern;HemisphereAmbient=hemisphere;
    if(!SunActive&&!Sun_Scene_Excluded()) {
        std::memcpy(EffectSceneView,&Transforms[D3DTS_VIEW],sizeof(EffectSceneView));
        CloakSnapshot=CloakSource=nullptr;CloakFrame=~uint64_t(0);
        ParticleDepth=ParticleDepthSource=nullptr;
        ++SceneSerial;PostRect[0]=float(Viewport.X);PostRect[1]=float(Viewport.Y);PostRect[2]=float(Viewport.Width);PostRect[3]=float(Viewport.Height);
    }
}
bool PosixDevice9::Copy_Scene(IDirect3DBaseTexture9 *texture,int x,int y,int width,int height){
    if(!Gpu||!Gpu->Hdr_Active()||!texture||x<0||y<0||width<=0||height<=0)return false;
    SdlTarget target;std::string error;if(!Resolve_Target(target,error))return false;
    auto *source=Mirrors->Texture(texture,error);
    if(!source||source==target.Colour||!Gpu->Is_Hdr_Texture(source)||!Gpu->Is_Hdr_Texture(target.Colour))return false;
    if(unsigned(x)+unsigned(width)>target.Width||unsigned(y)+unsigned(height)>target.Height)return false;
    const int32_t rect[4]={x,y,width,height};Gpu->Record_Blit(source,rect,target.Colour,rect,false);return true;
}
void PosixDevice9::Set_Post_Grade(float amount){PostGrade=std::isfinite(amount)?std::clamp(amount,0.0f,1.0f):0;}
void PosixDevice9::Finish_Scene(){
    if(!Gpu||FinishedSceneSerial==SceneSerial||!ModernLighting||!EffectsAllowed||SunActive||Sun_Scene_Excluded())return;
    SdlTarget target;std::string error;if(!Resolve_Target(target,error))return;
    FinishedSceneSerial=SceneSerial;
    if(!Gpu->Post_Process(target,PostRect,PostGrade))fprintf(stderr,"[SDL_POST] scene finish failed\n");
}
bool PosixDevice9::Lights_Per_Pixel() const
{
    return Gpu&&EffectsAllowed&&ModernLighting&&!SunActive&&!Sun_Scene_Excluded()&&Gpu->Supports_Firelight();
}
void PosixDevice9::Set_Blast_Lights(const float *lights,unsigned count)
{
    BlastCount=0;if(!lights)return;
    for(unsigned i=0;i<std::min(count,32u);++i) {
        const float *p=lights+i*12;bool valid=p[3]>0&&p[7]>=0;
        for(unsigned j=0;j<12;++j)valid=valid&&std::isfinite(p[j]);
        for(unsigned j:{4u,5u,6u,8u,9u,10u})valid=valid&&p[j]>=0;
        if(valid)std::memcpy(BlastLights[BlastCount++],p,12*sizeof(float));
    }
}
void PosixDevice9::Set_Headlights(const float *lights,unsigned count,const float *gain)
{
    HeadlightCount=0;if(!lights||!gain)return;
    for(unsigned c=0;c<3;++c)if(!std::isfinite(gain[c])||gain[c]<0)return;
    std::memcpy(HeadlightGain,gain,3*sizeof(float));
    for(unsigned i=0;i<std::min(count,16u);++i) {
        const float *p=lights+i*8;bool valid=p[3]>0&&p[7]>=-1&&p[7]<1;
        for(unsigned j=0;j<8;++j)valid=valid&&std::isfinite(p[j]);
        if(valid)std::memcpy(Headlights[HeadlightCount++],p,8*sizeof(float));
    }
}
void PosixDevice9::Set_Particle_Flag(unsigned flag,bool enabled)
{
    if(enabled)ParticleFlags|=flag;else ParticleFlags&=~flag;
}
void PosixDevice9::Allow_Effects(bool enabled)
{
    EffectsAllowed=enabled;
    if(!enabled){ParticleFlags=RequestedEffect=0;ParticleDepth=CloakSnapshot=nullptr;}
}
bool PosixDevice9::Supports_Object_Effects() const
{
    return Gpu&&EffectsAllowed&&ModernLighting&&!SunActive&&!Sun_Scene_Excluded();
}
bool PosixDevice9::Takes_Object_Effect(unsigned effect)
{
    return Supports_Object_Effects()&&effect>DRAW_EFFECT_NONE&&effect<DRAW_EFFECT_CLOAK_SHADOW
        &&(effect!=DRAW_EFFECT_CLOAK||Gpu->Supports_Heat_Haze());
}
void PosixDevice9::Set_Object_Effect(unsigned effect,const float *parameters)
{
    RequestedEffect=effect<DRAW_EFFECT_COUNT?effect:DRAW_EFFECT_NONE;
    std::fill(EffectParameters,EffectParameters+4,0.0f);
    if(parameters)for(unsigned i=0;i<4;++i) {
        if(!std::isfinite(parameters[i])){RequestedEffect=0;break;}
        EffectParameters[i]=parameters[i];
    }
}
unsigned PosixDevice9::Requested_Object_Effect(float *parameters) const
{
    if(parameters)std::memcpy(parameters,EffectParameters,sizeof(EffectParameters));
    return RequestedEffect;
}
unsigned PosixDevice9::Object_Effect() const
{
    if(!Supports_Object_Effects()||!RequestedEffect||RequestedEffect>=DRAW_EFFECT_CLOAK_SHADOW
        ||VertexShader||PixelShader||(FVF&D3DFVF_POSITION_MASK)==D3DFVF_XYZRHW)return DRAW_EFFECT_NONE;
    if(RenderStates[D3DRS_ALPHABLENDENABLE]) {
        const auto source=RenderStates[D3DRS_SRCBLEND],destination=RenderStates[D3DRS_DESTBLEND];
        if(source==D3DBLEND_ZERO||source==D3DBLEND_DESTCOLOR||destination==D3DBLEND_SRCCOLOR)return DRAW_EFFECT_NONE;
        if((source==D3DBLEND_ONE||destination==D3DBLEND_ONE)&&RequestedEffect!=DRAW_EFFECT_OUTAGE)return DRAW_EFFECT_NONE;
    }
    if(RequestedEffect==DRAW_EFFECT_CLOAK&&(!CloakSnapshot||CloakFrame!=PresentCount))return DRAW_EFFECT_NONE;
    return RequestedEffect;
}
void PosixDevice9::Take_Particle_Depth()
{
    ParticleDepth=ParticleDepthSource=nullptr;
    if(!Supports_Object_Effects()||!Gpu->Supports_Scene_Depth())return;
    SdlTarget target;std::string error;if(!Resolve_Target(target,error)||!target.Depth)return;
    ParticleDepth=Gpu->Scene_Depth(target);
    if(ParticleDepth){ParticleDepthSource=target.Depth;ParticleWidth=target.Width;ParticleHeight=target.Height;}
}
bool PosixDevice9::Supports_Atmosphere() const {return Gpu&&Gpu->Supports_Atmosphere();}
bool PosixDevice9::Supports_Firelight() const {return Gpu&&Gpu->Supports_Firelight();}
bool PosixDevice9::Firelight(const SceneEffects::PointLight *lights,unsigned count)
{
    if(!Gpu||SunActive||Sun_Scene_Excluded()||!ModernLighting
        ||count>SceneEffects::MaxFirelights||(count&&!lights)||!Viewport.Width||!Viewport.Height)return false;
    const bool full=Lights_Per_Pixel();
    if(!count&&(!full||(!BlastCount&&!HeadlightCount)))return false;
    for(unsigned i=0;i<count;++i) {
        float values[12];std::memcpy(values,lights+i,sizeof(values));
        for(float value:values)if(!std::isfinite(value))return false;
        if(lights[i].Position[3]<=0||lights[i].Diffuse[3]<0)return false;
        for(unsigned c=0;c<3;++c)if(lights[i].Diffuse[c]<0||lights[i].Ambient[c]<0)return false;
    }
    SdlTarget target;std::string error;if(!Resolve_Target(target,error))return false;
    SdlFirelight::Constants constants={};float clip[16];
    multiply(&Transforms[D3DTS_VIEW]._11,&Transforms[D3DTS_PROJECTION]._11,clip);
    if(!render_inverse(clip,constants.WorldFromClip))return false;
    for(float value:constants.WorldFromClip)if(!std::isfinite(value))return false;
    const float range=Viewport.MaxZ-Viewport.MinZ;if(!std::isfinite(range)||range<=1e-8f)return false;
    constants.Viewport[0]=float(Viewport.X);constants.Viewport[1]=float(Viewport.Y);
    constants.Viewport[2]=float(Viewport.Width);constants.Viewport[3]=float(Viewport.Height);
    constants.DepthInfo[2]=Viewport.MinZ;constants.DepthInfo[3]=1/range;
    if(full) {
        constants.Count[0]=float(BlastCount);constants.Count[1]=float(HeadlightCount);
        std::memcpy(constants.Lights,BlastLights,BlastCount*sizeof(BlastLights[0]));
        std::memcpy(constants.HeadlightGain,HeadlightGain,sizeof(HeadlightGain));
        for(unsigned i=0;i<HeadlightCount;++i) {
            std::memcpy(constants.HeadlightPosition[i],Headlights[i],4*sizeof(float));
            std::memcpy(constants.HeadlightDirection[i],Headlights[i]+4,4*sizeof(float));
        }
    } else {constants.Count[0]=float(count);if(count)std::memcpy(constants.Lights,lights,count*sizeof(*lights));}
    return Gpu->Firelight(target,constants);
}
bool PosixDevice9::Supports_Heat_Haze() const {return Gpu&&Gpu->Supports_Heat_Haze();}
bool PosixDevice9::Begin_Heat_Haze(IDirect3DBaseTexture9 *source)
{
    if(!Gpu||!source||HeatSnapshot||SunActive||Sun_Scene_Excluded())return false;
    SdlTarget target;std::string error;
    if(!Resolve_Target(target,error)||!target.Colour)return false;
    auto *texture=Mirrors->Texture(source,error);if(!texture||texture!=target.Colour)return false;
    auto *snapshot=Gpu->Capture_Heat_Background(target,error);
    if(!snapshot||snapshot==target.Colour)return false;
    HeatSourceTexture=texture;HeatSnapshot=snapshot;return true;
}
void PosixDevice9::End_Heat_Haze() {HeatSourceTexture=HeatSnapshot=nullptr;}
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
namespace {bool requestedHdr=false;unsigned requestedSamples=0;}
void RequestDisplay(bool hdr,unsigned samples){requestedHdr=hdr;requestedSamples=samples;}
bool RequestedHdr(){return requestedHdr;}
unsigned RequestedSamples(){return requestedSamples;}
unsigned MaxSamples(IDirect3DDevice9*d){auto *gpu=d?static_cast<PosixDevice9*>(d)->Get_Gpu():nullptr;return gpu?gpu->Max_Samples():1;}
unsigned ActiveSamples(IDirect3DDevice9*d){auto *gpu=d?static_cast<PosixDevice9*>(d)->Get_Gpu():nullptr;return gpu&&gpu->Sample_Count()>1?gpu->Sample_Count():0;}
bool SupportsHdr(IDirect3DDevice9*d){auto *gpu=d?static_cast<PosixDevice9*>(d)->Get_Gpu():nullptr;return gpu&&gpu->Supports_Hdr();}
void SetPostGrade(IDirect3DDevice9*d,float amount){if(d)static_cast<PosixDevice9*>(d)->Set_Post_Grade(amount);}
bool CopyScene(IDirect3DDevice9*d,IDirect3DBaseTexture9*t,int x,int y,int w,int h){return d&&static_cast<PosixDevice9*>(d)->Copy_Scene(t,x,y,w,h);}
void FinishScene(IDirect3DDevice9*d){if(d)static_cast<PosixDevice9*>(d)->Finish_Scene();}

bool LightsPerPixel(IDirect3DDevice9*d){return d&&static_cast<PosixDevice9*>(d)->Lights_Per_Pixel();}
void SetBlastLights(IDirect3DDevice9*d,const float*p,unsigned n){if(d)static_cast<PosixDevice9*>(d)->Set_Blast_Lights(p,n);}
void SetHeadlights(IDirect3DDevice9*d,const float*p,unsigned n,const float*g){if(d)static_cast<PosixDevice9*>(d)->Set_Headlights(p,n,g);}

void SetParticleFlag(IDirect3DDevice9*d,unsigned f,bool v){if(d)static_cast<PosixDevice9*>(d)->Set_Particle_Flag(f,v);}
void AllowEffects(IDirect3DDevice9*d,bool v){if(d)static_cast<PosixDevice9*>(d)->Allow_Effects(v);}
void TakeParticleDepth(IDirect3DDevice9*d){if(d)static_cast<PosixDevice9*>(d)->Take_Particle_Depth();}
bool SupportsObjectEffects(IDirect3DDevice9*d){return d&&static_cast<PosixDevice9*>(d)->Supports_Object_Effects();}
bool TakesObjectEffect(IDirect3DDevice9*d,unsigned e){return d&&static_cast<PosixDevice9*>(d)->Takes_Object_Effect(e);}
void SetObjectEffect(IDirect3DDevice9*d,unsigned e,const float*p){if(d)static_cast<PosixDevice9*>(d)->Set_Object_Effect(e,p);}
unsigned RequestedObjectEffect(IDirect3DDevice9*d,float*p){if(d)return static_cast<PosixDevice9*>(d)->Requested_Object_Effect(p);if(p)std::fill(p,p+4,0.0f);return 0;}

bool SupportsFirelight(IDirect3DDevice9 *device){return device&&static_cast<PosixDevice9*>(device)->Supports_Firelight();}
bool Firelight(IDirect3DDevice9 *device,const PointLight *lights,unsigned count){return device&&static_cast<PosixDevice9*>(device)->Firelight(lights,count);}
bool SupportsHeatHaze(IDirect3DDevice9 *device){return device&&static_cast<PosixDevice9*>(device)->Supports_Heat_Haze();}
bool BeginHeatHaze(IDirect3DDevice9 *device,IDirect3DBaseTexture9 *source){return device&&static_cast<PosixDevice9*>(device)->Begin_Heat_Haze(source);}
void EndHeatHaze(IDirect3DDevice9 *device){if(device)static_cast<PosixDevice9*>(device)->End_Heat_Haze();}
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
