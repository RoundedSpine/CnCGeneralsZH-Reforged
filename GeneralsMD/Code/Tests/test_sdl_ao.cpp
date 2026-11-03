// Compile the production shaders through SPIR-V and MSL, then render/read back
// flat ground and a raised object. GPU checks skip explicitly if none is available.
#include "SdlAmbientOcclusion.h"
#include "sdl3shadercompile.h"
#include <cstdio>
#include <cstring>
#include <vector>
#include <string>
#include <cmath>

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
static std::vector<unsigned char> render(SDL_GPUDevice *device,SdlAo::Renderer &renderer,
    SDL_GPUTexture *colour,const std::vector<float> &depth,unsigned width,unsigned height,SdlAo::Constants constants,SDL_GPUTexture *shroud=nullptr,unsigned green=0)
{
    auto *commands=SDL_AcquireGPUCommandBuffer(device);CHECK(commands);if(!commands)return {};
    std::vector<unsigned char> pixels(width*height*4,180);
    for(unsigned i=3;i<pixels.size();i+=4)pixels[i]=111;
    const unsigned char shroudPixel[4]={255,(unsigned char)green,255,255};
    if(shroud && !upload(device,commands,shroud,1,1,shroudPixel)) {SDL_CancelGPUCommandBuffer(commands);CHECK(false);return {};}
    if(!upload(device,commands,colour,width,height,pixels.data())
        ||!upload(device,commands,renderer.Depth(),width,height,depth.data())
        ||!renderer.Render(commands,colour,constants,shroud)) {SDL_CancelGPUCommandBuffer(commands);CHECK(false);return {};}
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
    SDL_DestroyGPUDevice(device);SDL_Quit();return failures?1:0;
}
