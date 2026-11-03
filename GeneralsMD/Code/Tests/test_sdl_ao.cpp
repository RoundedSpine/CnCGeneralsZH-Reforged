// Compile the production shaders through SPIR-V and MSL, then render/read back
// flat ground and a raised object. GPU checks skip explicitly if none is available.
#include "SdlAmbientOcclusion.h"
#include "SdlAtmosphere.h"
#include "SdlConstants.h"
#include "ffshader.h"
#include "sdl3shadercompile.h"
#include <cstdio>
#include <cstring>
#include <vector>
#include <string>
#include <cmath>
#include <algorithm>

static int failures=0;
#define CHECK(x) do {if(!(x)) {if(++failures<=20)std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);}} while(0)

static bool upload(SDL_GPUDevice *device,SDL_GPUCommandBuffer *commands,SDL_GPUTexture *texture,
    unsigned width,unsigned height,const void *data)
{
    // The pinned Metal upload path uses destination width as its row stride.
    // Match the production uploader: tightly packed rows, implicit transfer pitch.
    SDL_GPUTransferBufferCreateInfo info={};info.usage=SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;info.size=width*height*4;
    auto *transfer=SDL_CreateGPUTransferBuffer(device,&info);if(!transfer)return false;
    auto *mapped=(unsigned char*)SDL_MapGPUTransferBuffer(device,transfer,false);
    if(!mapped) {SDL_ReleaseGPUTransferBuffer(device,transfer);return false;}
    std::memcpy(mapped,data,width*height*4);
    SDL_UnmapGPUTransferBuffer(device,transfer);
    auto *pass=SDL_BeginGPUCopyPass(commands);
    if(!pass) {SDL_ReleaseGPUTransferBuffer(device,transfer);return false;}
    SDL_GPUTextureTransferInfo source={};source.transfer_buffer=transfer;
    SDL_GPUTextureRegion region={};region.texture=texture;region.w=width;region.h=height;region.d=1;
    SDL_UploadToGPUTexture(pass,&source,&region,false);SDL_EndGPUCopyPass(pass);
    SDL_ReleaseGPUTransferBuffer(device,transfer);return true;
}
template<class Draw>
static std::vector<unsigned char> renderPass(SDL_GPUDevice *device,SDL_GPUTexture *resolvedDepth,
    SDL_GPUTexture *colour,const std::vector<float> &depth,unsigned width,unsigned height,Draw draw,
    SDL_GPUTexture *shroud=nullptr,unsigned green=0,SDL_GPUTexture *sky=nullptr,unsigned base=180)
{
    auto *commands=SDL_AcquireGPUCommandBuffer(device);CHECK(commands);if(!commands)return {};
    std::vector<unsigned char> pixels(width*height*4,(unsigned char)base);
    for(unsigned i=3;i<pixels.size();i+=4)pixels[i]=111;
    const unsigned char shroudPixel[4]={255,(unsigned char)green,255,255};
    const unsigned char skyPixel[4]={40,80,160,255};
    if(sky && !upload(device,commands,sky,1,1,skyPixel)) {SDL_CancelGPUCommandBuffer(commands);CHECK(false);return {};}
    if(shroud && !upload(device,commands,shroud,1,1,shroudPixel)) {SDL_CancelGPUCommandBuffer(commands);CHECK(false);return {};}
    if(!upload(device,commands,colour,width,height,pixels.data())
        ||(resolvedDepth&&!upload(device,commands,resolvedDepth,width,height,depth.data()))
        ||!draw(commands)) {SDL_CancelGPUCommandBuffer(commands);CHECK(false);return {};}
    const unsigned pitch=(width+63)&~63u;
    SDL_GPUTransferBufferCreateInfo info={};info.usage=SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;info.size=pitch*height*4;
    auto *transfer=SDL_CreateGPUTransferBuffer(device,&info);CHECK(transfer);
    if(!transfer){SDL_CancelGPUCommandBuffer(commands);return {};}
    auto *pass=SDL_BeginGPUCopyPass(commands);CHECK(pass);
    if(!pass){SDL_ReleaseGPUTransferBuffer(device,transfer);SDL_CancelGPUCommandBuffer(commands);return {};}
    SDL_GPUTextureRegion region={};region.texture=colour;region.w=width;region.h=height;region.d=1;
    SDL_GPUTextureTransferInfo destination={};destination.transfer_buffer=transfer;destination.pixels_per_row=pitch;destination.rows_per_layer=height;
    SDL_DownloadFromGPUTexture(pass,&region,&destination);SDL_EndGPUCopyPass(pass);
    auto *fence=SDL_SubmitGPUCommandBufferAndAcquireFence(commands);CHECK(fence);
    if(!fence){SDL_ReleaseGPUTransferBuffer(device,transfer);return {};}
    CHECK(SDL_WaitForGPUFences(device,true,&fence,1));SDL_ReleaseGPUFence(device,fence);
    const auto *mapped=(const unsigned char*)SDL_MapGPUTransferBuffer(device,transfer,false);CHECK(mapped);
    if(mapped){for(unsigned y=0;y<height;++y)std::memcpy(pixels.data()+y*width*4,mapped+y*pitch*4,width*4);SDL_UnmapGPUTransferBuffer(device,transfer);}
    SDL_ReleaseGPUTransferBuffer(device,transfer);return mapped?pixels:std::vector<unsigned char>{};
}
static std::vector<unsigned char> render(SDL_GPUDevice *device,SdlAo::Renderer &renderer,
    SDL_GPUTexture *colour,const std::vector<float> &depth,unsigned width,unsigned height,SdlAo::Constants constants,SDL_GPUTexture *shroud=nullptr,unsigned green=0)
{
    return renderPass(device,renderer.Depth(),colour,depth,width,height,
        [&](SDL_GPUCommandBuffer *commands){return renderer.Render(commands,colour,constants,shroud);},shroud,green);
}
static void testAtmosphere(SDL_GPUDevice *device)
{
    const unsigned width=257,height=193;
    SdlAtmosphere::Renderer renderer(device);std::string error;
    CHECK(renderer.Prepare(width,height,SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,error));
    if(!renderer.Depth()){std::fprintf(stderr,"%s\n",error.c_str());return;}
    SDL_GPUTextureCreateInfo info={};info.type=SDL_GPU_TEXTURETYPE_2D;info.format=SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    info.usage=SDL_GPU_TEXTUREUSAGE_COLOR_TARGET|SDL_GPU_TEXTUREUSAGE_SAMPLER;
    info.width=width;info.height=height;info.layer_count_or_depth=info.num_levels=1;
    auto *colour=SDL_CreateGPUTexture(device,&info);info.width=info.height=1;
    auto *shroud=SDL_CreateGPUTexture(device,&info),*sky=SDL_CreateGPUTexture(device,&info);
    CHECK(colour&&shroud&&sky);
    if(!colour||!shroud||!sky){if(colour)SDL_ReleaseGPUTexture(device,colour);if(shroud)SDL_ReleaseGPUTexture(device,shroud);if(sky)SDL_ReleaseGPUTexture(device,sky);return;}
    SdlAtmosphere::Constants c={};c.WorldFromClip[0]=c.WorldFromClip[5]=50;c.WorldFromClip[10]=-1100;
    c.WorldFromClip[12]=c.WorldFromClip[13]=100;c.WorldFromClip[14]=100;c.WorldFromClip[15]=1;
    c.Viewport[2]=width;c.Viewport[3]=height;c.DepthInfo[3]=1;
    c.ShroudRect[2]=c.ShroudRect[3]=1;c.Parameters.MapSize[0]=c.Parameters.MapSize[1]=1000;
    std::vector<float> depth(width*height,0.05f);
    auto draw=[&](bool fog=false,unsigned green=255,bool useSky=false,unsigned base=180) {
        c.Flags[0]=fog?1.0f:0.0f;c.Flags[1]=useSky?1.0f:0.0f;
        return renderPass(device,renderer.Depth(),colour,depth,width,height,
            [&](SDL_GPUCommandBuffer *commands){return renderer.Render(commands,colour,c,useSky?sky:nullptr,fog?shroud:nullptr);},
            fog?shroud:nullptr,green,useSky?sky:nullptr,base);
    };
    auto neutral=[&](const std::vector<unsigned char> &pixels,unsigned base=180) {
        CHECK(pixels.size()==width*height*4);
        for(unsigned i=0;i<pixels.size();++i)CHECK(std::abs(int(pixels[i])-int(i%4==3?111:base))<=1);
    };
    neutral(draw()); // Disabled is an exact no-op, including alpha.
    c.Parameters.Haze[0]=100;c.Parameters.Haze[1]=100;c.Parameters.Haze[2]=0.6f;c.Parameters.Haze[3]=700;
    c.Parameters.HazeColour[0]=0.9f;c.Parameters.HazeColour[1]=0.8f;c.Parameters.HazeColour[2]=0.7f;
    auto haze=draw();CHECK(haze.size()==width*height*4);
    if(!haze.empty())CHECK(haze[(96*width+128)*4]>183);
    for(unsigned i=3;i<haze.size();i+=4)CHECK(haze[i]==111);
    neutral(draw(true,0)); // Shroud must not gain a haze wash.
    c.Parameters.HazeColour[3]=1000;neutral(draw()); // Submerged ground stays clear.
    c.Parameters.HazeColour[3]=-1e6f;c.Parameters.Haze[0]=0;neutral(draw()); // Above the layer.
    c.Parameters.Haze[2]=0;
    c.Parameters.Fade[0]=50;c.Parameters.Fade[1]=1000;c.Parameters.Fade[2]=0.85f;
    c.Parameters.Horizon[0]=0.2f;c.Parameters.Horizon[1]=0.4f;c.Parameters.Horizon[2]=0.8f;
    c.Parameters.Tint[0]=c.Parameters.Tint[1]=c.Parameters.Tint[2]=1;
    neutral(draw()); // Near ground is in front of the focus/start distance.
    std::fill(depth.begin(),depth.end(),0.4f);
    auto distance=draw();CHECK(distance.size()==width*height*4);
    if(!distance.empty()){const auto at=(96*width+128)*4;CHECK(distance[at]<170 && distance[at+2]>distance[at]);}
    for(unsigned i=3;i<distance.size();i+=4)CHECK(distance[i]==111);
    neutral(draw(true,0,false,0),0); // Fully shrouded ground stays black.
    c.Viewport[0]=13;c.Viewport[1]=9;c.Viewport[2]=211;c.Viewport[3]=167;
    auto cropped=draw();CHECK(cropped.size()==width*height*4);
    for(unsigned y=0;y<height&&!cropped.empty();++y)for(unsigned x=0;x<width;++x)
        if(x<13||y<9||x>=224||y>=176)for(unsigned channel=0;channel<4;++channel)
            CHECK(std::abs(int(cropped[(y*width+x)*4+channel])-(channel==3?111:180))<=1);
    c.Viewport[0]=c.Viewport[1]=0;c.Viewport[2]=width;c.Viewport[3]=height;
    std::fill(depth.begin(),depth.end(),1.0f);
    auto horizon=draw(false,255,true);CHECK(horizon.size()==width*height*4);
    if(!horizon.empty()){CHECK(std::abs(int(horizon[0])-28)<=1);CHECK(std::abs(int(horizon[1])-56)<=1);CHECK(std::abs(int(horizon[2])-112)<=1);}
    c.WorldFromClip[10]=1100;
    auto openSky=draw(false,255,true);CHECK(openSky.size()==width*height*4);
    for(unsigned i=0;i<openSky.size();i+=4){CHECK(std::abs(int(openSky[i])-40)<=1);CHECK(std::abs(int(openSky[i+1])-80)<=1);CHECK(std::abs(int(openSky[i+2])-160)<=1);CHECK(openSky[i+3]==111);}
    c.WorldFromClip[10]=-1100;c.DepthInfo[2]=0.2f;c.DepthInfo[3]=1/0.6f;
    std::fill(depth.begin(),depth.end(),0.23f);neutral(draw()); // Non-default viewport depth range.
    CHECK(renderer.Prepare(17,11,SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,error));
    CHECK(!renderer.Prepare(0,11,SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,error));
    SDL_ReleaseGPUTexture(device,colour);SDL_ReleaseGPUTexture(device,shroud);SDL_ReleaseGPUTexture(device,sky);
    CHECK(SDL_WaitForGPUIdle(device));
    std::printf("ATMOSPHERE: failures=%d backend=%s\n",failures,SDL_GetGPUDeviceDriver(device));
}
static void testLighting(SDL_GPUDevice *device)
{
    static std::vector<unsigned char> programs[4];
    if(programs[0].empty()) {
        VertexPipelineDescription vertex={};vertex.FVF=FF_FVF_XYZ|FF_FVF_NORMAL|FF_FVF_TEX1;
        vertex.LightingEnabled=true;vertex.StageCount=1;
        vertex.DiffuseMaterialSource=vertex.AmbientMaterialSource=vertex.EmissiveMaterialSource=vertex.SpecularMaterialSource=FF_MCS_MATERIAL;
        CombinerDescription pixel={};pixel.StageCount=1;
        auto &stage=pixel.Stages[0];stage.TextureBound=true;
        stage.ColourOperation=FF_TOP_MODULATE;stage.ColourArgument1=FF_TA_TEXTURE;stage.ColourArgument2=FF_TA_DIFFUSE;
        stage.AlphaOperation=FF_TOP_SELECTARG1;stage.AlphaArgument1=FF_TA_DIFFUSE;
        for(unsigned i=0;i<4;++i) {
            const bool vs=i<2;std::string source,error,msl;
            vertex.HemiAmbient=i==1;pixel.TeamGlow=i==3;
            CHECK(vs?VertexShader_Generate(vertex,VERTEX_SHADER_TARGET_SDL3_GPU,source):
                CombinerShader_Generate(pixel,COMBINER_SHADER_TARGET_SDL3_GPU,source));
            if(!SDL3_Compile_HLSL_To_SPIRV(source,vs,programs[i],error)
                ||!SDL3_Translate_SPIRV_To_MSL(programs[i],vs,msl,error)) {
                CHECK(false);std::fprintf(stderr,"LIGHTING: %s\n",error.c_str());return;
            }
        }
        std::puts("LIGHTING: hemisphere and team-glow variants compiled to SPIR-V and MSL");
    }
    if(!device)return;
    std::string error;SDL_GPUShader *shaders[4]={};SDL_GPUGraphicsPipeline *pipelines[3]={};
    for(unsigned i=0;i<4;++i){shaders[i]=SDL3_Create_Shader(device,programs[i],i<2,error);CHECK(shaders[i]);}
    if(!shaders[0]||!shaders[1]||!shaders[2]||!shaders[3]) {
        std::fprintf(stderr,"LIGHTING: %s\n",error.c_str());for(auto *s:shaders)if(s)SDL_ReleaseGPUShader(device,s);return;
    }
    struct Vertex {float x,y,z,nx,ny,nz,u,v;};
    SDL_GPUVertexBufferDescription bufferDescription={};bufferDescription.pitch=sizeof(Vertex);
    bufferDescription.input_rate=SDL_GPU_VERTEXINPUTRATE_VERTEX;
    SDL_GPUVertexAttribute attributes[3]={};
    attributes[0].location=0;attributes[0].format=SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
    attributes[1].location=1;attributes[1].format=SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;attributes[1].offset=3*sizeof(float);
    attributes[2].location=4;attributes[2].format=SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;attributes[2].offset=6*sizeof(float);
    SDL_GPUColorTargetDescription targetDescription={};targetDescription.format=SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    for(unsigned i=0;i<3;++i) {
        SDL_GPUGraphicsPipelineCreateInfo info={};info.vertex_shader=shaders[i==1?1:0];info.fragment_shader=shaders[i==2?3:2];
        info.vertex_input_state.vertex_buffer_descriptions=&bufferDescription;info.vertex_input_state.num_vertex_buffers=1;
        info.vertex_input_state.vertex_attributes=attributes;info.vertex_input_state.num_vertex_attributes=3;
        info.primitive_type=SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;info.rasterizer_state.fill_mode=SDL_GPU_FILLMODE_FILL;
        info.rasterizer_state.cull_mode=SDL_GPU_CULLMODE_NONE;
        info.target_info.color_target_descriptions=&targetDescription;info.target_info.num_color_targets=1;
        pipelines[i]=SDL_CreateGPUGraphicsPipeline(device,&info);CHECK(pipelines[i]);
    }
    SDL_GPUBufferCreateInfo bufferInfo={};bufferInfo.usage=SDL_GPU_BUFFERUSAGE_VERTEX;bufferInfo.size=3*sizeof(Vertex);
    auto *buffer=SDL_CreateGPUBuffer(device,&bufferInfo);CHECK(buffer);
    SDL_GPUTextureCreateInfo textureInfo={};textureInfo.type=SDL_GPU_TEXTURETYPE_2D;textureInfo.format=SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    textureInfo.usage=SDL_GPU_TEXTUREUSAGE_SAMPLER|SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
    textureInfo.width=textureInfo.height=1;textureInfo.layer_count_or_depth=textureInfo.num_levels=1;
    auto *texture=SDL_CreateGPUTexture(device,&textureInfo);textureInfo.width=32;textureInfo.height=24;
    auto *colour=SDL_CreateGPUTexture(device,&textureInfo);CHECK(texture&&colour);
    SDL_GPUSamplerCreateInfo samplerInfo={};auto *sampler=SDL_CreateGPUSampler(device,&samplerInfo);CHECK(sampler);
    if(buffer&&texture&&colour&&sampler&&pipelines[0]&&pipelines[1]&&pipelines[2]) {
        SdlVertexConstants constants={};for(unsigned i=0;i<4;++i) {
            constants.WorldViewProjection[i*5]=constants.WorldView[i*5]=constants.NormalTransform[i*5]=1;
            for(unsigned j=0;j<MAXIMUM_VERTEX_STAGES;++j)constants.TextureMatrix[j][i*5]=1;
            constants.MaterialAmbient[i]=constants.MaterialDiffuse[i]=1;
        }
        constants.MaterialDiffuse[3]=111/255.0f;
        constants.GlobalAmbient[0]=constants.GlobalAmbient[1]=constants.GlobalAmbient[2]=0.4f;
        constants.MaterialPower[3]=1; // World up, in view space.
        auto draw=[&](unsigned pipeline,float normal,unsigned green=255) {
            return renderPass(device,nullptr,colour,{},32,24,[&](SDL_GPUCommandBuffer *commands) {
                Vertex vertices[3]={{-1,-1,0.5f,0,0,normal,0.5f,0.5f},{3,-1,0.5f,0,0,normal,0.5f,0.5f},{-1,3,0.5f,0,0,normal,0.5f,0.5f}};
                SDL_GPUTransferBufferCreateInfo info={};info.usage=SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;info.size=sizeof(vertices);
                auto *transfer=SDL_CreateGPUTransferBuffer(device,&info);if(!transfer)return false;
                auto *mapped=SDL_MapGPUTransferBuffer(device,transfer,false);
                if(!mapped){SDL_ReleaseGPUTransferBuffer(device,transfer);return false;}
                std::memcpy(mapped,vertices,sizeof(vertices));SDL_UnmapGPUTransferBuffer(device,transfer);
                auto *copy=SDL_BeginGPUCopyPass(commands);if(!copy){SDL_ReleaseGPUTransferBuffer(device,transfer);return false;}
                SDL_GPUTransferBufferLocation source={};source.transfer_buffer=transfer;
                SDL_GPUBufferRegion destination={};destination.buffer=buffer;destination.size=sizeof(vertices);
                SDL_UploadToGPUBuffer(copy,&source,&destination,false);SDL_EndGPUCopyPass(copy);SDL_ReleaseGPUTransferBuffer(device,transfer);
                SDL_GPUColorTargetInfo target={};target.texture=colour;target.load_op=SDL_GPU_LOADOP_LOAD;target.store_op=SDL_GPU_STOREOP_STORE;
                auto *pass=SDL_BeginGPURenderPass(commands,&target,1,nullptr);if(!pass)return false;
                SDL_BindGPUGraphicsPipeline(pass,pipelines[pipeline]);SDL_GPUBufferBinding binding={};binding.buffer=buffer;
                SDL_BindGPUVertexBuffers(pass,0,&binding,1);SDL_GPUTextureSamplerBinding sampled={texture,sampler};
                SDL_BindGPUFragmentSamplers(pass,0,&sampled,1);SDL_PushGPUVertexUniformData(commands,0,&constants,sizeof(constants));
                const SDL_GPUViewport viewport={0,0,32,24,0,1};SDL_SetGPUViewport(pass,&viewport);
                SDL_DrawGPUPrimitives(pass,3,1,0,0);SDL_EndGPURenderPass(pass);return true;
            },texture,green);
        };
        auto flat=draw(0,1),up=draw(1,1),down=draw(1,-1),teamOff=draw(0,1,100),teamOn=draw(2,1,100);
        for(const auto *pixels:{&flat,&up,&down,&teamOff,&teamOn}) {
            CHECK(pixels->size()==32*24*4);for(unsigned i=3;i<pixels->size();i+=4)CHECK((*pixels)[i]==111);
        }
        const unsigned at=(12*32+16)*4;
        if(flat.size()>at&&up.size()>at&&down.size()>at)CHECK(up[at]>flat[at]+5&&flat[at]>down[at]+5);
        if(teamOn.size()>at&&teamOff.size()>at)CHECK(teamOn[at]>teamOff[at]+10&&teamOn[at+2]>teamOff[at+2]+10);
    }
    if(buffer)SDL_ReleaseGPUBuffer(device,buffer);if(texture)SDL_ReleaseGPUTexture(device,texture);
    if(colour)SDL_ReleaseGPUTexture(device,colour);if(sampler)SDL_ReleaseGPUSampler(device,sampler);
    for(auto *p:pipelines)if(p)SDL_ReleaseGPUGraphicsPipeline(device,p);for(auto *s:shaders)SDL_ReleaseGPUShader(device,s);
    CHECK(SDL_WaitForGPUIdle(device));std::printf("LIGHTING: failures=%d backend=%s\n",failures,SDL_GetGPUDeviceDriver(device));
}
int main()
{
    for(unsigned stage=0;stage<4;++stage) {
        std::vector<unsigned char> spirv;std::string log,msl;
        const bool vertex=stage==0;
        const auto source=vertex?std::string(SdlAo::VertexShader()):SdlAo::PixelShader(stage-1);
        const bool compiled=SDL3_Compile_HLSL_To_SPIRV(source,vertex,spirv,log);
        CHECK(compiled);if(!compiled){std::fprintf(stderr,"%s\n",log.c_str());return 1;}
        CHECK(SDL3_Translate_SPIRV_To_MSL(spirv,vertex,msl,log));
        if(msl.empty())std::fprintf(stderr,"%s\n",log.c_str());
        unsigned samplers,uniforms;SDL3_Shader_Slots(spirv,vertex,samplers,uniforms);
        CHECK(vertex?(samplers==0&&uniforms==0):(samplers<=2&&uniforms==1));
    }
    if(failures)return 1;
    std::puts("AO: four production shader stages compiled to SPIR-V and MSL");
    for(bool vertex:{true,false}) {
        std::vector<unsigned char> spirv;std::string log,msl;
        const bool compiled=SDL3_Compile_HLSL_To_SPIRV(vertex?SdlAtmosphere::VertexShader():SdlAtmosphere::PixelShader(),vertex,spirv,log);
        CHECK(compiled);if(!compiled){std::fprintf(stderr,"%s\n",log.c_str());return 1;}
        CHECK(SDL3_Translate_SPIRV_To_MSL(spirv,vertex,msl,log));
        unsigned slots,uniforms;SDL3_Shader_Slots(spirv,vertex,slots,uniforms);
        CHECK(vertex?(slots==0&&uniforms==0):(slots==3&&uniforms==1));
    }
    if(failures)return 1;
    std::puts("ATMOSPHERE: production shaders compiled to SPIR-V and MSL");
    testLighting(nullptr);if(failures)return 1;
#if defined(__APPLE__)
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER,"dummy");SDL_SetHint("ZH_SDL_GPU_METAL_WINDOWLESS","1");
#else
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER,"offscreen");
#endif
    if(!SDL_Init(SDL_INIT_VIDEO)){std::printf("SKIP GPU: %s\n",SDL_GetError());return 77;}
    auto *device=SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV|SDL_GPU_SHADERFORMAT_MSL,false,nullptr);
    if(!device){std::printf("SKIP GPU: %s\n",SDL_GetError());SDL_Quit();return 77;}
    {
        SdlAo::Renderer renderer(device);
        const unsigned width=257,height=193;
        std::string error;
        CHECK(renderer.Prepare(width,height,SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,error));
        if(!renderer.Depth()){std::fprintf(stderr,"%s\n",error.c_str());return 1;}
        SDL_GPUTextureCreateInfo info={};info.type=SDL_GPU_TEXTURETYPE_2D;info.format=SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
        info.usage=SDL_GPU_TEXTUREUSAGE_COLOR_TARGET|SDL_GPU_TEXTUREUSAGE_SAMPLER;
        info.width=width;info.height=height;info.layer_count_or_depth=info.num_levels=1;
        auto *colour=SDL_CreateGPUTexture(device,&info);CHECK(colour);if(!colour)return 1;
        SdlAo::Constants constants={};
        // Inverse RH D3D projection: near=1, far=100, projection scale=2.
        constants.WorldFromClip[0]=0.5f;constants.WorldFromClip[5]=0.5f;
        constants.WorldFromClip[11]=-0.99f;constants.WorldFromClip[14]=-1;constants.WorldFromClip[15]=1;
        constants.Viewport[2]=width;constants.Viewport[3]=height;
        constants.Projection[0]=2;constants.Projection[1]=2;constants.Projection[3]=1;
        const float ground=(1.0f-1.0f/80)/0.99f,roof=(1.0f-1.0f/70)/0.99f;
        std::vector<float> depth(width*height,ground);
        auto flat=render(device,renderer,colour,depth,width,height,constants);
        CHECK(flat.size()==width*height*4);
        for(unsigned i=0;i<flat.size();++i)CHECK(std::abs(int(flat[i])-(i%4==3?111:180))<=1);
        for(unsigned y=72;y<128;++y)for(unsigned x=96;x<160;++x)depth[y*width+x]=roof;
        for(unsigned x=0;x<width;++x)depth[x]=1.0f;
        auto raised=render(device,renderer,colour,depth,width,height,constants);
        CHECK(raised.size()==width*height*4);
        unsigned contact=0;
        for(unsigned y=0;y<height && !raised.empty();++y)for(unsigned x=0;x<width;++x) {
            const auto at=(y*width+x)*4;
            CHECK(raised[at]<=181 && raised[at+1]<=181 && raised[at+2]<=181);
            CHECK(raised[at+3]==111);
            if(depth[y*width+x]==ground && raised[at]<175)++contact;
            if(y==0 || x==0 || x==width-1)CHECK(std::abs(int(raised[at])-180)<=1);
        }
        CHECK(contact>4);
        if(!raised.empty())CHECK(raised[(96*width+128)*4]>=179); // Flat centre of raised roof stays lit.
        info.width=info.height=1;
        auto *shroud=SDL_CreateGPUTexture(device,&info);CHECK(shroud);
        if(shroud) {
            constants.Shroud[0]=1;constants.ShroudRect[2]=constants.ShroudRect[3]=1;
            auto hidden=render(device,renderer,colour,depth,width,height,constants,shroud,0);
            CHECK(hidden.size()==width*height*4);
            for(unsigned i=0;i<hidden.size();++i)CHECK(std::abs(int(hidden[i])-(i%4==3?111:180))<=1);
            SDL_ReleaseGPUTexture(device,shroud);
        }
        CHECK(renderer.Prepare(32,24,SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,error));
        CHECK(!renderer.Prepare(0,24,SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,error));
        SDL_ReleaseGPUTexture(device,colour);
        CHECK(SDL_WaitForGPUIdle(device));
        std::printf("AO: contact pixels=%u failures=%d backend=%s\n",contact,failures,SDL_GetGPUDeviceDriver(device));
    }
    testAtmosphere(device);
    testLighting(device);
    SDL_DestroyGPUDevice(device);SDL_Quit();return failures?1:0;
}
