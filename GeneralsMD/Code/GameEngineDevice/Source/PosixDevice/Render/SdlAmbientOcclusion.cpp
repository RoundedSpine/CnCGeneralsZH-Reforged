#include "SdlAmbientOcclusion.h"
#include "sdl3shadercompile.h"
#include <cstdio>
#include <vector>
namespace SdlAo {
void Renderer::ReleaseTargets() {
    if(ResolvedDepth) SDL_ReleaseGPUTexture(Device,ResolvedDepth);
    ResolvedDepth=nullptr;
    for(auto &texture:Ping) { if(texture) SDL_ReleaseGPUTexture(Device,texture); texture=nullptr; }
    Width=Height=0;
}
Renderer::~Renderer() {
    ReleaseTargets();
    for(auto pipeline:Pipelines) if(pipeline) SDL_ReleaseGPUGraphicsPipeline(Device,pipeline);
    for(auto pixel:Pixels) if(pixel) SDL_ReleaseGPUShader(Device,pixel);
    if(Vertex) SDL_ReleaseGPUShader(Device,Vertex);
    if(Sampler) SDL_ReleaseGPUSampler(Device,Sampler);
    if(ShroudSampler) SDL_ReleaseGPUSampler(Device,ShroudSampler);
}
bool Renderer::Prepare(unsigned width,unsigned height,unsigned colourFormat,std::string &error) {
    const auto usage=SDL_GPU_TEXTUREUSAGE_COLOR_TARGET|SDL_GPU_TEXTUREUSAGE_SAMPLER;
    if(!width||!height||!SDL_GPUTextureSupportsFormat(Device,SDL_GPU_TEXTUREFORMAT_R32G32_FLOAT,SDL_GPU_TEXTURETYPE_2D,usage)
        ||!SDL_GPUTextureSupportsFormat(Device,SDL_GPU_TEXTUREFORMAT_R32_FLOAT,SDL_GPU_TEXTURETYPE_2D,usage)) {
        error="sampleable AO/depth colour targets unsupported"; return false;
    }
    std::vector<unsigned char> spirv;
    if(!Vertex && (!SDL3_Compile_HLSL_To_SPIRV(VertexShader(),true,spirv,error)
        ||!(Vertex=SDL3_Create_Shader(Device,spirv,true,error)))) return false;
    for(unsigned i=0;i<3;++i) {
        if(!Pixels[i]) {
            if(!SDL3_Compile_HLSL_To_SPIRV(PixelShader(i),false,spirv,error)) return false;
            unsigned uniforms;
            SDL3_Shader_Slots(spirv,false,Slots[i],uniforms);
            if(Slots[i]>2 || uniforms!=1) {error="AO shader binding layout mismatch";return false;}
            Pixels[i]=SDL3_Create_Shader(Device,spirv,false,error);
            if(!Pixels[i]) return false;
        }
        if(Pipelines[i] && i==2 && ColourFormat!=colourFormat) {
            SDL_ReleaseGPUGraphicsPipeline(Device,Pipelines[i]); Pipelines[i]=nullptr;
        }
        if(!Pipelines[i]) {
            SDL_GPUColorTargetDescription target={};
            target.format=i==2?(SDL_GPUTextureFormat)colourFormat:SDL_GPU_TEXTUREFORMAT_R32G32_FLOAT;
            if(i==2) {
                target.blend_state.enable_blend=true;
                target.blend_state.src_color_blendfactor=SDL_GPU_BLENDFACTOR_ZERO;
                target.blend_state.dst_color_blendfactor=SDL_GPU_BLENDFACTOR_SRC_COLOR;
                target.blend_state.color_blend_op=SDL_GPU_BLENDOP_ADD;
                target.blend_state.src_alpha_blendfactor=SDL_GPU_BLENDFACTOR_ZERO;
                target.blend_state.dst_alpha_blendfactor=SDL_GPU_BLENDFACTOR_ONE;
                target.blend_state.alpha_blend_op=SDL_GPU_BLENDOP_ADD;
                target.blend_state.enable_color_write_mask=true;
                target.blend_state.color_write_mask=SDL_GPU_COLORCOMPONENT_R|SDL_GPU_COLORCOMPONENT_G|SDL_GPU_COLORCOMPONENT_B;
            }
            SDL_GPUGraphicsPipelineCreateInfo info={};
            info.vertex_shader=Vertex;info.fragment_shader=Pixels[i];
            info.primitive_type=SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
            info.rasterizer_state.fill_mode=SDL_GPU_FILLMODE_FILL;
            info.rasterizer_state.cull_mode=SDL_GPU_CULLMODE_NONE;
            info.target_info.color_target_descriptions=&target;info.target_info.num_color_targets=1;
            Pipelines[i]=SDL_CreateGPUGraphicsPipeline(Device,&info);
            if(!Pipelines[i]) {error=SDL_GetError();return false;}
        }
    }
    ColourFormat=colourFormat;
    if(!Sampler) {
        SDL_GPUSamplerCreateInfo info={};
        info.min_filter=info.mag_filter=SDL_GPU_FILTER_NEAREST;
        info.mipmap_mode=SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
        info.address_mode_u=info.address_mode_v=info.address_mode_w=SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        Sampler=SDL_CreateGPUSampler(Device,&info);
        if(!Sampler) {error=SDL_GetError();return false;}
    }
    if(!ShroudSampler) {
        SDL_GPUSamplerCreateInfo info={};
        info.min_filter=info.mag_filter=SDL_GPU_FILTER_LINEAR;
        info.mipmap_mode=SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
        info.address_mode_u=info.address_mode_v=info.address_mode_w=SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        ShroudSampler=SDL_CreateGPUSampler(Device,&info);
        if(!ShroudSampler) {error=SDL_GetError();return false;}
    }
    if(Width!=width || Height!=height) {
        ReleaseTargets();
        SDL_GPUTextureCreateInfo info={};
        info.type=SDL_GPU_TEXTURETYPE_2D;info.format=SDL_GPU_TEXTUREFORMAT_R32_FLOAT;
        info.usage=usage;info.width=width;info.height=height;info.layer_count_or_depth=info.num_levels=1;
        ResolvedDepth=SDL_CreateGPUTexture(Device,&info);
        info.width=(width+1)/2;info.height=(height+1)/2;info.format=SDL_GPU_TEXTUREFORMAT_R32G32_FLOAT;
        for(auto &texture:Ping) texture=SDL_CreateGPUTexture(Device,&info);
        if(!ResolvedDepth||!Ping[0]||!Ping[1]) {error=SDL_GetError();ReleaseTargets();return false;}
        Width=width;Height=height;
        fprintf(stderr,"[SDL_AO] ready size=%ux%u half=%ux%u\n",width,height,(width+1)/2,(height+1)/2);
    }
    return true;
}
bool Renderer::Render(SDL_GPUCommandBuffer *commands,SDL_GPUTexture *colour,const Constants &constants,SDL_GPUTexture *shroud) {
    if(!commands||!colour||!Width||colour==ResolvedDepth||colour==Ping[0]||colour==Ping[1]) return false;
    for(unsigned passIndex=0;passIndex<4;++passIndex) {
        const unsigned shader=passIndex==0?0:passIndex==3?2:1;
        SDL_GPUColorTargetInfo target={};
        target.texture=passIndex==3?colour:Ping[passIndex==1?1:0];
        target.load_op=passIndex==3?SDL_GPU_LOADOP_LOAD:SDL_GPU_LOADOP_DONT_CARE;
        target.store_op=SDL_GPU_STOREOP_STORE;
        SDL_GPURenderPass *pass=SDL_BeginGPURenderPass(commands,&target,1,nullptr);
        if(!pass) return false;
        SDL_BindGPUGraphicsPipeline(pass,Pipelines[shader]);
        SDL_GPUTextureSamplerBinding bindings[2]={{passIndex==0?(shroud?shroud:ResolvedDepth):Ping[passIndex==2?1:0],
            passIndex==0&&shroud?ShroudSampler:Sampler},{ResolvedDepth,Sampler}};
        SDL_BindGPUFragmentSamplers(pass,0,bindings,Slots[shader]);
        Constants block=constants;
        const unsigned w=passIndex==0?Width:(Width+1)/2,h=passIndex==0?Height:(Height+1)/2;
        block.TexelSize[0]=1.0f/w;block.TexelSize[1]=1.0f/h;block.TexelSize[2]=float(w);block.TexelSize[3]=float(h);
        block.Blur[0]=passIndex==1?1.0f:0.0f;block.Blur[1]=passIndex==2?1.0f:0.0f;
        block.Blur[2]=float(Width);block.Blur[3]=float(Height);
        SDL_PushGPUFragmentUniformData(commands,0,&block,sizeof(block));
        const SDL_GPUViewport viewport={0,0,float(passIndex==3?Width:(Width+1)/2),float(passIndex==3?Height:(Height+1)/2),0,1};
        SDL_SetGPUViewport(pass,&viewport);
        SDL_DrawGPUPrimitives(pass,3,1,0,0);
        SDL_EndGPURenderPass(pass);
    }
    return true;
}
}
