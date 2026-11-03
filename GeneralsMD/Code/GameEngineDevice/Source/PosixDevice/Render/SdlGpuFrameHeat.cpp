// GPL-3.0; see LICENSE.md. Heat samples an immutable copy, never its active target.
#include "SdlGpuFrame.h"
#include "Platform/IOSLifecycle.h"
#include <SDL3/SDL.h>
#include <cstdio>
void SdlGpuFrame::Release_Heat_Background()
{
    if(HeatBackground)Release_Texture(HeatBackground);
    HeatBackground=nullptr;HeatWidth=HeatHeight=0;
}
SDL_GPUTexture *SdlGpuFrame::Capture_Heat_Background(const SdlTarget &target,std::string &error)
{
    if(!Supports_Heat_Haze()||!target.Colour||!target.Width||!target.Height)return nullptr;
    const auto fail=[&](const char *reason)->SDL_GPUTexture* {
        error=reason;HeatFailed=true;fprintf(stderr,"[SDL_HEAT] disabled reason=%s\n",reason);return nullptr;
    };
    if(HeatBackground==target.Colour)return fail("capture source aliases destination");
    if(!Flush())return fail("scene flush failed");
    if(HeatWidth!=target.Width||HeatHeight!=target.Height) {
        Release_Heat_Background();SDL_GPUTextureCreateInfo info={};info.type=SDL_GPU_TEXTURETYPE_2D;
        info.format=(SDL_GPUTextureFormat)Scene_Format();info.usage=SDL_GPU_TEXTUREUSAGE_SAMPLER|SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
        info.width=target.Width;info.height=target.Height;info.layer_count_or_depth=info.num_levels=1;
        HeatBackground=SDL_CreateGPUTexture(GpuDevice,&info);Track_Colour(HeatBackground);if(!HeatBackground)return fail(SDL_GetError());
        HeatWidth=target.Width;HeatHeight=target.Height;
        fprintf(stderr,"[SDL_HEAT] ready size=%ux%u\n",HeatWidth,HeatHeight);
    }
    ZH_IOS_WaitForForeground();auto *commands=SDL_AcquireGPUCommandBuffer(GpuDevice);
    if(!commands)return fail(SDL_GetError());
    auto *copy=SDL_BeginGPUCopyPass(commands);
    if(!copy){SDL_CancelGPUCommandBuffer(commands);return fail(SDL_GetError());}
    SDL_GPUTextureLocation source={},destination={};source.texture=target.Colour;destination.texture=HeatBackground;
    SDL_CopyGPUTextureToTexture(copy,&source,&destination,HeatWidth,HeatHeight,1,false);SDL_EndGPUCopyPass(copy);
    if(!(SerializeSubmits?Submit(commands):Submit_Flush(commands)))return fail("capture submission failed");
    return HeatBackground;
}
