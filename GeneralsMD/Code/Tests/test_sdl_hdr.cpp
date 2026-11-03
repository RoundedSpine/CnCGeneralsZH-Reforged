#include "SdlGpuFrame.h"
#include "SdlPostShader.h"
#include "SdlColour.h"
#include "sdl3shadercompile.h"
#include <SDL3/SDL.h>
#include <cstdio>
#include <memory>
static unsigned failures=0;
#define CHECK(x) do{if(!(x)){++failures;fprintf(stderr,"FAIL HDR line %d: %s\n",__LINE__,#x);}}while(0)
int main(){
    for(unsigned b=0;b<256;++b)CHECK(SdlColour::byte(SdlColour::real(SdlColour::half(b/255.0f)))==b);
    for(float f:{0.0f,0.125f,0.8f,1.0f,2.0f,4.0f,65504.0f})CHECK(std::abs(SdlColour::real(SdlColour::half(f))-f)<0.001f);
    std::string error,msl;std::vector<unsigned char> spirv;
    CHECK(SDL3_Compile_HLSL_To_SPIRV(SdlPost::Pixel(),false,spirv,error));CHECK(SDL3_Translate_SPIRV_To_MSL(spirv,false,msl,error));
    if(failures){fprintf(stderr,"%s\n",error.c_str());return 1;}
#if defined(__APPLE__)
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER,"dummy");SDL_SetHint("ZH_SDL_GPU_METAL_WINDOWLESS","1");
#else
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER,"offscreen");
#endif
    if(!SDL_Init(SDL_INIT_VIDEO)){printf("SKIP HDR GPU: %s\n",SDL_GetError());return 77;}
    std::unique_ptr<SdlGpuFrame> frame(SdlGpuFrame::Create(nullptr,17,11,error,true));
    if(!frame){printf("SKIP HDR GPU: %s\n",error.c_str());SDL_Quit();return 77;}
    CHECK(frame->Hdr_Active());auto target=frame->Back_Buffer_Target();
    auto clear=[&](){auto *cmd=SDL_AcquireGPUCommandBuffer(frame->Device());CHECK(cmd);
        SDL_GPUColorTargetInfo colour={};colour.texture=target.Colour;colour.load_op=SDL_GPU_LOADOP_CLEAR;colour.store_op=SDL_GPU_STOREOP_STORE;
        colour.clear_color={4,2,0.5f,0.6f};auto *pass=SDL_BeginGPURenderPass(cmd,&colour,1,nullptr);CHECK(pass);
        if(pass)SDL_EndGPURenderPass(pass);CHECK(SDL_SubmitGPUCommandBuffer(cmd));};
    clear();std::vector<float> raw;CHECK(frame->Read_Float_Colour(target.Colour,17,11,raw));
    if(!raw.empty()){CHECK(raw[0]>3.99f&&raw[1]>1.99f);CHECK(std::abs(raw[3]-0.6f)<0.001f);}
    const float rectangle[4]={0,0,17,11};CHECK(frame->Post_Process(target,rectangle,0));
    CHECK(frame->Read_Float_Colour(target.Colour,17,11,raw));
    if(!raw.empty()){CHECK(raw[0]>0.98f&&raw[0]<=1&&raw[0]>raw[1]&&raw[1]>raw[2]);CHECK(std::abs(raw[3]-0.6f)<0.001f);}
    // UI drawn after scene tone mapping retains its white, without a second tone pass.
    frame->Clear_Rect(0,0,2,2,true,false,false,0xffffffff,1,0);
    std::vector<uint8_t> bgra;CHECK(frame->Read_Back(target.Colour,17,11,bgra));
    if(!bgra.empty())CHECK(bgra[0]==255&&bgra[1]==255&&bgra[2]==255&&bgra[3]==255);
    std::vector<uint8_t> pattern(17*11*4,90);for(unsigned i=3;i<pattern.size();i+=4)pattern[i]=255;
    CHECK(frame->Upload_Back_Buffer(pattern));CHECK(frame->Read_Back(target.Colour,17,11,bgra));CHECK(pattern==bgra);
    CHECK(frame->Trim_Transient_Memory());CHECK(frame->Resize(31,19));target=frame->Back_Buffer_Target();clear();
    const float cropped[4]={3,2,20,12};CHECK(frame->Post_Process(target,cropped,1));
    CHECK(frame->Read_Float_Colour(target.Colour,31,19,raw));
    if(!raw.empty()){CHECK(raw[0]>3.99f);CHECK(raw[(4*31+5)*4]<=1);}
    printf("HDR: failures=%u backend=%s\n",failures,SDL_GetGPUDeviceDriver(frame->Device()));
    frame.reset();SDL_Quit();return failures?1:0;
}
