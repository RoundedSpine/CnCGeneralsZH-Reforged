// SDL multisample attachments resolve colour after each pass. SDL cannot sample
// multisample depth, so alpha-tested depth/stencil is replayed into the independent
// single-sample attachment used by AO, particles, water and CPU diagnostics.
#include "SdlGpuFrame.h"
#include "SdlAoShader.h"
#include "sdl3shadercompile.h"
#include <SDL3/SDL.h>
#include <cstdio>
#include <algorithm>
static SDL_GPUSampleCount sampleIndex(unsigned count){return count>=8?SDL_GPU_SAMPLECOUNT_8:count>=4?SDL_GPU_SAMPLECOUNT_4:count>=2?SDL_GPU_SAMPLECOUNT_2:SDL_GPU_SAMPLECOUNT_1;}
unsigned SdlGpuFrame::Max_Samples() const {
    if(!GpuDevice||MsaaFailed)return 1;
    unsigned highest=1;
    for(unsigned count:{2u,4u,8u}) {
        if(!SDL_GPUTextureSupportsSampleCount(GpuDevice,(SDL_GPUTextureFormat)Scene_Format(),sampleIndex(count))
            ||!SDL_GPUTextureSupportsSampleCount(GpuDevice,(SDL_GPUTextureFormat)DepthFormat,sampleIndex(count)))break;
        highest=count;
    }
    return highest;
}
unsigned SdlGpuFrame::Target_Sample_Index(const SdlTarget &target) const {
    return target.Colour&&target.Depth&&Target_Depth_Format(target)==DepthFormat?unsigned(sampleIndex(SampleCount)):0;
}
bool SdlGpuFrame::Disable_Multisampling(const char *why){
    if(!Flush())return false;
    MsaaFailed=true;SampleCount=1;Release_Multisampling(false);
    fprintf(stderr,"[SDL_MSAA] disabled reason=%s\n",why);return true;
}
void SdlGpuFrame::External_Colour_Write(SDL_GPUTexture *texture){
    if(SampleCount==1)return;auto found=SampleColours.find(texture);if(found!=SampleColours.end())found->second.Current=false;
}
void SdlGpuFrame::Release_Multisampling(bool shaders){
    for(auto &entry:SampleColours)if(entry.second.Texture)SDL_ReleaseGPUTexture(GpuDevice,entry.second.Texture);
    for(auto &entry:SampleDepths)if(entry.second.Texture)SDL_ReleaseGPUTexture(GpuDevice,entry.second.Texture);
    SampleColours.clear();SampleDepths.clear();
    if(!shaders)return;
    if(SampleSeedPipeline)SDL_ReleaseGPUGraphicsPipeline(GpuDevice,SampleSeedPipeline);
    if(SampleVertex)SDL_ReleaseGPUShader(GpuDevice,SampleVertex);
    if(SamplePixel)SDL_ReleaseGPUShader(GpuDevice,SamplePixel);
    if(SampleSampler)SDL_ReleaseGPUSampler(GpuDevice,SampleSampler);
    if(DepthClearPixel)SDL_ReleaseGPUShader(GpuDevice,DepthClearPixel);
    SampleSeedPipeline=nullptr;SampleVertex=SamplePixel=DepthClearPixel=nullptr;SampleSampler=nullptr;
}
bool SdlGpuFrame::Prepare_Multisampling(){
    if(SampleCount==1)return true;if(SampleSeedPipeline)return true;
    const char *pixel=R"(
Texture2D<float4> Source:register(t0,space2);SamplerState Point:register(s0,space2);
float4 main(float4 position:SV_Position):SV_Target {uint w,h;Source.GetDimensions(w,h);return Source.SampleLevel(Point,position.xy/float2(w,h),0);}
)";
    std::string error;std::vector<unsigned char> spirv;
    if(!SDL3_Compile_HLSL_To_SPIRV(SdlAo::VertexShader(),true,spirv,error)
        ||!(SampleVertex=SDL3_Create_Shader(GpuDevice,spirv,true,error)))return false;
    if(!SDL3_Compile_HLSL_To_SPIRV(pixel,false,spirv,error)
        ||!(SamplePixel=SDL3_Create_Shader(GpuDevice,spirv,false,error)))return false;
    SDL_GPUSamplerCreateInfo sampler={};sampler.address_mode_u=sampler.address_mode_v=sampler.address_mode_w=SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    SampleSampler=SDL_CreateGPUSampler(GpuDevice,&sampler);if(!SampleSampler)return false;
    SDL_GPUColorTargetDescription colour={};colour.format=(SDL_GPUTextureFormat)Scene_Format();
    SDL_GPUGraphicsPipelineCreateInfo pipeline={};pipeline.vertex_shader=SampleVertex;pipeline.fragment_shader=SamplePixel;
    pipeline.primitive_type=SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;pipeline.rasterizer_state.cull_mode=SDL_GPU_CULLMODE_NONE;
    pipeline.target_info.num_color_targets=1;pipeline.target_info.color_target_descriptions=&colour;
    pipeline.multisample_state.sample_count=sampleIndex(SampleCount);
    SampleSeedPipeline=SDL_CreateGPUGraphicsPipeline(GpuDevice,&pipeline);
    const bool stencil=DepthFormat==SDL_GPU_TEXTUREFORMAT_D24_UNORM_S8_UINT||DepthFormat==SDL_GPU_TEXTUREFORMAT_D32_FLOAT_S8_UINT;
    return SampleSeedPipeline&&Clear_Pipeline(2,true)&&(!stencil||(Clear_Pipeline(4,true)&&Clear_Pipeline(6,true)));
}
bool SdlGpuFrame::Prepare_Multisample_Target(const SdlTarget &target){
    if(!Target_Sample_Index(target))return true;
    auto prepare=[&](auto &table,SDL_GPUTexture *source,bool depth){
        auto found=table.find(source);if(found!=table.end())return true;
        SDL_GPUTextureCreateInfo info={};info.type=SDL_GPU_TEXTURETYPE_2D;
        info.format=(SDL_GPUTextureFormat)(depth?DepthFormat:Scene_Format());
        info.usage=depth?SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET:SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
        info.width=target.Width;info.height=target.Height;info.num_levels=info.layer_count_or_depth=1;info.sample_count=sampleIndex(SampleCount);
        SampleTexture value;value.Texture=SDL_CreateGPUTexture(GpuDevice,&info);value.Width=target.Width;value.Height=target.Height;
        if(!value.Texture)return false;table.emplace(source,value);return true;
    };
    return prepare(SampleColours,target.Colour,false)&&prepare(SampleDepths,target.Depth,true);
}
bool SdlGpuFrame::Seed_Multisample_Colour(SDL_GPUCommandBuffer *commands,const SdlTarget &target){
    auto found=SampleColours.find(target.Colour);if(found==SampleColours.end())return false;
    if(found->second.Current)return true;
    SDL_GPUColorTargetInfo attachment={};attachment.texture=found->second.Texture;
    attachment.load_op=SDL_GPU_LOADOP_DONT_CARE;attachment.store_op=SDL_GPU_STOREOP_STORE;
    // The source is read here, then resolved only by the following scene pass.
    auto *pass=SDL_BeginGPURenderPass(commands,&attachment,1,nullptr);if(!pass)return false;
    SDL_BindGPUGraphicsPipeline(pass,SampleSeedPipeline);SDL_GPUTextureSamplerBinding binding={target.Colour,SampleSampler};
    SDL_BindGPUFragmentSamplers(pass,0,&binding,1);SDL_GPUViewport viewport={0,0,float(target.Width),float(target.Height),0,1};SDL_SetGPUViewport(pass,&viewport);
    SDL_DrawGPUPrimitives(pass,3,1,0,0);SDL_EndGPURenderPass(pass);found->second.Current=true;return true;
}
void SdlGpuFrame::Draw_Depth_Record(SDL_GPUCommandBuffer *commands,SDL_GPURenderPass *pass,SDL_GPUBuffer *stream,const SdlRecordedDraw &draw){
    SDL_BindGPUGraphicsPipeline(pass,draw.DepthPipeline);
    const SDL_GPUViewport viewport={draw.Viewport[0],draw.Viewport[1],draw.Viewport[2],draw.Viewport[3],draw.Viewport[4],draw.Viewport[5]};SDL_SetGPUViewport(pass,&viewport);
    const SDL_Rect scissor={draw.Scissor[0],draw.Scissor[1],draw.Scissor[2],draw.Scissor[3]};SDL_SetGPUScissor(pass,&scissor);
    SDL_SetGPUStencilReference(pass,(Uint8)draw.StencilReference);
    SDL_GPUBufferBinding vertices={draw.VertexBuffer?draw.VertexBuffer:stream,draw.VertexOffset};SDL_BindGPUVertexBuffers(pass,0,&vertices,1);
    if(draw.IndexSize){SDL_GPUBufferBinding indices={draw.IndexBuffer?draw.IndexBuffer:stream,draw.IndexOffset};SDL_BindGPUIndexBuffer(pass,&indices,draw.IndexSize==4?SDL_GPU_INDEXELEMENTSIZE_32BIT:SDL_GPU_INDEXELEMENTSIZE_16BIT);}
    SDL_GPUTextureSamplerBinding bindings[SdlRecordedDraw::MAXIMUM_SAMPLERS]={};
    for(unsigned slot=0;slot<draw.DepthSamplers;++slot)bindings[slot]={draw.DepthTextures[slot],draw.DepthSamplerBindings[slot]};
    if(draw.DepthSamplers)SDL_BindGPUFragmentSamplers(pass,0,bindings,draw.DepthSamplers);
    for(unsigned slot=0;slot<draw.VertexSamplerCount;++slot)bindings[slot]={draw.VertexTextures[slot],draw.VertexSamplers[slot]};
    if(draw.VertexSamplerCount)SDL_BindGPUVertexSamplers(pass,0,bindings,draw.VertexSamplerCount);
    if(draw.VertexConstantsSize)SDL_PushGPUVertexUniformData(commands,0,&ConstantBytes[draw.VertexConstants],draw.VertexConstantsSize);
    if(draw.VertexWaterConstantsSize)SDL_PushGPUVertexUniformData(commands,1,&ConstantBytes[draw.VertexWaterConstants],draw.VertexWaterConstantsSize);
    if(draw.DepthUniforms&&draw.PixelConstantsSize)SDL_PushGPUFragmentUniformData(commands,0,&ConstantBytes[draw.PixelConstants],draw.PixelConstantsSize);
    for(unsigned block=0;block+1<draw.DepthUniforms&&block<SdlRecordedDraw::WATER_PIXEL_BLOCKS;++block)
        if(draw.PixelWaterConstantsSize[block])SDL_PushGPUFragmentUniformData(commands,block+1,&ConstantBytes[draw.PixelWaterConstants[block]],draw.PixelWaterConstantsSize[block]);
    if(draw.IndexSize)SDL_DrawGPUIndexedPrimitives(pass,draw.Count,1,draw.First,draw.BaseVertex,0);else SDL_DrawGPUPrimitives(pass,draw.Count,1,draw.First,0);
}
bool SdlGpuFrame::Replay_Depth(SDL_GPUCommandBuffer *commands,SDL_GPUBuffer *stream,const SdlTarget &target,size_t first,size_t end){
    if(!Target_Sample_Index(target))return true;
    bool any=false;for(size_t i=first;i<end;++i){const auto &c=Commands[i];if((c.IsDraw&&Draws[c.Draw].DepthPipeline)||(c.IsRect&&(c.Depth||c.Stencil)))any=true;}
    if(!any)return true;
    const bool stencil=Target_Depth_Format(target)==SDL_GPU_TEXTUREFORMAT_D24_UNORM_S8_UINT||Target_Depth_Format(target)==SDL_GPU_TEXTUREFORMAT_D32_FLOAT_S8_UINT;
    SDL_GPUDepthStencilTargetInfo depth={};depth.texture=target.Depth;depth.load_op=SDL_GPU_LOADOP_LOAD;depth.store_op=SDL_GPU_STOREOP_STORE;
    depth.stencil_load_op=stencil?SDL_GPU_LOADOP_LOAD:SDL_GPU_LOADOP_DONT_CARE;depth.stencil_store_op=stencil?SDL_GPU_STOREOP_STORE:SDL_GPU_STOREOP_DONT_CARE;
    auto *pass=SDL_BeginGPURenderPass(commands,nullptr,0,&depth);if(!pass)return false;
    for(size_t i=first;i<end;++i){const auto &c=Commands[i];
        if(c.IsDraw&&Draws[c.Draw].DepthPipeline)Draw_Depth_Record(commands,pass,stream,Draws[c.Draw]);
        else if(c.IsRect&&(c.Depth||c.Stencil)){
            auto *pipeline=Clear_Pipeline((c.Depth?2:0)|(c.Stencil?4:0),true);if(!pipeline){SDL_EndGPURenderPass(pass);return false;}
            SDL_BindGPUGraphicsPipeline(pass,pipeline);const SDL_GPUViewport viewport={0,0,float(target.Width),float(target.Height),0,1};SDL_SetGPUViewport(pass,&viewport);
            const SDL_Rect scissor={c.Rect[0],c.Rect[1],c.Rect[2],c.Rect[3]};SDL_SetGPUScissor(pass,&scissor);SDL_SetGPUStencilReference(pass,(Uint8)c.StencilValue);
            const float constants[8]={0,0,0,0,c.Z,0,0,0};SDL_PushGPUVertexUniformData(commands,0,constants,sizeof(constants));SDL_DrawGPUPrimitives(pass,3,1,0,0);
        }
    }
    SDL_EndGPURenderPass(pass);return true;
}
