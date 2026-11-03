// Distributed under the repository's GPL-3.0 terms; see LICENSE.md.
#include "SdlAtmosphere.h"
#include "sdl3shadercompile.h"
#include <cstdio>
#include <vector>
namespace SdlAtmosphere {
Renderer::~Renderer() {
    if(ResolvedDepth) SDL_ReleaseGPUTexture(Device,ResolvedDepth);
    if(Pipeline) SDL_ReleaseGPUGraphicsPipeline(Device,Pipeline);
    if(FirePipeline) SDL_ReleaseGPUGraphicsPipeline(Device,FirePipeline);
    if(FirePixel) SDL_ReleaseGPUShader(Device,FirePixel);
    if(Vertex) SDL_ReleaseGPUShader(Device,Vertex);
    if(Pixel) SDL_ReleaseGPUShader(Device,Pixel);
    if(Point) SDL_ReleaseGPUSampler(Device,Point);
    if(Linear) SDL_ReleaseGPUSampler(Device,Linear);
    if(Repeat) SDL_ReleaseGPUSampler(Device,Repeat);
}
bool Renderer::Prepare(unsigned width,unsigned height,unsigned format,std::string &error) {
    if(!width||!height||!SDL_GPUTextureSupportsFormat(Device,SDL_GPU_TEXTUREFORMAT_R32_FLOAT,SDL_GPU_TEXTURETYPE_2D,
        SDL_GPU_TEXTUREUSAGE_COLOR_TARGET|SDL_GPU_TEXTUREUSAGE_SAMPLER)) {error="sampleable atmosphere depth unavailable";return false;}
    std::vector<unsigned char> spirv;
    if(!Vertex && (!SDL3_Compile_HLSL_To_SPIRV(VertexShader(),true,spirv,error)
        ||!(Vertex=SDL3_Create_Shader(Device,spirv,true,error)))) return false;
    if(!Pixel) {
        if(!SDL3_Compile_HLSL_To_SPIRV(PixelShader(),false,spirv,error)) return false;
        unsigned slots,uniforms;SDL3_Shader_Slots(spirv,false,slots,uniforms);
        if(slots!=3||uniforms!=1) {error="atmosphere shader binding mismatch";return false;}
        Pixel=SDL3_Create_Shader(Device,spirv,false,error);if(!Pixel)return false;
    }
    if(ColourFormat!=format) {
        if(Pipeline)SDL_ReleaseGPUGraphicsPipeline(Device,Pipeline);
        Pipeline=nullptr;
        if(FirePipeline)SDL_ReleaseGPUGraphicsPipeline(Device,FirePipeline);
        FirePipeline=nullptr;
    }
    if(!Pipeline) {
        SDL_GPUColorTargetDescription target={};target.format=(SDL_GPUTextureFormat)format;
        auto &blend=target.blend_state;blend.enable_blend=true;
        blend.src_color_blendfactor=SDL_GPU_BLENDFACTOR_SRC_ALPHA;
        blend.dst_color_blendfactor=SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        blend.color_blend_op=SDL_GPU_BLENDOP_ADD;
        blend.src_alpha_blendfactor=SDL_GPU_BLENDFACTOR_ZERO;blend.dst_alpha_blendfactor=SDL_GPU_BLENDFACTOR_ONE;
        blend.alpha_blend_op=SDL_GPU_BLENDOP_ADD;blend.enable_color_write_mask=true;
        blend.color_write_mask=SDL_GPU_COLORCOMPONENT_R|SDL_GPU_COLORCOMPONENT_G|SDL_GPU_COLORCOMPONENT_B;
        SDL_GPUGraphicsPipelineCreateInfo info={};info.vertex_shader=Vertex;info.fragment_shader=Pixel;
        info.primitive_type=SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;info.rasterizer_state.cull_mode=SDL_GPU_CULLMODE_NONE;
        info.rasterizer_state.fill_mode=SDL_GPU_FILLMODE_FILL;info.target_info.num_color_targets=1;
        info.target_info.color_target_descriptions=&target;
        Pipeline=SDL_CreateGPUGraphicsPipeline(Device,&info);if(!Pipeline){error=SDL_GetError();return false;}
        ColourFormat=format;
    }
    if(!Point||!Linear||!Repeat) {
        SDL_GPUSamplerCreateInfo info={};info.min_filter=info.mag_filter=SDL_GPU_FILTER_NEAREST;
        info.mipmap_mode=SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
        info.address_mode_u=info.address_mode_v=info.address_mode_w=SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        if(!Point)Point=SDL_CreateGPUSampler(Device,&info);
        info.min_filter=info.mag_filter=SDL_GPU_FILTER_LINEAR;
        if(!Linear)Linear=SDL_CreateGPUSampler(Device,&info);
        info.mipmap_mode=SDL_GPU_SAMPLERMIPMAPMODE_LINEAR;
        info.address_mode_u=info.address_mode_v=info.address_mode_w=SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
        if(!Repeat)Repeat=SDL_CreateGPUSampler(Device,&info);
        if(!Point||!Linear||!Repeat){error=SDL_GetError();return false;}
    }
    if(width!=Width||height!=Height) {
        if(ResolvedDepth)SDL_ReleaseGPUTexture(Device,ResolvedDepth);
        ResolvedDepth=nullptr;Width=Height=0;
        SDL_GPUTextureCreateInfo info={};info.type=SDL_GPU_TEXTURETYPE_2D;info.format=SDL_GPU_TEXTUREFORMAT_R32_FLOAT;
        info.usage=SDL_GPU_TEXTUREUSAGE_COLOR_TARGET|SDL_GPU_TEXTUREUSAGE_SAMPLER;
        info.width=width;info.height=height;info.layer_count_or_depth=info.num_levels=1;
        ResolvedDepth=SDL_CreateGPUTexture(Device,&info);if(!ResolvedDepth){error=SDL_GetError();return false;}
        Width=width;Height=height;fprintf(stderr,"[SDL_ATMOSPHERE] ready size=%ux%u\n",Width,Height);
    }
    return true;
}
bool Renderer::PrepareFirelight(unsigned width,unsigned height,unsigned format,std::string &error) {
    // Shares the fullscreen vertex program, sampled depth and resource lifetime.
    if(!Prepare(width,height,format,error))return false;
    if(!FirePixel) {
        std::vector<unsigned char> spirv;
        if(!SDL3_Compile_HLSL_To_SPIRV(SdlFirelight::PixelShader(),false,spirv,error))return false;
        unsigned slots,uniforms;SDL3_Shader_Slots(spirv,false,slots,uniforms);
        if(slots!=1||uniforms!=1){error="firelight shader binding mismatch";return false;}
        FirePixel=SDL3_Create_Shader(Device,spirv,false,error);if(!FirePixel)return false;
    }
    if(!FirePipeline) {
        SDL_GPUColorTargetDescription target={};target.format=(SDL_GPUTextureFormat)format;
        auto &blend=target.blend_state;blend.enable_blend=true;
        blend.src_color_blendfactor=SDL_GPU_BLENDFACTOR_DST_COLOR;blend.dst_color_blendfactor=SDL_GPU_BLENDFACTOR_ONE;
        blend.color_blend_op=blend.alpha_blend_op=SDL_GPU_BLENDOP_ADD;
        blend.src_alpha_blendfactor=SDL_GPU_BLENDFACTOR_ZERO;blend.dst_alpha_blendfactor=SDL_GPU_BLENDFACTOR_ONE;
        blend.enable_color_write_mask=true;blend.color_write_mask=SDL_GPU_COLORCOMPONENT_R|SDL_GPU_COLORCOMPONENT_G|SDL_GPU_COLORCOMPONENT_B;
        SDL_GPUGraphicsPipelineCreateInfo info={};info.vertex_shader=Vertex;info.fragment_shader=FirePixel;
        info.primitive_type=SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;info.rasterizer_state.fill_mode=SDL_GPU_FILLMODE_FILL;
        info.rasterizer_state.cull_mode=SDL_GPU_CULLMODE_NONE;
        info.target_info.color_target_descriptions=&target;info.target_info.num_color_targets=1;
        FirePipeline=SDL_CreateGPUGraphicsPipeline(Device,&info);if(!FirePipeline){error=SDL_GetError();return false;}
        fprintf(stderr,"[SDL_FIRELIGHT] ready\n");
    }
    return true;
}
bool Renderer::RenderFirelight(SDL_GPUCommandBuffer *commands,SDL_GPUTexture *colour,const SdlFirelight::Constants &constants) {
    if(!commands||!colour||!FirePipeline||!ResolvedDepth||colour==ResolvedDepth)return false;
    SDL_GPUColorTargetInfo target={};target.texture=colour;target.load_op=SDL_GPU_LOADOP_LOAD;target.store_op=SDL_GPU_STOREOP_STORE;
    auto *pass=SDL_BeginGPURenderPass(commands,&target,1,nullptr);if(!pass)return false;
    SDL_BindGPUGraphicsPipeline(pass,FirePipeline);SDL_GPUTextureSamplerBinding depth={ResolvedDepth,Point};
    SDL_BindGPUFragmentSamplers(pass,0,&depth,1);
    auto block=constants;block.DepthInfo[0]=1.0f/Width;block.DepthInfo[1]=1.0f/Height;
    SDL_PushGPUFragmentUniformData(commands,0,&block,sizeof(block));
    const SDL_GPUViewport viewport={0,0,float(Width),float(Height),0,1};SDL_SetGPUViewport(pass,&viewport);
    SDL_DrawGPUPrimitives(pass,3,1,0,0);SDL_EndGPURenderPass(pass);return true;
}
bool Renderer::Render(SDL_GPUCommandBuffer *commands,SDL_GPUTexture *colour,const Constants &constants,
    SDL_GPUTexture *sky,SDL_GPUTexture *shroud) {
    if(!commands||!colour||!Pipeline||!ResolvedDepth||colour==ResolvedDepth||colour==sky||colour==shroud
        ||(constants.Flags[0]!=0&&!shroud)||(constants.Flags[1]!=0&&!sky))return false;
    SDL_GPUColorTargetInfo target={};target.texture=colour;target.load_op=SDL_GPU_LOADOP_LOAD;target.store_op=SDL_GPU_STOREOP_STORE;
    auto *pass=SDL_BeginGPURenderPass(commands,&target,1,nullptr);if(!pass)return false;
    SDL_BindGPUGraphicsPipeline(pass,Pipeline);
    SDL_GPUTextureSamplerBinding bindings[3]={{ResolvedDepth,Point},{shroud?shroud:ResolvedDepth,shroud?Linear:Point},
        {sky?sky:ResolvedDepth,sky?Repeat:Point}};
    SDL_BindGPUFragmentSamplers(pass,0,bindings,3);
    Constants block=constants;block.DepthInfo[0]=1.0f/Width;block.DepthInfo[1]=1.0f/Height;
    SDL_PushGPUFragmentUniformData(commands,0,&block,sizeof(block));
    const SDL_GPUViewport viewport={0,0,float(Width),float(Height),0,1};SDL_SetGPUViewport(pass,&viewport);
    SDL_DrawGPUPrimitives(pass,3,1,0,0);SDL_EndGPURenderPass(pass);return true;
}
}
