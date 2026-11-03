// Actual device MSAA: edge coverage, depth/alpha replay, redirected scene targets,
// GPU copies, HDR post/UI sequencing, partial depth clears and cache eviction.
#include "PosixDevice9.h"
#include "SdlGpuFrame.h"
#include "Platform/SceneEffects.h"
#include <SDL3/SDL.h>
#include <cstdio>
#include <cmath>
#include <vector>
static unsigned failures=0;
#define CHECK(x) do{if(!(x)){++failures;fprintf(stderr,"FAIL MSAA %d: %s\n",__LINE__,#x);}}while(0)
struct Vertex{float x,y,z,rhw;uint32_t colour;float u,v;};
static unsigned edgePixels(const std::vector<uint8_t>&pixels,unsigned channel){unsigned count=0;for(unsigned i=channel;i<pixels.size();i+=4)if(pixels[i]>0&&pixels[i]<255)++count;return count;}
int main(){
#if defined(__APPLE__)
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER,"dummy");SDL_SetHint("ZH_SDL_GPU_METAL_WINDOWLESS","1");
#else
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER,"offscreen");
#endif
    if(!SDL_Init(SDL_INIT_VIDEO)){printf("SKIP MSAA GPU: %s\n",SDL_GetError());return 77;}
    for(bool hdr:{false,true}) {
        SceneEffects::RequestDisplay(hdr,4);
        auto *adapter=Direct3DCreate9(D3D_SDK_VERSION);D3DPRESENT_PARAMETERS p={};p.BackBufferWidth=64;p.BackBufferHeight=48;
        p.BackBufferFormat=D3DFMT_A8R8G8B8;p.Windowed=1;p.EnableAutoDepthStencil=1;p.AutoDepthStencilFormat=D3DFMT_D24S8;
        IDirect3DDevice9 *created=nullptr;CHECK(adapter->CreateDevice(0,D3DDEVTYPE_HAL,nullptr,0,&p,&created)==D3D_OK);
        auto *d=static_cast<PosixDevice9*>(created);
        if(!d||d->Create_Gpu_Frame(true)!=D3D_OK){printf("SKIP MSAA GPU: %s\n",SDL_GetError());if(d)d->Release();adapter->Release();SceneEffects::RequestDisplay(false,0);SDL_Quit();return failures?1:77;}
        auto *frame=d->Get_Gpu();CHECK(frame->Sample_Count()>=2&&frame->Sample_Count()<=4);CHECK(frame->Hdr_Active()==hdr);
        auto state=[&](){d->SetFVF(D3DFVF_XYZRHW|D3DFVF_DIFFUSE|D3DFVF_TEX1);d->SetTexture(0,nullptr);
            d->SetRenderState(D3DRS_LIGHTING,0);d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE);
            d->SetRenderState(D3DRS_ZENABLE,D3DZB_TRUE);d->SetRenderState(D3DRS_ZWRITEENABLE,1);
            d->SetRenderState(D3DRS_ALPHABLENDENABLE,0);d->SetRenderState(D3DRS_ALPHATESTENABLE,0);
            d->SetTextureStageState(0,D3DTSS_COLOROP,D3DTOP_MODULATE);d->SetTextureStageState(0,D3DTSS_COLORARG1,D3DTA_TEXTURE);d->SetTextureStageState(0,D3DTSS_COLORARG2,D3DTA_DIFFUSE);
            d->SetTextureStageState(0,D3DTSS_ALPHAOP,D3DTOP_MODULATE);d->SetTextureStageState(0,D3DTSS_ALPHAARG1,D3DTA_TEXTURE);d->SetTextureStageState(0,D3DTSS_ALPHAARG2,D3DTA_DIFFUSE);
            d->SetTextureStageState(1,D3DTSS_COLOROP,D3DTOP_DISABLE);};
        auto triangle=[&](uint32_t colour){Vertex v[3]={{5,6,.4f,1,colour,0,0},{58,7,.4f,1,colour,0,0},{8,41,.4f,1,colour,0,0}};
            auto before=d->Draws_Recorded();CHECK(d->DrawPrimitiveUP(D3DPT_TRIANGLELIST,1,v,sizeof(Vertex))==D3D_OK);CHECK(d->Draws_Recorded()==before+1);};
        auto quad=[&](float right,float bottom,float z){Vertex v[4]={{0,0,z,1,0xffffffff,0,0},{right,0,z,1,0xffffffff,1,0},{0,bottom,z,1,0xffffffff,0,1},{right,bottom,z,1,0xffffffff,1,1}};
            auto before=d->Draws_Recorded();CHECK(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,v,sizeof(Vertex))==D3D_OK);CHECK(d->Draws_Recorded()==before+1);};
        state();d->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER|D3DCLEAR_STENCIL,0xff000000,1,0);triangle(0xffff0000);
        std::vector<uint8_t> pixels;CHECK(frame->Read_Back(frame->Back_Buffer(),64,48,pixels));CHECK(edgePixels(pixels,2)>10);
        std::vector<float> depth;CHECK(frame->Read_Depth(frame->Back_Buffer_Target().Depth,64,48,depth));
        if(!depth.empty()){CHECK(std::abs(depth[14*64+12]-.4f)<.001f);CHECK(depth[0]>.99f);}
        IDirect3DTexture9 *alpha=nullptr;CHECK(d->CreateTexture(2,1,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&alpha,nullptr)==D3D_OK);
        D3DLOCKED_RECT lock={};CHECK(alpha->LockRect(0,&lock,nullptr,0)==D3D_OK);((uint32_t*)lock.pBits)[0]=0x0000ff00;((uint32_t*)lock.pBits)[1]=0xff00ff00;alpha->UnlockRect(0);
        d->SetTexture(0,alpha);d->SetRenderState(D3DRS_ALPHATESTENABLE,1);d->SetRenderState(D3DRS_ALPHAFUNC,D3DCMP_GREATER);d->SetRenderState(D3DRS_ALPHAREF,128);
        quad(64,48,.2f);CHECK(frame->Read_Depth(frame->Back_Buffer_Target().Depth,64,48,depth));
        if(!depth.empty()){CHECK(std::abs(depth[14*64+12]-.4f)<.001f);CHECK(std::abs(depth[14*64+40]-.2f)<.001f);}
        // A second-stage alpha mask exercises independently compacted replay bindings.
        d->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER|D3DCLEAR_STENCIL,0xff000000,1,0);
        d->SetTexture(0,nullptr);d->SetTexture(1,alpha);
        d->SetTextureStageState(1,D3DTSS_COLOROP,D3DTOP_SELECTARG1);d->SetTextureStageState(1,D3DTSS_COLORARG1,D3DTA_CURRENT);
        d->SetTextureStageState(1,D3DTSS_ALPHAOP,D3DTOP_SELECTARG1);d->SetTextureStageState(1,D3DTSS_ALPHAARG1,D3DTA_TEXTURE);
        quad(64,48,.2f);CHECK(frame->Read_Depth(frame->Back_Buffer_Target().Depth,64,48,depth));
        if(!depth.empty()){CHECK(depth[14*64+12]>.99f);CHECK(std::abs(depth[14*64+40]-.2f)<.001f);}
        d->SetTexture(1,nullptr);
        IDirect3DSurface9 *back=nullptr,*surface=nullptr,*cpu=nullptr;IDirect3DTexture9 *scene=nullptr;
        CHECK(d->GetRenderTarget(0,&back)==D3D_OK);CHECK(d->CreateTexture(64,48,1,D3DUSAGE_RENDERTARGET,D3DFMT_A8R8G8B8,D3DPOOL_DEFAULT,&scene,nullptr)==D3D_OK);
        CHECK(scene->GetSurfaceLevel(0,&surface)==D3D_OK);CHECK(d->SetRenderTarget(0,surface)==D3D_OK);
        state();d->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER|D3DCLEAR_STENCIL,0xff000000,1,0);triangle(0xff0000ff);
        CHECK(d->CreateOffscreenPlainSurface(64,48,D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM,&cpu,nullptr)==D3D_OK);CHECK(d->GetRenderTargetData(surface,cpu)==D3D_OK);
        CHECK(cpu->LockRect(&lock,nullptr,D3DLOCK_READONLY)==D3D_OK);unsigned edges=0;
        for(unsigned y=0;y<48;++y)for(unsigned x=0;x<64;++x){auto b=((uint8_t*)lock.pBits)[y*lock.Pitch+x*4];if(b>0&&b<255)++edges;}cpu->UnlockRect();CHECK(edges>10);
        CHECK(d->SetRenderTarget(0,back)==D3D_OK);CHECK(d->StretchRect(surface,nullptr,back,nullptr,D3DTEXF_POINT)==D3D_OK);
        if(hdr){CHECK(frame->Flush());const float rect[4]={0,0,64,48};CHECK(frame->Post_Process(frame->Back_Buffer_Target(),rect,0));}
        std::vector<uint8_t> expected;CHECK(frame->Read_Back(frame->Back_Buffer(),64,48,expected));
        d->SetRenderState(D3DRS_ZENABLE,D3DZB_FALSE);d->SetRenderState(D3DRS_ZWRITEENABLE,0);quad(4,4,.1f);
        CHECK(frame->Read_Back(frame->Back_Buffer(),64,48,pixels));
        if(!pixels.empty()&&!expected.empty()){CHECK(pixels[0]==255&&pixels[1]==255&&pixels[2]==255);const unsigned at=(14*64+12)*4;CHECK(pixels[at]==expected[at]&&pixels[at+2]==expected[at+2]);}
        D3DRECT small={0,0,6,6};CHECK(d->Clear(1,&small,D3DCLEAR_ZBUFFER,0,.9f,0)==D3D_OK);
        CHECK(frame->Read_Depth(frame->Back_Buffer_Target().Depth,64,48,depth));if(!depth.empty())CHECK(std::abs(depth[0]-.9f)<.001f);
        CHECK(frame->Trim_Transient_Memory());state();d->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,0xff000000,1,0);triangle(0xffff0000);
        CHECK(frame->Read_Back(frame->Back_Buffer(),64,48,pixels));CHECK(edgePixels(pixels,2)>10);
        CHECK(frame->Handle_Memory_Pressure());CHECK(frame->Sample_Count()==1&&frame->Max_Samples()==1);
        state();d->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,0xff000000,1,0);triangle(0xffff0000);
        CHECK(frame->Read_Back(frame->Back_Buffer(),64,48,pixels));CHECK(edgePixels(pixels,2)==0);
        CHECK(frame->Handle_Memory_Pressure());
        cpu->Release();surface->Release();scene->Release();back->Release();alpha->Release();
        printf("MSAA: hdr=%d samples=%u edges=%u failures=%u backend=%s\n",int(hdr),frame->Sample_Count(),edges,failures,SDL_GetGPUDeviceDriver(frame->Device()));
        d->Release();adapter->Release();SceneEffects::RequestDisplay(false,0);
    }
    SDL_Quit();return failures?1:0;
}
