// Native floating-point world tone mapping, completed before HUD rendering.
#include "SdlGpuFrame.h"
#include "SdlAoShader.h"
#include "SdlPostShader.h"
#include "sdl3shadercompile.h"
#include "Platform/IOSLifecycle.h"
#include <SDL3/SDL.h>
#if defined(__APPLE__)
extern "C" void ZH_SDL_Metal_SetNextRenderPassTag(Uint32 category);
#endif
#include <cstdio>
#include <cmath>
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
    sampler.min_filter=sampler.mag_filter=SDL_GPU_FILTER_LINEAR;
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
bool SdlGpuFrame::Post_Process(const SdlTarget &target,const float rect[4],float grade,const ScenePost::Chain *chain){
    if(!rect)return false;
    for(unsigned i=0;i<4;++i)if(!std::isfinite(rect[i]))return false;
    if(rect[0]<0||rect[1]<0||rect[2]<=0||rect[3]<=0||rect[0]+rect[2]>target.Width||rect[1]+rect[3]>target.Height)return false;
    if(MemoryConstrained)grade=0;
    unsigned active=0,passes=0,modes[4]={},status=unsigned(HdrActive)|(unsigned(MemoryConstrained)<<1)|(SampleCount<<2);
    if(chain)for(unsigned i=0;i<chain->Count;++i){
        const auto effect=chain->Effects[i];status=status*31+unsigned(effect)+1;
        if(!ScenePost::Active(effect,SampleCount,MemoryConstrained))continue;
        ++active;
        // Filter modes: 1 FXAA, 2 sharpen, 3 FXAA directly followed by sharpen (one pass, one frame copy).
        if(effect==ScenePost::Sharpen&&passes&&modes[passes-1]==1)modes[passes-1]=3;
        else modes[passes++]=effect==ScenePost::FXAA?1:2;
    }
    // HDR tone mapping has to precede filtering, so it keeps its own pass. Without HDR the colour grade is folded
    // into the first filter pass, and only gets a pass of its own when no filter is active.
    const bool foldGrade=!HdrActive&&grade>0&&passes>0;
    if(status!=PostStatus){PostStatus=status;fprintf(stderr,"[SDL_POST] selected_filters=%u passes=%u samples=%u hdr=%d constrained=%d grade_folded=%d\n",active,passes+((HdrActive||grade>0)&&!foldGrade?1u:0u),SampleCount,int(HdrActive),int(MemoryConstrained),int(foldGrade));}
    if((HdrActive||grade>0)&&!foldGrade)if(!Run_Post_Pass(target,rect,grade,0,0))return false;
    for(unsigned i=0;i<passes;++i)
        if(!Run_Post_Pass(target,rect,i==0&&foldGrade?grade:0,modes[i],SampleCount>1?-0.25f:0.35f))return false;
    return true;
}
bool SdlGpuFrame::Run_Post_Pass(const SdlTarget &target,const float rect[4],float grade,unsigned filter,float amount){
    if(!Prepare_Post())return false;
    std::string error;auto *source=Capture_Heat_Background(target,error);if(!source)return false;
    ZH_IOS_WaitForForeground();auto *commands=SDL_AcquireGPUCommandBuffer(GpuDevice);if(!commands)return false;
    SDL_GPUColorTargetInfo colour={};colour.texture=target.Colour;const bool covers=rect[0]==0&&rect[1]==0&&rect[2]==float(target.Width)&&rect[3]==float(target.Height);
    colour.load_op=covers?SDL_GPU_LOADOP_DONT_CARE:SDL_GPU_LOADOP_LOAD;colour.store_op=SDL_GPU_STOREOP_STORE; // every pixel is rewritten when the rect covers the target
    #if defined(__APPLE__)
ZH_SDL_Metal_SetNextRenderPassTag(8u);
#endif
auto *pass=SDL_BeginGPURenderPass(commands,&colour,1,nullptr);if(!pass){SDL_CancelGPUCommandBuffer(commands);return false;}
    SDL_BindGPUGraphicsPipeline(pass,PostPipeline);SDL_GPUTextureSamplerBinding binding={source,PostSampler};SDL_BindGPUFragmentSamplers(pass,0,&binding,1);
    const float constants[12]={rect[0],rect[1],rect[2],rect[3],HdrActive?1.0f:0.0f,grade,1.0f/target.Width,1.0f/target.Height,float(filter),amount,0,0};
    SDL_PushGPUFragmentUniformData(commands,0,constants,sizeof(constants));
    const SDL_GPUViewport viewport={0,0,float(target.Width),float(target.Height),0,1};SDL_SetGPUViewport(pass,&viewport);
    SDL_DrawGPUPrimitives(pass,3,1,0,0);SDL_EndGPURenderPass(pass);
    const bool ok=SerializeSubmits?Submit(commands):Submit_Flush(commands);
    if(ok){External_Colour_Write(target.Colour);Invalidate_Water_Snapshots(target.Colour,nullptr);}
    return ok;
}
