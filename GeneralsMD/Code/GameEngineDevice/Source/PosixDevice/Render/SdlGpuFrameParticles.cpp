// Bounded native overflow particles. GPL-3.0; see LICENSE.md.
#include "SdlGpuFrame.h"
#include "Platform/ParticleBudget.h"
#include "Platform/ParticleShaders.h"
#include "Platform/IOSLifecycle.h"
#include "sdl3shadercompile.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>

using namespace NativeParticleBudget;
struct SdlParticleState {
    unsigned Capacity=0,Count=0,GroundWidth=1,GroundHeight=1,TransferSize=0;
    unsigned First=~0u,Now=~0u,Evaluated=~0u,Evaluations=0;float Smooth=-1,GroundMap[4]={};
    bool Dirty=true;
    SDL_GPUBuffer *Spawns=nullptr,*Systems=nullptr,*Fades=nullptr,*Aged=nullptr;
    SDL_GPUTexture *Atlas=nullptr,*Ground=nullptr,*Sources[Slices]={};
    SDL_GPUTransferBuffer *Transfer=nullptr;
    SDL_GPUComputePipeline *Compute=nullptr;
    SDL_GPUShader *Vertex=nullptr,*Pixel=nullptr;
    SDL_GPUSampler *Linear=nullptr,*Point=nullptr;
    std::map<unsigned,SDL_GPUGraphicsPipeline*> Pipelines;
};
bool SdlGpuFrame::Particle_Failure(const char *why){
    if(!ParticlesFailed)fprintf(stderr,"[SDL_PARTICLES] refused reason=%s error=%s CPU_fallback=1\n",why,SDL_GetError());
    ParticlesFailed=true;return false;
}
void SdlGpuFrame::Release_Particles(){
    if(!Particles)return;auto &p=*Particles;
    for(auto &entry:p.Pipelines)SDL_ReleaseGPUGraphicsPipeline(GpuDevice,entry.second);
    for(auto *buffer:{p.Spawns,p.Systems,p.Fades,p.Aged})if(buffer)SDL_ReleaseGPUBuffer(GpuDevice,buffer);
    if(p.Transfer)SDL_ReleaseGPUTransferBuffer(GpuDevice,p.Transfer);
    if(p.Compute)SDL_ReleaseGPUComputePipeline(GpuDevice,p.Compute);
    if(p.Vertex)SDL_ReleaseGPUShader(GpuDevice,p.Vertex);if(p.Pixel)SDL_ReleaseGPUShader(GpuDevice,p.Pixel);
    if(p.Linear)SDL_ReleaseGPUSampler(GpuDevice,p.Linear);if(p.Point)SDL_ReleaseGPUSampler(GpuDevice,p.Point);
    if(p.Atlas)SDL_ReleaseGPUTexture(GpuDevice,p.Atlas);if(p.Ground)SDL_ReleaseGPUTexture(GpuDevice,p.Ground);
    fprintf(stderr,"[SDL_PARTICLES] released capacity=%u evaluated=%u\n",p.Capacity,p.Count);
    delete Particles;Particles=nullptr;
}
bool SdlGpuFrame::Prepare_Particles(unsigned capacity,bool *fresh){
    ZH_IOS_WaitForForeground();
    if(!fresh)return false;*fresh=false;
    if(!Particles_Available()||!capacity||capacity>Records)return false;
    if(Particles&&Particles->Capacity==capacity)return true;
    if(!Flush()||!SDL_WaitForGPUIdle(GpuDevice))return Particle_Failure("prepare synchronization");
    Release_Particles();Particles=new SdlParticleState;auto &p=*Particles;p.Capacity=capacity;*fresh=true;
    auto buffer=[&](SDL_GPUBuffer *&out,unsigned bytes,SDL_GPUBufferUsageFlags usage){
        SDL_GPUBufferCreateInfo info={};info.size=bytes;info.usage=usage;out=SDL_CreateGPUBuffer(GpuDevice,&info);return out!=nullptr;
    };
    if(!buffer(p.Spawns,capacity*80,SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ)
        ||!buffer(p.Systems,NativeParticleBudget::Systems*240,SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ)
        ||!buffer(p.Fades,NativeParticleBudget::Systems*4,SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ)
        ||!buffer(p.Aged,capacity*32,SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE|SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ))return Particle_Failure("particle buffers");
    std::string error;std::vector<unsigned char> spv;
    if(!SDL3_Compile_HLSL(ParticleShaders::Evaluation(true),SdlShaderStage::Compute,spv,error)
        ||!(p.Compute=SDL3_Create_Compute(GpuDevice,spv,error))) {SDL_SetError("%s",error.c_str());return Particle_Failure("compute shader");}
    if(!SDL3_Compile_HLSL_To_SPIRV(ParticleShaders::Vertex(true),true,spv,error)
        ||!(p.Vertex=SDL3_Create_Shader(GpuDevice,spv,true,error))) {SDL_SetError("%s",error.c_str());return Particle_Failure("vertex shader");}
    if(!SDL3_Compile_HLSL_To_SPIRV(ParticleShaders::Pixel(),false,spv,error)
        ||!(p.Pixel=SDL3_Create_Shader(GpuDevice,spv,false,error))) {SDL_SetError("%s",error.c_str());return Particle_Failure("pixel shader");}
    SDL_GPUSamplerCreateInfo sampler={};sampler.address_mode_u=sampler.address_mode_v=sampler.address_mode_w=SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    p.Point=SDL_CreateGPUSampler(GpuDevice,&sampler);sampler.min_filter=sampler.mag_filter=SDL_GPU_FILTER_LINEAR;
    sampler.mipmap_mode=SDL_GPU_SAMPLERMIPMAPMODE_LINEAR;sampler.max_lod=SpriteMips-1;p.Linear=SDL_CreateGPUSampler(GpuDevice,&sampler);
    SDL_GPUTextureCreateInfo texture={};texture.type=SDL_GPU_TEXTURETYPE_2D_ARRAY;texture.format=SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    texture.usage=SDL_GPU_TEXTUREUSAGE_SAMPLER|SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;texture.width=texture.height=SpriteSize;
    texture.layer_count_or_depth=Slices;texture.num_levels=SpriteMips;p.Atlas=SDL_CreateGPUTexture(GpuDevice,&texture);
    texture.type=SDL_GPU_TEXTURETYPE_2D;texture.format=SDL_GPU_TEXTUREFORMAT_R32_FLOAT;texture.usage=SDL_GPU_TEXTUREUSAGE_SAMPLER;
    texture.width=texture.height=texture.layer_count_or_depth=texture.num_levels=1;p.Ground=SDL_CreateGPUTexture(GpuDevice,&texture);
    if(!p.Point||!p.Linear||!p.Atlas||!p.Ground)return Particle_Failure("particle textures/samplers");
    auto *cmd=SDL_AcquireGPUCommandBuffer(GpuDevice);if(!cmd)return Particle_Failure("atlas command");
    for(unsigned layer=0;layer<Slices;++layer){
        SDL_GPUColorTargetInfo target={};target.texture=p.Atlas;target.layer_or_depth_plane=layer;
        target.load_op=SDL_GPU_LOADOP_CLEAR;target.store_op=SDL_GPU_STOREOP_STORE;
        auto *pass=SDL_BeginGPURenderPass(cmd,&target,1,nullptr);
        if(!pass){SDL_CancelGPUCommandBuffer(cmd);return Particle_Failure("atlas clear");}SDL_EndGPURenderPass(pass);
    }
    SDL_GenerateMipmapsForGPUTexture(cmd,p.Atlas);if(!Submit_Flush(cmd))return Particle_Failure("atlas submit");
    std::vector<unsigned char> zero(std::max(capacity*80,NativeParticleBudget::Systems*240),0);
    if(!Write_Particles(0,0,zero.data(),capacity)||!Write_Particles(1,0,zero.data(),NativeParticleBudget::Systems)
        ||!Write_Particles(2,0,zero.data(),NativeParticleBudget::Systems))return false;
    const float ground=0,map[4]={};if(!Particle_Ground(&ground,1,1,map))return false;
    fprintf(stderr,"[SDL_PARTICLES] ready capacity=%u system_slots=%u atlas_layers=%u base_nominal_mib=%.2f admission=opt-in-additive-overflow\n",
        capacity,NativeParticleBudget::Systems,Slices,(capacity*112.0+NativeParticleBudget::Systems*244.0+Slices*21845.0*4)/1048576.0);
    return true;
}
bool SdlGpuFrame::Write_Particles(unsigned kind,unsigned first,const void *data,unsigned count){
    if(!Particles||!Particles_Available())return false;if(!count)return true;auto &p=*Particles;
    const unsigned limit=kind==0?p.Capacity:NativeParticleBudget::Systems,stride=kind==0?80:kind==1?240:4;
    if(kind>2||!data||first>limit||count>limit-first)return Particle_Failure("upload bounds");
    const unsigned bytes=count*stride;
    if(bytes>p.TransferSize){if(p.Transfer)SDL_ReleaseGPUTransferBuffer(GpuDevice,p.Transfer);
        SDL_GPUTransferBufferCreateInfo info={};info.size=bytes;info.usage=SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        p.Transfer=SDL_CreateGPUTransferBuffer(GpuDevice,&info);p.TransferSize=bytes;}
    if(!p.Transfer)return Particle_Failure("upload transfer");
    void *mapped=SDL_MapGPUTransferBuffer(GpuDevice,p.Transfer,true);if(!mapped)return Particle_Failure("upload map");
    std::memcpy(mapped,data,bytes);SDL_UnmapGPUTransferBuffer(GpuDevice,p.Transfer);
    ZH_IOS_WaitForForeground();auto *cmd=SDL_AcquireGPUCommandBuffer(GpuDevice);if(!cmd)return Particle_Failure("upload command");
    auto *copy=SDL_BeginGPUCopyPass(cmd);if(!copy){SDL_CancelGPUCommandBuffer(cmd);return Particle_Failure("upload copy pass");}
    SDL_GPUTransferBufferLocation source={};source.transfer_buffer=p.Transfer;
    SDL_GPUBufferRegion dest={};dest.buffer=kind==0?p.Spawns:kind==1?p.Systems:p.Fades;dest.offset=first*stride;dest.size=bytes;
    SDL_UploadToGPUBuffer(copy,&source,&dest,false);SDL_EndGPUCopyPass(copy);
    if(!Submit_Flush(cmd))return Particle_Failure("upload submit");p.Dirty=true;return true;
}
bool SdlGpuFrame::Particle_Ground(const float *heights,unsigned width,unsigned height,const float map[4]){
    ZH_IOS_WaitForForeground();
    if(!Particles||!Particles_Available())return false;auto &p=*Particles;
    if(!heights||!map||!width||!height||width>GroundSide||height>GroundSide)return Particle_Failure("ground budget/bounds");
    for(unsigned i=0;i<4;++i)if(!std::isfinite(map[i]))return Particle_Failure("ground mapping");
    if(width!=p.GroundWidth||height!=p.GroundHeight){
        SDL_GPUTextureCreateInfo info={};info.type=SDL_GPU_TEXTURETYPE_2D;info.format=SDL_GPU_TEXTUREFORMAT_R32_FLOAT;
        info.usage=SDL_GPU_TEXTUREUSAGE_SAMPLER;info.width=width;info.height=height;info.layer_count_or_depth=info.num_levels=1;
        auto *ground=SDL_CreateGPUTexture(GpuDevice,&info);if(!ground)return Particle_Failure("ground allocation");
        SDL_ReleaseGPUTexture(GpuDevice,p.Ground);p.Ground=ground;p.GroundWidth=width;p.GroundHeight=height;
    }
    const unsigned bytes=width*height*4;SDL_GPUTransferBufferCreateInfo info={};info.size=bytes;info.usage=SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    auto *transfer=SDL_CreateGPUTransferBuffer(GpuDevice,&info);if(!transfer)return Particle_Failure("ground transfer");
    void *mapped=SDL_MapGPUTransferBuffer(GpuDevice,transfer,false);
    if(!mapped){SDL_ReleaseGPUTransferBuffer(GpuDevice,transfer);return Particle_Failure("ground map");}
    std::memcpy(mapped,heights,bytes);SDL_UnmapGPUTransferBuffer(GpuDevice,transfer);
    auto *cmd=SDL_AcquireGPUCommandBuffer(GpuDevice);if(!cmd){SDL_ReleaseGPUTransferBuffer(GpuDevice,transfer);return Particle_Failure("ground command");}
    auto *copy=SDL_BeginGPUCopyPass(cmd);if(!copy){SDL_CancelGPUCommandBuffer(cmd);SDL_ReleaseGPUTransferBuffer(GpuDevice,transfer);return Particle_Failure("ground copy");}
    SDL_GPUTextureTransferInfo source={};source.transfer_buffer=transfer;source.pixels_per_row=width;source.rows_per_layer=height;
    SDL_GPUTextureRegion dest={};dest.texture=p.Ground;dest.w=width;dest.h=height;dest.d=1;
    SDL_UploadToGPUTexture(copy,&source,&dest,false);SDL_EndGPUCopyPass(copy);
    const bool ok=Submit_Flush(cmd);SDL_ReleaseGPUTransferBuffer(GpuDevice,transfer);
    if(!ok)return Particle_Failure("ground submit");std::memcpy(p.GroundMap,map,16);p.Dirty=true;return true;
}
bool SdlGpuFrame::Particle_Slice(unsigned slice,SDL_GPUTexture *texture,unsigned width,unsigned height){
    ZH_IOS_WaitForForeground();
    if(!Particles||!Particles_Available())return false;auto &p=*Particles;
    if(slice>=Slices||!texture||!width||!height)return Particle_Failure("slice bounds/source");
    if(p.Sources[slice]==texture)return true;if(!Flush())return Particle_Failure("slice flush");
    auto *cmd=SDL_AcquireGPUCommandBuffer(GpuDevice);if(!cmd)return Particle_Failure("slice command");
    SDL_GPUBlitInfo blit={};blit.source.texture=texture;blit.source.w=width;blit.source.h=height;
    blit.destination.texture=p.Atlas;blit.destination.layer_or_depth_plane=slice;blit.destination.w=blit.destination.h=SpriteSize;
    blit.load_op=SDL_GPU_LOADOP_DONT_CARE;blit.filter=SDL_GPU_FILTER_LINEAR;SDL_BlitGPUTexture(cmd,&blit);
    SDL_GenerateMipmapsForGPUTexture(cmd,p.Atlas);if(!Submit_Flush(cmd))return Particle_Failure("slice submit");
    p.Sources[slice]=texture;return true;
}
bool SdlGpuFrame::Evaluate_Particles(unsigned first,unsigned count,unsigned now,float smooth){
    ZH_IOS_WaitForForeground();
    if(!Particles||!Particles_Available())return false;auto &p=*Particles;
    if(first>=p.Capacity||count>p.Capacity||!std::isfinite(smooth)||smooth<0||smooth>1)return Particle_Failure("evaluate bounds/time");
    p.Count=count;if(!count)return true;
    if(!p.Dirty&&p.First==first&&p.Evaluated==count&&p.Now==now&&p.Smooth==smooth)return true;
    if(!Flush())return Particle_Failure("evaluate flush");
    struct Frame {unsigned First,Count,Capacity,Now;float Smooth,Unused[3],GroundMap[4];};
    const Frame constants={first,count,p.Capacity,now,smooth,{0,0,0},{p.GroundMap[0],p.GroundMap[1],p.GroundMap[2],p.GroundMap[3]}};
    auto *cmd=SDL_AcquireGPUCommandBuffer(GpuDevice);if(!cmd)return Particle_Failure("evaluate command");
    SDL_GPUStorageBufferReadWriteBinding write={};write.buffer=p.Aged;
    auto *pass=SDL_BeginGPUComputePass(cmd,nullptr,0,&write,1);
    if(!pass){SDL_CancelGPUCommandBuffer(cmd);return Particle_Failure("compute pass");}
    SDL_BindGPUComputePipeline(pass,p.Compute);SDL_GPUTextureSamplerBinding ground={p.Ground,p.Linear};
    SDL_BindGPUComputeSamplers(pass,0,&ground,1);SDL_GPUBuffer *buffers[]={p.Spawns,p.Systems,p.Fades};
    SDL_BindGPUComputeStorageBuffers(pass,0,buffers,3);SDL_PushGPUComputeUniformData(cmd,0,&constants,sizeof(constants));
    SDL_DispatchGPUCompute(pass,(count+63)/64,1,1);SDL_EndGPUComputePass(pass);
    if(!Submit_Flush(cmd))return Particle_Failure("compute submit");
    p.Dirty=false;p.First=first;p.Evaluated=count;p.Now=now;p.Smooth=smooth;
    if((p.Evaluations++%128)==0)fprintf(stderr,"[SDL_PARTICLES] evaluate range=%u capacity=%u now=%u upload_capacity=%u\n",count,p.Capacity,now,p.TransferSize);
    return true;
}
bool SdlGpuFrame::Draw_Particles(const SdlTarget &target,const float viewport[6],const float view[16],const float projection[16],unsigned shader,bool soft,float gain){
    ZH_IOS_WaitForForeground();
    if(!Particles||!Particles_Available())return false;auto &p=*Particles;if(!p.Count)return true;
    if(!target.Colour||!target.Depth||!viewport||!view||!projection||(shader!=1&&shader!=2))return Particle_Failure("draw target/arguments");
    SDL_GPUTexture *depth=soft?Scene_Depth(target):nullptr;
    if(!Flush())return Particle_Failure("draw flush");
    const unsigned key=shader|(Target_Depth_Format(target)<<2);auto &pipeline=p.Pipelines[key];
    if(!pipeline){
        SDL_GPUColorTargetDescription colour={};colour.format=(SDL_GPUTextureFormat)Scene_Format();auto &blend=colour.blend_state;blend.enable_blend=true;
        blend.src_color_blendfactor=blend.src_alpha_blendfactor=shader==1?SDL_GPU_BLENDFACTOR_ONE:SDL_GPU_BLENDFACTOR_SRC_ALPHA;
        blend.dst_color_blendfactor=blend.dst_alpha_blendfactor=shader==1?SDL_GPU_BLENDFACTOR_ONE:SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        blend.color_blend_op=blend.alpha_blend_op=SDL_GPU_BLENDOP_ADD;
        SDL_GPUGraphicsPipelineCreateInfo info={};info.vertex_shader=p.Vertex;info.fragment_shader=p.Pixel;info.primitive_type=SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
        info.rasterizer_state.cull_mode=SDL_GPU_CULLMODE_NONE;info.depth_stencil_state.enable_depth_test=true;
        info.depth_stencil_state.compare_op=SDL_GPU_COMPAREOP_LESS_OR_EQUAL;info.target_info.has_depth_stencil_target=true;
        info.target_info.depth_stencil_format=(SDL_GPUTextureFormat)Target_Depth_Format(target);info.target_info.num_color_targets=1;info.target_info.color_target_descriptions=&colour;
        pipeline=SDL_CreateGPUGraphicsPipeline(GpuDevice,&info);if(!pipeline)return Particle_Failure("draw pipeline");
    }
    struct Pass {float View[16],Projection[16],SoftDepth[4];unsigned Count,Shader,Newest,Soft;float Gain,Unused[3];} constants={};
    std::memcpy(constants.View,view,64);std::memcpy(constants.Projection,projection,64);
    constants.SoftDepth[0]=projection[11]*projection[14];constants.SoftDepth[1]=projection[11];constants.SoftDepth[2]=projection[10];constants.SoftDepth[3]=1.0f/12;
    constants.Count=p.Count;constants.Shader=shader;constants.Newest=shader==2;constants.Soft=depth!=nullptr;constants.Gain=gain;
    auto *cmd=SDL_AcquireGPUCommandBuffer(GpuDevice);if(!cmd)return Particle_Failure("draw command");
    SDL_GPUColorTargetInfo colour={};colour.texture=target.Colour;colour.load_op=SDL_GPU_LOADOP_LOAD;colour.store_op=SDL_GPU_STOREOP_STORE;
    SDL_GPUDepthStencilTargetInfo z={};z.texture=target.Depth;z.load_op=z.stencil_load_op=SDL_GPU_LOADOP_LOAD;z.store_op=z.stencil_store_op=SDL_GPU_STOREOP_STORE;
    auto *pass=SDL_BeginGPURenderPass(cmd,&colour,1,&z);if(!pass){SDL_CancelGPUCommandBuffer(cmd);return Particle_Failure("draw pass");}
    SDL_BindGPUGraphicsPipeline(pass,pipeline);SDL_BindGPUVertexStorageBuffers(pass,0,&p.Aged,1);
    SDL_GPUTextureSamplerBinding textures[]={{p.Atlas,p.Linear},{depth?depth:p.Ground,p.Point}};
    SDL_BindGPUFragmentSamplers(pass,0,textures,2);
    SDL_PushGPUVertexUniformData(cmd,0,&constants,sizeof(constants));SDL_PushGPUFragmentUniformData(cmd,0,&constants,sizeof(constants));
    const SDL_GPUViewport vp={viewport[0],viewport[1],viewport[2],viewport[3],viewport[4],viewport[5]};SDL_SetGPUViewport(pass,&vp);
    SDL_DrawGPUPrimitives(pass,p.Count*6,1,0,0);SDL_EndGPURenderPass(pass);
    if(!Submit_Flush(cmd))return Particle_Failure("draw submit");
    External_Colour_Write(target.Colour);Invalidate_Water_Snapshots(target.Colour,nullptr);return true;
}
bool SdlGpuFrame::Read_Particles(std::vector<unsigned char> &out){
    ZH_IOS_WaitForForeground();
    out.clear();if(!Particles||!Particles_Available())return false;auto &p=*Particles;if(!p.Count)return true;
    SDL_GPUTransferBufferCreateInfo info={};info.size=p.Count*32;info.usage=SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
    auto *transfer=SDL_CreateGPUTransferBuffer(GpuDevice,&info);if(!transfer)return false;
    auto *cmd=SDL_AcquireGPUCommandBuffer(GpuDevice);if(!cmd){SDL_ReleaseGPUTransferBuffer(GpuDevice,transfer);return false;}
    auto *copy=SDL_BeginGPUCopyPass(cmd);if(!copy){SDL_CancelGPUCommandBuffer(cmd);SDL_ReleaseGPUTransferBuffer(GpuDevice,transfer);return false;}
    SDL_GPUBufferRegion source={};source.buffer=p.Aged;source.size=info.size;SDL_GPUTransferBufferLocation dest={};dest.transfer_buffer=transfer;
    SDL_DownloadFromGPUBuffer(copy,&source,&dest);SDL_EndGPUCopyPass(copy);
    bool ok=Submit_Flush(cmd)&&SDL_WaitForGPUIdle(GpuDevice);
    if(ok){auto *mapped=SDL_MapGPUTransferBuffer(GpuDevice,transfer,false);ok=mapped!=nullptr;
        if(mapped){const auto *bytes=static_cast<unsigned char*>(mapped);out.assign(bytes,bytes+info.size);SDL_UnmapGPUTransferBuffer(GpuDevice,transfer);}}
    SDL_ReleaseGPUTransferBuffer(GpuDevice,transfer);return ok;
}
