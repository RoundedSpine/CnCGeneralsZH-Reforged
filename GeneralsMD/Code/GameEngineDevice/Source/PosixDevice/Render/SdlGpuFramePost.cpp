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
#include <algorithm>
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
    if(rect[0]<0||rect[1]<0||rect[2]<=0||rect[3]<=0||rect[0]>=float(target.Width)||rect[1]>=float(target.Height))return false;
    // A rounded scale can overshoot the target by a fraction of a texel; clamp instead of
    // reject, so tone mapping still covers the whole visible scene.
    const float area[4]={rect[0],rect[1],
        std::min(rect[2],float(target.Width)-rect[0]),
        std::min(rect[3],float(target.Height)-rect[1])};
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
    const unsigned totalPasses=passes+((HdrActive||grade>0)&&!foldGrade?1u:0u);
    if(!totalPasses)return true;
    if(!Prepare_Post())return false;
    const auto failHeat=[&](const char *reason) {
        HeatFailed=true;fprintf(stderr,"[SDL_HEAT] disabled reason=%s\n",reason);return false;
    };
    // Tone mapping must not depend on the heat-haze latch: HeatFailed disables the heat
    // effect, but the scene copy below is retried every frame so tone mapping recovers.
    if(!GpuDevice||!target.Colour||!target.Width||!target.Height)return false;
    if(HeatBackground==target.Colour)return failHeat("capture source aliases destination");
    const bool resized=HeatBackground&&(HeatWidth!=target.Width||HeatHeight!=target.Height);
    if(resized&&!Flush(FlushHeat))return failHeat("scene flush failed");
    std::string error;
    if(!Ensure_Heat_Background(target,error)) {
        const std::string reason=error.empty()?SDL_GetError():error;
        if(!resized&&!Flush(FlushHeat))
            fprintf(stderr,"[SDL_HEAT] scene flush failed after capture allocation failure\n");
        return failHeat(reason.c_str());
    }

    // Keep scene draws and each source-copy/filter pair ordered in one submission.
    ZH_IOS_WaitForForeground();
    SDL_GPUCommandBuffer *commands=SDL_AcquireGPUCommandBuffer(GpuDevice);
    if(!commands) {
        const std::string reason=SDL_GetError();
        if(!Flush(FlushHeat))fprintf(stderr,"[SDL_POST] scene flush failed after command acquisition failure\n");
        return failHeat(reason.c_str());
    }
    bool recorded=Record_Batch(commands);
    bool postWritten=false;
    const auto recordPost=[&](float passGrade,unsigned filter,float amount) {
        if(!Record_Heat_Background(commands,target,error)) {
            HeatFailed=true;fprintf(stderr,"[SDL_HEAT] disabled reason=%s\n",error.c_str());return false;
        }
        return Record_Post_Pass(commands,target,HeatBackground,area,passGrade,filter,amount);
    };
    if(recorded&&((HdrActive||grade>0)&&!foldGrade)) {
        recorded=recordPost(grade,0,0);
        postWritten=recorded;
    }
    for(unsigned i=0;recorded&&i<passes;++i) {
        recorded=recordPost(i==0&&foldGrade?grade:0,modes[i],SampleCount>1?-0.25f:0.35f);
        postWritten=postWritten||recorded;
    }
    if(!recorded) {
        const char *reason=error.empty()?SDL_GetError():error.c_str();
        fprintf(stderr,"[SDL_POST] pass recording failed: %s\n",reason);
    }
    const bool submitted=SerializeSubmits?Submit(commands):Submit_Flush(commands);
    End_Batch();
    if(submitted&&postWritten) {
        External_Colour_Write(target.Colour);
        Invalidate_Water_Snapshots(target.Colour,nullptr);
        // The HDR tone pass is always the first pass, so a written post chain means the
        // scene is tone mapped and the render-scale composite may upscale it directly.
        if(HdrActive)HdrToneApplied=true;
    }
    return recorded&&submitted;
}
bool SdlGpuFrame::Record_Post_Pass(SDL_GPUCommandBuffer *commands,const SdlTarget &target,SDL_GPUTexture *source,
    const float rect[4],float grade,unsigned filter,float amount){
    SDL_GPUColorTargetInfo colour={};colour.texture=target.Colour;
    // The pipeline writes RGB only; LOAD preserves the scene alpha even when the viewport covers the target.
    colour.load_op=SDL_GPU_LOADOP_LOAD;colour.store_op=SDL_GPU_STOREOP_STORE;
    #if defined(__APPLE__)
ZH_SDL_Metal_SetNextRenderPassTag(8u);
#endif
auto *pass=SDL_BeginGPURenderPass(commands,&colour,1,nullptr);if(!pass)return false;
    SDL_BindGPUGraphicsPipeline(pass,PostPipeline);SDL_GPUTextureSamplerBinding binding={source,PostSampler};SDL_BindGPUFragmentSamplers(pass,0,&binding,1);
    const float constants[12]={rect[0],rect[1],rect[2],rect[3],HdrActive?1.0f:0.0f,grade,1.0f/target.Width,1.0f/target.Height,float(filter),amount,0,0};
    SDL_PushGPUFragmentUniformData(commands,0,constants,sizeof(constants));
    const SDL_GPUViewport viewport={0,0,float(target.Width),float(target.Height),0,1};SDL_SetGPUViewport(pass,&viewport);
    SDL_DrawGPUPrimitives(pass,3,1,0,0);SDL_EndGPURenderPass(pass);
    return true;
}

// HDR safety net for the render-scale composite. When the post chain did not run, the
// scene still holds linear values above 1; tone map while upscaling - sample the scene
// with the linear post sampler and write the native back buffer - so neither MetalFX
// nor the linear blit ever receives raw HDR. One pass, no extra scene copy.
bool SdlGpuFrame::Tone_Scene_To_Back(){
    if(!GpuDevice||!SceneBuffer||!BackBuffer||!SceneWidth||!SceneHeight||!HdrActive||!Prepare_Post())return false;
    ZH_IOS_WaitForForeground();
    SDL_GPUCommandBuffer *commands=SDL_AcquireGPUCommandBuffer(GpuDevice);
    if(!commands)return false;
    if(!Record_Batch(commands)){SDL_CancelGPUCommandBuffer(commands);return false;}
    const SdlTarget back={BackBuffer,nullptr,BackWidth,BackHeight,0};
    const float rect[4]={0,0,float(BackWidth),float(BackHeight)};
    if(!Record_Post_Pass(commands,back,SceneBuffer,rect,0,0,0)){
        SDL_CancelGPUCommandBuffer(commands);return false;
    }
    const bool submitted=SerializeSubmits?Submit(commands):Submit_Flush(commands);
    End_Batch();
    if(submitted){
        HdrToneApplied=true;
        External_Colour_Write(BackBuffer);
        Invalidate_Water_Snapshots(BackBuffer,nullptr);
        static bool logged=false;
        if(!logged){logged=true;fprintf(stderr,"[SDL_POST] tone composite fallback: scene tone mapped during upscale\n");}
    }
    return submitted;
}
