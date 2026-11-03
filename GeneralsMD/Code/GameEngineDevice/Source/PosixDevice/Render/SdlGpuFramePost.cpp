// Native floating-point world tone mapping, completed before HUD rendering.
#include "SdlGpuFrame.h"
#include "SdlAoShader.h"
#include "SdlPostShader.h"
#include "sdl3shadercompile.h"
#include "Platform/IOSLifecycle.h"
#include <SDL3/SDL.h>
#include <cstdio>
bool SdlGpuFrame::Supports_Hdr() const {
    return GpuDevice&&!PostFailed&&SDL_GPUTextureSupportsFormat(GpuDevice,SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT,
        SDL_GPU_TEXTURETYPE_2D,SDL_GPU_TEXTUREUSAGE_COLOR_TARGET|SDL_GPU_TEXTUREUSAGE_SAMPLER);
}
void SdlGpuFrame::Release_Post(){
    if(PostPipeline)SDL_ReleaseGPUGraphicsPipeline(GpuDevice,PostPipeline);
    if(PostVertex)SDL_ReleaseGPUShader(GpuDevice,PostVertex);
    if(PostPixel)SDL_ReleaseGPUShader(GpuDevice,PostPixel);
    if(PostSampler)SDL_ReleaseGPUSampler(GpuDevice,PostSampler);
    PostPipeline=nullptr;PostVertex=PostPixel=nullptr;PostSampler=nullptr;
}
bool SdlGpuFrame::Prepare_Post(){
    if(PostFailed)return false;if(PostPipeline)return true;
    const auto fail=[&](const char *why){PostFailed=true;fprintf(stderr,"[SDL_POST] disabled reason=%s\n",why);return false;};
    std::string error;std::vector<unsigned char> spirv;
    if(!PostVertex&&(!SDL3_Compile_HLSL_To_SPIRV(SdlAo::VertexShader(),true,spirv,error)
        ||!(PostVertex=SDL3_Create_Shader(GpuDevice,spirv,true,error))))return fail(error.c_str());
    if(!PostPixel&&(!SDL3_Compile_HLSL_To_SPIRV(SdlPost::Pixel(),false,spirv,error)
        ||!(PostPixel=SDL3_Create_Shader(GpuDevice,spirv,false,error))))return fail(error.c_str());
    SDL_GPUSamplerCreateInfo sampler={};sampler.address_mode_u=sampler.address_mode_v=sampler.address_mode_w=SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    if(!PostSampler)PostSampler=SDL_CreateGPUSampler(GpuDevice,&sampler);
    if(!PostSampler)return fail(SDL_GetError());
    SDL_GPUColorTargetDescription colour={};colour.format=(SDL_GPUTextureFormat)Scene_Format();
    colour.blend_state.enable_color_write_mask=true;
    colour.blend_state.color_write_mask=SDL_GPU_COLORCOMPONENT_R|SDL_GPU_COLORCOMPONENT_G|SDL_GPU_COLORCOMPONENT_B;
    SDL_GPUGraphicsPipelineCreateInfo pipeline={};pipeline.vertex_shader=PostVertex;pipeline.fragment_shader=PostPixel;
    pipeline.primitive_type=SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;pipeline.rasterizer_state.cull_mode=SDL_GPU_CULLMODE_NONE;
    pipeline.target_info.num_color_targets=1;pipeline.target_info.color_target_descriptions=&colour;
    PostPipeline=SDL_CreateGPUGraphicsPipeline(GpuDevice,&pipeline);if(!PostPipeline)return fail(SDL_GetError());
    fprintf(stderr,"[SDL_POST] ready hdr=%d\n",int(HdrActive));return true;
}
bool SdlGpuFrame::Post_Process(const SdlTarget &target,const float rect[4],float grade){
    if((!HdrActive&&(grade<=0||MemoryConstrained))||!rect||rect[2]<=0||rect[3]<=0)return true;
    if(!Prepare_Post())return false;
    std::string error;auto *source=Capture_Heat_Background(target,error);if(!source)return false;
    ZH_IOS_WaitForForeground();auto *commands=SDL_AcquireGPUCommandBuffer(GpuDevice);if(!commands)return false;
    SDL_GPUColorTargetInfo colour={};colour.texture=target.Colour;colour.load_op=SDL_GPU_LOADOP_LOAD;colour.store_op=SDL_GPU_STOREOP_STORE;
    auto *pass=SDL_BeginGPURenderPass(commands,&colour,1,nullptr);if(!pass){SDL_CancelGPUCommandBuffer(commands);return false;}
    SDL_BindGPUGraphicsPipeline(pass,PostPipeline);SDL_GPUTextureSamplerBinding binding={source,PostSampler};SDL_BindGPUFragmentSamplers(pass,0,&binding,1);
    const float constants[8]={rect[0],rect[1],rect[2],rect[3],HdrActive?1.0f:0.0f,grade,1.0f/target.Width,1.0f/target.Height};
    SDL_PushGPUFragmentUniformData(commands,0,constants,sizeof(constants));
    const SDL_GPUViewport viewport={0,0,float(target.Width),float(target.Height),0,1};SDL_SetGPUViewport(pass,&viewport);
    SDL_DrawGPUPrimitives(pass,3,1,0,0);SDL_EndGPURenderPass(pass);
    const bool ok=SerializeSubmits?Submit(commands):Submit_Flush(commands);
    if(ok){External_Colour_Write(target.Colour);Invalidate_Water_Snapshots(target.Colour,nullptr);}
    return ok;
}
