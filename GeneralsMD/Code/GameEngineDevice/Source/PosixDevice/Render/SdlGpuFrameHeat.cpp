// GPL-3.0; see LICENSE.md. Native scene-copy heat distortion for smudges and heat splats.
#include "SdlGpuFrame.h"
#include "sdl3shadercompile.h"
#include "Platform/IOSLifecycle.h"
#include <SDL3/SDL.h>
#if defined(__APPLE__)
extern "C" void ZH_SDL_Metal_SetNextRenderPassTag(Uint32 category);
#endif
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {
constexpr unsigned HeatFloatsPerSplat=16;
constexpr unsigned HeatSplatLimit=2048;
const char *HeatSplatVertex(){return R"(
StructuredBuffer<float4> Splats:register(t0,space0);
struct Out{float4 Position:SV_Position;float2 Corner:TEXCOORD0;float2 Screen:TEXCOORD1;float4 Heat:TEXCOORD2;float4 Planes:TEXCOORD3;float4 Across:TEXCOORD4;float2 Offset:TEXCOORD5;};
static const float2 Corners[6]={float2(-1,-1),float2(-1,1),float2(1,-1),float2(1,-1),float2(-1,1),float2(1,1)};
Out main(uint index:SV_VertexID){
    uint splat=index/6;float2 corner=Corners[index%6];float4 shape=Splats[splat*4];
    Out o;float2 clip=shape.xy+corner*shape.zw;o.Position=float4(clip,0,1);o.Corner=corner;
    o.Screen=clip*float2(0.5,-0.5)+0.5;o.Heat=Splats[splat*4+1];o.Planes=Splats[splat*4+2];
    o.Across=Splats[splat*4+3];o.Offset=corner*shape.zw;return o;
})";}
const char *HeatSplatPixel(){return R"(
Texture2D<float> SceneDepth:register(t0,space2);
SamplerState Point:register(s0,space2);
struct Out{float4 Position:SV_Position;float2 Corner:TEXCOORD0;float2 Screen:TEXCOORD1;float4 Heat:TEXCOORD2;float4 Planes:TEXCOORD3;float4 Across:TEXCOORD4;float2 Offset:TEXCOORD5;};
float linear_depth(float depth,float4 planes){return planes.x*planes.y/max(planes.y-depth*(planes.y-planes.x),0.0001);}
float4 main(Out input):SV_Target{
    float reach=dot(input.Corner,input.Corner);if(reach>=1)discard;
    float weight=(1-reach)*(1-reach)*(1-reach);uint width,height;SceneDepth.GetDimensions(width,height);
    float2 uv=min((floor(input.Position.xy*2)+0.5)/float2(width,height),float2(1,1));
    float scene=linear_depth(SceneDepth.SampleLevel(Point,uv,0),input.Planes);
    float flame=linear_depth(input.Heat.x,input.Planes);
    weight*=saturate((scene-flame-input.Planes.z)/max(input.Planes.w,0.0001));
    float aspect=(float)width/(float)height;
    if(dot(input.Across.xy,input.Across.xy)>0){
        float2 along=normalize(input.Across.xy*float2(aspect,1));
        float2 offset=input.Offset*float2(aspect,1)*0.5;
        float off=abs(offset.x*along.y-offset.y*along.x);
        float still=max(input.Across.z,2.0/(float)height);
        weight*=saturate((off-still)/max(input.Across.w,0.0001));
    }
    if(weight<=0)discard;
    float2 p=float2(input.Screen.x*aspect,input.Screen.y)*50.0;float t=input.Heat.z*2.0;
    float rise=p.y+t*3.0+0.8*sin(p.x*0.35+t*0.7);
    float2 shift;shift.x=sin(rise)+0.3*sin(rise*1.9+p.x*0.5-t*1.3);
    shift.y=0.25*sin(rise*0.6+p.x*0.4-t*0.9);
    return float4(shift*input.Heat.y*weight,weight,0);
})";}
const char *HeatCompositeVertex(){return R"(
struct Out{float4 Position:SV_Position;float2 Uv:TEXCOORD0;};
Out main(uint id:SV_VertexID){Out o;float2 p=float2(id==1?3.0:-1.0,id==2?3.0:-1.0);o.Position=float4(p,0,1);o.Uv=float2(p.x*0.5+0.5,0.5-p.y*0.5);return o;}
)";}
const char *HeatCompositePixel(){return R"(
Texture2D<float4> Source:register(t0,space2);SamplerState Linear0:register(s0,space2);
Texture2D<float4> Heat:register(t1,space2);SamplerState Linear1:register(s1,space2);
struct Out{float4 Position:SV_Position;float2 Uv:TEXCOORD0;};
float4 main(Out input):SV_Target{
    float4 heat=Heat.Sample(Linear1,input.Uv);float2 shift=heat.xy/max(heat.z,1.0);
    shift*=saturate(Heat.Sample(Linear1,input.Uv+shift).z*4.0);
    float2 border=min(input.Uv,1.0-input.Uv);float2 settle=saturate(border/0.02);shift*=settle.x*settle.y;
    return Source.Sample(Linear0,input.Uv+shift);
})";}
}

struct SdlGpuFrame::HeatResources {
    SDL_GPUBuffer *Splats=nullptr;
    SDL_GPUTransferBuffer *Transfer=nullptr;
    SDL_GPUTexture *Field=nullptr;
    SDL_GPUShader *SplatVertex=nullptr,*SplatPixel=nullptr,*CompositeVertex=nullptr,*CompositePixel=nullptr;
    SDL_GPUGraphicsPipeline *SplatPipeline=nullptr,*CompositePipeline=nullptr;
    SDL_GPUSampler *Point=nullptr,*Linear=nullptr;
    unsigned Width=0,Height=0;
};

bool SdlGpuFrame::Supports_Heat_Haze() const
{
    return GpuDevice&&!HeatFailed&&!HeatSplatFailed&&DepthSampleable
        && SDL_GPUTextureSupportsFormat(GpuDevice,SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT,
            SDL_GPU_TEXTURETYPE_2D,SDL_GPU_TEXTUREUSAGE_COLOR_TARGET|SDL_GPU_TEXTUREUSAGE_SAMPLER);
}

void SdlGpuFrame::Set_Heat_Splats(const float *splats,unsigned count)
{
    HeatSplats.clear();
    if(!splats||!count)return;
    count=std::min(count,HeatSplatLimit);
    const size_t floats=size_t(count)*HeatFloatsPerSplat;
    for(size_t i=0;i<floats;++i)if(!std::isfinite(splats[i]))return;
    HeatSplats.assign(splats,splats+floats);
}

void SdlGpuFrame::Release_Heat_Background()
{
    if(HeatBackground)Release_Texture(HeatBackground);
    HeatBackground=nullptr;HeatWidth=HeatHeight=0;
}

void SdlGpuFrame::Release_Heat()
{
    Release_Heat_Background();
    if(!Heat)return;
    if(Heat->SplatPipeline)SDL_ReleaseGPUGraphicsPipeline(GpuDevice,Heat->SplatPipeline);
    if(Heat->CompositePipeline)SDL_ReleaseGPUGraphicsPipeline(GpuDevice,Heat->CompositePipeline);
    for(auto *s:{Heat->SplatVertex,Heat->SplatPixel,Heat->CompositeVertex,Heat->CompositePixel})if(s)SDL_ReleaseGPUShader(GpuDevice,s);
    if(Heat->Point)SDL_ReleaseGPUSampler(GpuDevice,Heat->Point);
    if(Heat->Linear)SDL_ReleaseGPUSampler(GpuDevice,Heat->Linear);
    if(Heat->Field)Release_Texture(Heat->Field);
    if(Heat->Splats)SDL_ReleaseGPUBuffer(GpuDevice,Heat->Splats);
    if(Heat->Transfer)SDL_ReleaseGPUTransferBuffer(GpuDevice,Heat->Transfer);
    delete Heat;Heat=nullptr;
}

bool SdlGpuFrame::Prepare_Heat(unsigned width,unsigned height)
{
    if(!Supports_Heat_Haze()||!width||!height)return false;
    const auto fail=[&](const char *why){HeatSplatFailed=true;fprintf(stderr,"[SDL_HEAT] splats disabled reason=%s error=%s\n",why,SDL_GetError());Release_Heat();return false;};
    if(!Heat)Heat=new HeatResources;
    auto &h=*Heat;
    if(!h.SplatVertex){
        std::string error;std::vector<unsigned char> spirv;
        auto shader=[&](const char *source,bool vertex,SDL_GPUShader *&out,const char *name){
            if(!SDL3_Compile_HLSL_To_SPIRV(source,vertex,spirv,error)||!(out=SDL3_Create_Shader(GpuDevice,spirv,vertex,error))){
                SDL_SetError("%s",error.c_str());return fail(name);
            }return true;
        };
        if(!shader(HeatSplatVertex(),true,h.SplatVertex,"splat vertex")
            ||!shader(HeatSplatPixel(),false,h.SplatPixel,"splat pixel")
            ||!shader(HeatCompositeVertex(),true,h.CompositeVertex,"composite vertex")
            ||!shader(HeatCompositePixel(),false,h.CompositePixel,"composite pixel"))return false;
        SDL_GPUBufferCreateInfo bi={};bi.size=HeatSplatLimit*HeatFloatsPerSplat*sizeof(float);bi.usage=SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ;
        h.Splats=SDL_CreateGPUBuffer(GpuDevice,&bi);
        SDL_GPUTransferBufferCreateInfo ti={};ti.size=bi.size;ti.usage=SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;h.Transfer=SDL_CreateGPUTransferBuffer(GpuDevice,&ti);
        SDL_GPUSamplerCreateInfo si={};si.address_mode_u=si.address_mode_v=si.address_mode_w=SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        h.Point=SDL_CreateGPUSampler(GpuDevice,&si);si.min_filter=si.mag_filter=SDL_GPU_FILTER_LINEAR;h.Linear=SDL_CreateGPUSampler(GpuDevice,&si);
        if(!h.Splats||!h.Transfer||!h.Point||!h.Linear)return fail("buffers/samplers");
        SDL_GPUColorTargetDescription field={};field.format=SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT;
        field.blend_state.enable_blend=true;field.blend_state.src_color_blendfactor=SDL_GPU_BLENDFACTOR_ONE;field.blend_state.dst_color_blendfactor=SDL_GPU_BLENDFACTOR_ONE;
        field.blend_state.color_blend_op=SDL_GPU_BLENDOP_ADD;field.blend_state.src_alpha_blendfactor=SDL_GPU_BLENDFACTOR_ONE;field.blend_state.dst_alpha_blendfactor=SDL_GPU_BLENDFACTOR_ONE;
        field.blend_state.alpha_blend_op=SDL_GPU_BLENDOP_ADD;field.blend_state.enable_color_write_mask=true;field.blend_state.color_write_mask=SDL_GPU_COLORCOMPONENT_R|SDL_GPU_COLORCOMPONENT_G|SDL_GPU_COLORCOMPONENT_B|SDL_GPU_COLORCOMPONENT_A;
        SDL_GPUGraphicsPipelineCreateInfo pi={};pi.vertex_shader=h.SplatVertex;pi.fragment_shader=h.SplatPixel;pi.primitive_type=SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
        pi.rasterizer_state.cull_mode=SDL_GPU_CULLMODE_NONE;pi.target_info.num_color_targets=1;pi.target_info.color_target_descriptions=&field;
        h.SplatPipeline=SDL_CreateGPUGraphicsPipeline(GpuDevice,&pi);if(!h.SplatPipeline)return fail("splat pipeline");
        SDL_GPUColorTargetDescription scene={};scene.format=(SDL_GPUTextureFormat)Scene_Format();scene.blend_state.enable_color_write_mask=true;
        scene.blend_state.color_write_mask=SDL_GPU_COLORCOMPONENT_R|SDL_GPU_COLORCOMPONENT_G|SDL_GPU_COLORCOMPONENT_B|SDL_GPU_COLORCOMPONENT_A;
        pi={};pi.vertex_shader=h.CompositeVertex;pi.fragment_shader=h.CompositePixel;pi.primitive_type=SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
        pi.rasterizer_state.cull_mode=SDL_GPU_CULLMODE_NONE;pi.target_info.num_color_targets=1;pi.target_info.color_target_descriptions=&scene;
        h.CompositePipeline=SDL_CreateGPUGraphicsPipeline(GpuDevice,&pi);if(!h.CompositePipeline)return fail("composite pipeline");
    }
    const unsigned halfW=(width+1)/2,halfH=(height+1)/2;
    if(h.Field&&(h.Width!=halfW||h.Height!=halfH)){Release_Texture(h.Field);h.Field=nullptr;}
    if(!h.Field){
        SDL_GPUTextureCreateInfo ti={};ti.type=SDL_GPU_TEXTURETYPE_2D;ti.format=SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT;
        ti.usage=SDL_GPU_TEXTUREUSAGE_COLOR_TARGET|SDL_GPU_TEXTUREUSAGE_SAMPLER;ti.width=halfW;ti.height=halfH;ti.layer_count_or_depth=ti.num_levels=1;
        h.Field=SDL_CreateGPUTexture(GpuDevice,&ti);if(!h.Field)return fail("field texture");h.Width=halfW;h.Height=halfH;
        fprintf(stderr,"[SDL_HEAT] splat renderer ready field=%ux%u\n",halfW,halfH);
    }
    return true;
}

bool SdlGpuFrame::Ensure_Heat_Background(const SdlTarget &target,std::string &error)
{
    error.clear();
    // HeatFailed gates the heat-haze effect, not this allocation: Post_Process needs the
    // scene copy for tone mapping and retries it every frame after a transient failure.
    if(!GpuDevice||!target.Colour||!target.Width||!target.Height)return false;
    if(HeatBackground==target.Colour){error="capture source aliases destination";return false;}
    if(HeatWidth!=target.Width||HeatHeight!=target.Height) {
        Release_Heat_Background();SDL_GPUTextureCreateInfo info={};info.type=SDL_GPU_TEXTURETYPE_2D;
        info.format=(SDL_GPUTextureFormat)Scene_Format();info.usage=SDL_GPU_TEXTUREUSAGE_SAMPLER|SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
        info.width=target.Width;info.height=target.Height;info.layer_count_or_depth=info.num_levels=1;
        HeatBackground=SDL_CreateGPUTexture(GpuDevice,&info);Track_Colour(HeatBackground);
        if(!HeatBackground){error=SDL_GetError();return false;}
        HeatWidth=target.Width;HeatHeight=target.Height;
        fprintf(stderr,"[SDL_HEAT] ready size=%ux%u\n",HeatWidth,HeatHeight);
    }
    return true;
}

bool SdlGpuFrame::Record_Heat_Background(SDL_GPUCommandBuffer *commands,const SdlTarget &target,std::string &error)
{
    if(!Ensure_Heat_Background(target,error))return false;
    auto *copy=SDL_BeginGPUCopyPass(commands);
    if(!copy){error=SDL_GetError();return false;}
    SDL_GPUTextureLocation source={},destination={};source.texture=target.Colour;destination.texture=HeatBackground;
    SDL_CopyGPUTextureToTexture(copy,&source,&destination,HeatWidth,HeatHeight,1,false);SDL_EndGPUCopyPass(copy);
    return true;
}

SDL_GPUTexture *SdlGpuFrame::Capture_Heat_Background(const SdlTarget &target,std::string &error)
{
    if(!GpuDevice||HeatFailed||!target.Colour||!target.Width||!target.Height)return nullptr;
    const auto fail=[&](const char *reason)->SDL_GPUTexture* {
        // HeatFailed is checked above, so this is the first failure of the episode: the heat
        // effect stays disabled and later captures return early without logging again.
        error=reason;HeatFailed=true;
        fprintf(stderr,"[SDL_HEAT] disabled reason=%s\n",reason);
        return nullptr;
    };
    if(HeatBackground==target.Colour)return fail("capture source aliases destination");
    if(!Flush(FlushHeat))return fail("scene flush failed");
    if(!Ensure_Heat_Background(target,error))return fail(error.c_str());
    ZH_IOS_WaitForForeground();auto *commands=SDL_AcquireGPUCommandBuffer(GpuDevice);
    if(!commands)return fail(SDL_GetError());
    if(!Record_Heat_Background(commands,target,error)){SDL_CancelGPUCommandBuffer(commands);return fail(error.c_str());}
    if(!(SerializeSubmits?Submit(commands):Submit_Flush(commands)))return fail("capture submission failed");
    return HeatBackground;
}

bool SdlGpuFrame::Heat_Haze_Scene(const SdlTarget &target)
{
    std::vector<float> splats;splats.swap(HeatSplats);
    if(splats.empty())return true;
    if(!target.Colour||!target.Depth||!target.Width||!target.Height||!Prepare_Heat(target.Width,target.Height))return false;
    SDL_GPUTexture *depth=Scene_Depth(target);if(!depth)return false;
    std::string error;SDL_GPUTexture *source=Capture_Heat_Background(target,error);if(!source)return false;
    auto &h=*Heat;const unsigned count=unsigned(splats.size()/HeatFloatsPerSplat),bytes=unsigned(splats.size()*sizeof(float));
    void *mapped=SDL_MapGPUTransferBuffer(GpuDevice,h.Transfer,true);if(!mapped)return false;
    std::memcpy(mapped,splats.data(),bytes);SDL_UnmapGPUTransferBuffer(GpuDevice,h.Transfer);
    ZH_IOS_WaitForForeground();auto *commands=SDL_AcquireGPUCommandBuffer(GpuDevice);if(!commands)return false;
    auto *copy=SDL_BeginGPUCopyPass(commands);if(!copy){SDL_CancelGPUCommandBuffer(commands);return false;}
    SDL_GPUTransferBufferLocation from={};from.transfer_buffer=h.Transfer;SDL_GPUBufferRegion to={};to.buffer=h.Splats;to.size=bytes;
    SDL_UploadToGPUBuffer(copy,&from,&to,true);SDL_EndGPUCopyPass(copy);
    SDL_GPUColorTargetInfo field={};field.texture=h.Field;field.load_op=SDL_GPU_LOADOP_CLEAR;field.store_op=SDL_GPU_STOREOP_STORE;
    #if defined(__APPLE__)
ZH_SDL_Metal_SetNextRenderPassTag(5u);
#endif
auto *pass=SDL_BeginGPURenderPass(commands,&field,1,nullptr);if(!pass){SDL_CancelGPUCommandBuffer(commands);return false;}
    SDL_BindGPUGraphicsPipeline(pass,h.SplatPipeline);SDL_BindGPUVertexStorageBuffers(pass,0,&h.Splats,1);
    SDL_GPUTextureSamplerBinding depthBinding={depth,h.Point};SDL_BindGPUFragmentSamplers(pass,0,&depthBinding,1);
    const SDL_GPUViewport half={0,0,float(h.Width),float(h.Height),0,1};SDL_SetGPUViewport(pass,&half);
    SDL_DrawGPUPrimitives(pass,count*6,1,0,0);SDL_EndGPURenderPass(pass);
    SDL_GPUColorTargetInfo scene={};scene.texture=target.Colour;scene.load_op=SDL_GPU_LOADOP_LOAD;scene.store_op=SDL_GPU_STOREOP_STORE;
    #if defined(__APPLE__)
ZH_SDL_Metal_SetNextRenderPassTag(5u);
#endif
pass=SDL_BeginGPURenderPass(commands,&scene,1,nullptr);if(!pass){SDL_CancelGPUCommandBuffer(commands);return false;}
    SDL_BindGPUGraphicsPipeline(pass,h.CompositePipeline);
    SDL_GPUTextureSamplerBinding textures[]={{source,h.Linear},{h.Field,h.Linear}};SDL_BindGPUFragmentSamplers(pass,0,textures,2);
    const SDL_GPUViewport full={0,0,float(target.Width),float(target.Height),0,1};SDL_SetGPUViewport(pass,&full);
    SDL_DrawGPUPrimitives(pass,3,1,0,0);SDL_EndGPURenderPass(pass);
    const bool ok=SerializeSubmits?Submit(commands):Submit_Flush(commands);
    if(ok){External_Colour_Write(target.Colour);Invalidate_Water_Snapshots(target.Colour,nullptr);}
    return ok;
}
