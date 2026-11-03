#pragma once
#include <SDL3/SDL.h>
#include "SdlAoShader.h"
#include <string>
namespace SdlAo {
class Renderer {
    SDL_GPUDevice *Device;
    SDL_GPUShader *Vertex=nullptr,*Pixels[3]={};
    SDL_GPUGraphicsPipeline *Pipelines[3]={};
    unsigned Slots[3]={};
    SDL_GPUSampler *Sampler=nullptr,*ShroudSampler=nullptr;
    SDL_GPUTexture *ResolvedDepth=nullptr,*Ping[2]={};
    unsigned Width=0,Height=0,ColourFormat=0;
    void ReleaseTargets();
public:
    explicit Renderer(SDL_GPUDevice *device):Device(device) {}
    ~Renderer();
    bool Prepare(unsigned width,unsigned height,unsigned colourFormat,std::string &error);
    SDL_GPUTexture *Depth() const { return ResolvedDepth; }
    bool Render(SDL_GPUCommandBuffer *commands,SDL_GPUTexture *colour,const Constants &constants,SDL_GPUTexture *shroud=nullptr);
    Renderer(const Renderer &)=delete;
    Renderer &operator=(const Renderer &)=delete;
};
}
