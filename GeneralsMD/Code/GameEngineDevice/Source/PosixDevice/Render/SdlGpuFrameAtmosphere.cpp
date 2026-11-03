// Distributed under the repository's GPL-3.0 terms; see LICENSE.md.
#include "SdlGpuFrame.h"
#include "SdlAtmosphere.h"
#include "Platform/IOSLifecycle.h"
#include <cstdio>
#include <new>
void SdlGpuFrame::Release_Atmosphere() {delete AtmosphereRenderer;AtmosphereRenderer=nullptr;}
bool SdlGpuFrame::Supports_Atmosphere() const {return GpuDevice&&!Memory_Constrained()&&!AtmosphereFailed&&Supports_Scene_Depth();}
bool SdlGpuFrame::Supports_Firelight() const {return GpuDevice&&!Memory_Constrained()&&!FirelightFailed&&Supports_Scene_Depth();}
bool SdlGpuFrame::Firelight(const SdlTarget &target,const SdlFirelight::Constants &constants)
{
    if(Memory_Constrained()||FirelightFailed||!target.Colour||!target.Depth)return false;
    const auto fail=[&](const char *why){FirelightFailed=true;fprintf(stderr,"[SDL_FIRELIGHT] disabled reason=%s\n",why);return false;};
    if(!Supports_Scene_Depth()||!Flush()||!Create_Scene_Depth_Pipeline())return fail("scene flush/depth resolve unavailable");
    if(!AtmosphereRenderer)AtmosphereRenderer=new(std::nothrow) SdlAtmosphere::Renderer(GpuDevice);
    if(!AtmosphereRenderer)return fail("resource owner allocation failed");
    std::string error;
    if(!AtmosphereRenderer->PrepareFirelight(target.Width,target.Height,Scene_Format(),error))return fail(error.c_str());
    ZH_IOS_WaitForForeground();
    auto *commands=SDL_AcquireGPUCommandBuffer(GpuDevice);if(!commands)return fail(SDL_GetError());
    if(!Resolve_Scene_Depth(commands,target.Depth,AtmosphereRenderer->Depth(),target.Width,target.Height)
        ||!AtmosphereRenderer->RenderFirelight(commands,target.Colour,constants)) {
        SDL_CancelGPUCommandBuffer(commands);return fail("could not record firelight pass");
    }
    if(!(SerializeSubmits?Submit(commands):Submit_Flush(commands)))return fail("submission failed");
    External_Colour_Write(target.Colour);Invalidate_Water_Snapshots(target.Colour,nullptr);return true;
}
bool SdlGpuFrame::Atmosphere(const SdlTarget &target,const SdlAtmosphere::Constants &constants,
    SDL_GPUTexture *sky,SDL_GPUTexture *shroud)
{
    if(Memory_Constrained()||AtmosphereFailed||!target.Colour||!target.Depth)return false;
    const auto fail=[&](const char *why) {AtmosphereFailed=true;fprintf(stderr,"[SDL_ATMOSPHERE] disabled reason=%s\n",why);return false;};
    if(!Supports_Scene_Depth())return fail("sampleable scene depth unavailable");
    if(!Flush()||!Create_Scene_Depth_Pipeline())return fail("scene flush/depth resolve unavailable");
    if(!AtmosphereRenderer)AtmosphereRenderer=new(std::nothrow) SdlAtmosphere::Renderer(GpuDevice);
    if(!AtmosphereRenderer)return fail("resource owner allocation failed");
    std::string error;
    if(!AtmosphereRenderer->Prepare(target.Width,target.Height,Scene_Format(),error))return fail(error.c_str());
    ZH_IOS_WaitForForeground();
    auto *commands=SDL_AcquireGPUCommandBuffer(GpuDevice);if(!commands)return fail(SDL_GetError());
    if(!Resolve_Scene_Depth(commands,target.Depth,AtmosphereRenderer->Depth(),target.Width,target.Height)
        ||!AtmosphereRenderer->Render(commands,target.Colour,constants,sky,shroud)) {
        SDL_CancelGPUCommandBuffer(commands);return fail("could not record atmosphere pass");
    }
    if(!(SerializeSubmits?Submit(commands):Submit_Flush(commands)))return fail("submission failed");
    External_Colour_Write(target.Colour);Invalidate_Water_Snapshots(target.Colour,nullptr);return true;
}
