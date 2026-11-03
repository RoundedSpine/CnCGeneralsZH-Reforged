// GPL-3.0; see LICENSE.md. World-only FXAA/sharpen integration and pixel checks.
#include "SdlGpuFrame.h"
#include "SdlPostShader.h"
#include "sdl3shadercompile.h"
#include <SDL3/SDL.h>
#include <cstdio>
#include <memory>
static unsigned failures=0;
#define CHECK(x) do{if(!(x)){++failures;fprintf(stderr,"FAIL POST line %d: %s\n",__LINE__,#x);}}while(0)
int main(){
    ScenePost::Chain chain;CHECK(ScenePost::Parse("bloom,ao,fxaa,sharpen",chain));CHECK(chain.Count==4);
    CHECK(ScenePost::Active(ScenePost::FXAA,1,false));CHECK(!ScenePost::Active(ScenePost::FXAA,4,false));
    CHECK(!ScenePost::Active(ScenePost::Sharpen,1,true));
    CHECK(!ScenePost::Parse("fxaa,bloom",chain));CHECK(chain.Count==0);
    CHECK(!ScenePost::Parse("fxaa,unknown",chain));CHECK(chain.Count==0);
    CHECK(ScenePost::Parse("off",chain)&&chain.Count==0);
    CHECK(ScenePost::Parse("sharpen,fxaa,sharpen",chain)&&chain.Count==3);
    std::vector<unsigned char> spv;std::string error,msl;
    CHECK(SDL3_Compile_HLSL_To_SPIRV(SdlPost::Pixel(),false,spv,error));
    CHECK(SDL3_Translate_SPIRV_To_MSL(spv,false,msl,error));
    if(failures){fprintf(stderr,"%s\n",error.c_str());return 1;}
#if defined(__APPLE__)
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER,"dummy");SDL_SetHint("ZH_SDL_GPU_METAL_WINDOWLESS","1");
#else
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER,"offscreen");
#endif
    if(!SDL_Init(SDL_INIT_VIDEO)){printf("SKIP POST GPU: %s\n",SDL_GetError());return 77;}
    std::unique_ptr<SdlGpuFrame> frame(SdlGpuFrame::Create(nullptr,33,23,error));
    if(!frame){printf("SKIP POST GPU: %s\n",error.c_str());SDL_Quit();return 77;}
    auto target=frame->Back_Buffer_Target();const float rect[4]={0,0,33,23};
    std::vector<uint8_t> pixels(33*23*4,96),out;
    for(unsigned i=3;i<pixels.size();i+=4)pixels[i]=67;
    ScenePost::Parse("fxaa,sharpen",chain);
    CHECK(frame->Upload_Back_Buffer(pixels));CHECK(frame->Post_Process(target,rect,0,&chain));
    CHECK(frame->Read_Back(target.Colour,33,23,out));CHECK(out==pixels); // Flat fields, including alpha.
    for(unsigned y=0;y<23;++y)for(unsigned x=0;x<33;++x){
        const unsigned at=(y*33+x)*4;const uint8_t colour=x*2>y*3?240:10;
        pixels[at]=pixels[at+1]=pixels[at+2]=colour;pixels[at+3]=uint8_t(30+(x*7+y)%180);
    }
    ScenePost::Parse("fxaa",chain);CHECK(frame->Upload_Back_Buffer(pixels));CHECK(frame->Post_Process(target,rect,0,&chain));
    CHECK(frame->Read_Back(target.Colour,33,23,out));unsigned changed=0;
    if(out.size()==pixels.size())for(unsigned i=0;i<out.size();i+=4){
        changed+=out[i]!=pixels[i];CHECK(out[i+3]==pixels[i+3]);
    }
    CHECK(changed>0);
    ScenePost::Parse("sharpen",chain);CHECK(frame->Upload_Back_Buffer(pixels));CHECK(frame->Post_Process(target,rect,0,&chain));
    CHECK(frame->Read_Back(target.Colour,33,23,out));
    if(out.size()==pixels.size())for(unsigned i=0;i<out.size();i+=4){CHECK(out[i]>=10&&out[i]<=240);CHECK(out[i+3]==pixels[i+3]);}
    // Bright adjacent view content must not bleed into the selected constant-grey viewport.
    for(unsigned y=0;y<23;++y)for(unsigned x=0;x<33;++x){const unsigned at=(y*33+x)*4;
        const bool inside=x>=5&&x<25&&y>=4&&y<17;
        pixels[at]=inside?96:0;pixels[at+1]=inside?96:255;pixels[at+2]=inside?96:0;pixels[at+3]=67;
    }
    const float cropped[4]={5,4,20,13};ScenePost::Parse("fxaa,sharpen",chain);
    CHECK(frame->Upload_Back_Buffer(pixels));CHECK(frame->Post_Process(target,cropped,0,&chain));
    CHECK(frame->Read_Back(target.Colour,33,23,out));CHECK(out==pixels);
    frame->Clear_Rect(6,5,5,5,true,false,false,0xffffffff,1,0);
    CHECK(frame->Read_Back(target.Colour,33,23,out));CHECK(out[(7*33+8)*4]==255); // UI after filtering stays sharp.
    CHECK(frame->Resize(41,27));target=frame->Back_Buffer_Target();
    pixels.assign(41*27*4,100);for(unsigned i=3;i<pixels.size();i+=4)pixels[i]=77;
    for(unsigned y=0;y<27;++y)for(unsigned x=0;x<41;++x){unsigned at=(y*41+x)*4;pixels[at]=pixels[at+1]=pixels[at+2]=x*2>y*3?240:10;}
    CHECK(frame->Upload_Back_Buffer(pixels));const float resized[4]={0,0,41,27};
    CHECK(frame->Handle_Memory_Pressure());CHECK(frame->Post_Process(target,resized,0,&chain));
    CHECK(frame->Read_Back(target.Colour,41,27,out));CHECK(out==pixels);
    // Hardware MSAA skips FXAA even if requested; no edge pixels may be changed.
    std::unique_ptr<SdlGpuFrame> msaa(SdlGpuFrame::Create(nullptr,41,27,error,false,4));
    CHECK(msaa!=nullptr);
    if(msaa&&msaa->Sample_Count()>1){ScenePost::Parse("fxaa",chain);CHECK(msaa->Upload_Back_Buffer(pixels));
        CHECK(msaa->Post_Process(msaa->Back_Buffer_Target(),resized,0,&chain));
        CHECK(msaa->Read_Back(msaa->Back_Buffer_Target().Colour,41,27,out));CHECK(out==pixels);
        for(unsigned y=0;y<27;++y)for(unsigned x=0;x<41;++x){unsigned at=(y*41+x)*4;pixels[at]=pixels[at+1]=pixels[at+2]=x>=20?240:10;}
        CHECK(msaa->Upload_Back_Buffer(pixels));ScenePost::Parse("sharpen",chain);
        CHECK(msaa->Post_Process(msaa->Back_Buffer_Target(),resized,0,&chain));CHECK(msaa->Read_Back(msaa->Back_Buffer_Target().Colour,41,27,out));
        CHECK(out!=pixels);if(out.size()==pixels.size())for(unsigned i=0;i<out.size();i+=4){CHECK(out[i]>=10&&out[i]<=240);CHECK(out[i+3]==pixels[i+3]);}
    }
    printf("POST: failures=%u backend=%s\n",failures,SDL_GetGPUDeviceDriver(frame->Device()));
    msaa.reset();frame.reset();SDL_Quit();return failures?1:0;
}
