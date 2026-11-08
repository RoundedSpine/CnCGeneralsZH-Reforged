// Exercise the actual frame capture with queued scene writes and GPU readback.
#include "SdlGpuFrame.h"
#include <SDL3/SDL.h>
#include <cstdio>
#include <memory>
#include <vector>
static unsigned failures=0;
#define CHECK(x) do{if(!(x)){++failures;std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);}}while(0)
int main(){
#if defined(__APPLE__)
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER,"dummy");SDL_SetHint("ZH_SDL_GPU_METAL_WINDOWLESS","1");
#else
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER,"offscreen");
#endif
    if(!SDL_Init(SDL_INIT_VIDEO)){std::printf("SKIP HEAT GPU: %s\n",SDL_GetError());return 77;}
    std::string error;std::unique_ptr<SdlGpuFrame> frame(SdlGpuFrame::Create(nullptr,257,193,error));
    if(!frame){std::printf("SKIP HEAT GPU: %s\n",error.c_str());SDL_Quit();return 77;}
    auto verify=[&](SDL_GPUTexture *texture,unsigned w,unsigned h,unsigned b,unsigned g,unsigned r){
        std::vector<uint8_t> pixels;CHECK(frame->Read_Back(texture,w,h,pixels));CHECK(pixels.size()==w*h*4);
        for(unsigned i=0;i+3<pixels.size();i+=4){if(pixels[i]!=b||pixels[i+1]!=g||pixels[i+2]!=r||pixels[i+3]!=255){CHECK(false);break;}}
    };
    CHECK(frame->Supports_Heat_Haze());
    frame->Clear_Back_Buffer(true,true,false,0xff123456,1,0);
    auto target=frame->Back_Buffer_Target();auto *snapshot=frame->Capture_Heat_Background(target,error);
    CHECK(snapshot&&snapshot!=target.Colour);
    if(snapshot){verify(snapshot,257,193,0x56,0x34,0x12);
        frame->Clear_Back_Buffer(true,false,false,0xffabcdef,1,0);
        verify(snapshot,257,193,0x56,0x34,0x12); // Immutable while the target changes.
        verify(target.Colour,257,193,0xef,0xcd,0xab);
        auto *next=frame->Capture_Heat_Background(target,error);CHECK(next==snapshot);
        verify(next,257,193,0xef,0xcd,0xab); // Refresh on the next smudge pass.
    }
    CHECK(frame->Resize(17,11));frame->Clear_Back_Buffer(true,true,false,0xff654321,1,0);
    auto resized=frame->Back_Buffer_Target();snapshot=frame->Capture_Heat_Background(resized,error);
    CHECK(snapshot&&snapshot!=resized.Colour);if(snapshot)verify(snapshot,17,11,0x21,0x43,0x65);
    CHECK(frame->Trim_Transient_Memory());
    snapshot=frame->Capture_Heat_Background(resized,error);CHECK(snapshot);
    if(snapshot)verify(snapshot,17,11,0x21,0x43,0x65);
    CHECK(!frame->Capture_Heat_Background({nullptr,nullptr,17,11},error));
    // The HeatHaze option's fire/laser/exhaust packet must reach an actual native composite,
    // not only the scene-copy helper used by legacy smudge distortion.
    CHECK(frame->Resize(64,48));frame->Clear_Back_Buffer(true,true,false,0xff000000,1,0);
    std::vector<uint8_t> gradient(64*48*4);
    for(unsigned y=0;y<48;++y)for(unsigned x=0;x<64;++x){const size_t at=(y*64+x)*4;gradient[at]=uint8_t(x*4);gradient[at+1]=uint8_t(y*5);gradient[at+2]=uint8_t((x+y)*2);gradient[at+3]=255;}
    CHECK(frame->Upload_Back_Buffer(gradient));auto heatTarget=frame->Back_Buffer_Target();
    std::vector<uint8_t> before;CHECK(frame->Read_Back(heatTarget.Colour,64,48,before));
    const float splat[16]={0,0,0.85f,0.85f,0.25f,0.035f,0.37f,0,1,1000,0,1,0,0,0,0};
    frame->Set_Heat_Splats(splat,1);CHECK(frame->Heat_Haze_Scene(heatTarget));
    std::vector<uint8_t> after;CHECK(frame->Read_Back(heatTarget.Colour,64,48,after));CHECK(before!=after);
    frame->Set_Heat_Splats(nullptr,0);CHECK(frame->Heat_Haze_Scene(heatTarget));
    CHECK(frame->Supports_Heat_Haze());CHECK(SDL_WaitForGPUIdle(frame->Device()));
    // Resize invalidates SdlTarget texture pointers. Restore the original target before
    // the existing memory-pressure checks below continue to use its 17x11 expectations.
    CHECK(frame->Resize(17,11));frame->Clear_Back_Buffer(true,true,false,0xff654321,1,0);
    resized=frame->Back_Buffer_Target();
    // Unused staging capacity survives ordinary batch completion but is disposable.
    uint32_t stagedOffset=0;CHECK(frame->Stage(8*1024*1024,stagedOffset));
    CHECK(frame->Flush());CHECK(frame->Retained_Staging_Bytes()>=8*1024*1024);
    CHECK(frame->Handle_Memory_Pressure());CHECK(frame->Memory_Constrained());
    CHECK(frame->Retained_Staging_Bytes()==0);
    CHECK(!frame->Supports_Ambient_Occlusion()&&!frame->Supports_Atmosphere()&&!frame->Supports_Firelight());
    CHECK(!frame->Prepare_Ocean(1));
    // Required scene copies can return once, but a warning burst must retain them.
    snapshot=frame->Capture_Heat_Background(resized,error);CHECK(snapshot);
    frame->Clear_Back_Buffer(true,false,false,0xffabcdef,1,0);
    CHECK(frame->Handle_Memory_Pressure());
    if(snapshot)verify(snapshot,17,11,0x21,0x43,0x65);
    verify(resized.Colour,17,11,0xef,0xcd,0xab);
    std::printf("HEAT: failures=%u backend=%s\n",failures,SDL_GetGPUDeviceDriver(frame->Device()));
    frame.reset();SDL_Quit();return failures?1:0;
}
