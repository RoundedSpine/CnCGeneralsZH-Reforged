#include "SdlGpuFrame.h"
#include "SdlAmbientOcclusion.h"
#include "Platform/IOSLifecycle.h"
#include <cstdio>
#include <new>

void SdlGpuFrame::Release_Ambient_Occlusion() { delete Ao; Ao=nullptr; }

bool SdlGpuFrame::Ambient_Occlusion(const SdlTarget &target,const SdlAo::Constants &constants,SDL_GPUTexture *shroud)
{
    if(AoFailed) return false;
    std::string error;
    const auto fail=[&](const char *reason) {
        AoFailed=true;
        fprintf(stderr,"[SDL_AO] disabled reason=%s\n",reason);
        return false;
    };
    if(!Supports_Scene_Depth()) return fail("sampleable scene depth unsupported");
    if(!target.Colour||!target.Depth||target.Colour!=BackBuffer) return false;
    if(!Flush()||!Create_Scene_Depth_Pipeline()) return fail("scene flush/depth resolve unavailable");
    if(!Ao) Ao=new (std::nothrow) SdlAo::Renderer(GpuDevice);
    if(!Ao) return fail("AO resource owner allocation failed");
    if(!Ao->Prepare(target.Width,target.Height,Target_Format(),error)) return fail(error.c_str());
    ZH_IOS_WaitForForeground();
    SDL_GPUCommandBuffer *commands=SDL_AcquireGPUCommandBuffer(GpuDevice);
    if(!commands) return fail(SDL_GetError());
    if(!Resolve_Scene_Depth(commands,target.Depth,Ao->Depth(),target.Width,target.Height)
        ||!Ao->Render(commands,target.Colour,constants,shroud)) {
        SDL_CancelGPUCommandBuffer(commands);return fail("could not record AO passes");
    }
    if(!(SerializeSubmits?Submit(commands):Submit_Flush(commands))) return fail("AO submission failed");
    Invalidate_Water_Snapshots(target.Colour,nullptr);
    return true;
}
