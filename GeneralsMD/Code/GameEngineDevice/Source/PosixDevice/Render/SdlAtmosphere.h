// Distributed under the repository's GPL-3.0 terms; see LICENSE.md.
#pragma once
#include <SDL3/SDL.h>
#include "SdlAtmosphereShader.h"
#include "SdlFirelightShader.h"
namespace SdlAtmosphere {
class Renderer {
    SDL_GPUDevice *Device;
    SDL_GPUShader *Vertex=nullptr,*Pixel=nullptr;
    SDL_GPUGraphicsPipeline *Pipeline=nullptr;
    SDL_GPUShader *FirePixel=nullptr;
    SDL_GPUGraphicsPipeline *FirePipeline=nullptr;
    SDL_GPUSampler *Point=nullptr,*Linear=nullptr,*Repeat=nullptr;
    SDL_GPUTexture *ResolvedDepth=nullptr;
    unsigned Width=0,Height=0,ColourFormat=0;
public:
    explicit Renderer(SDL_GPUDevice *device):Device(device){}
    ~Renderer();
    bool Prepare(unsigned width,unsigned height,unsigned format,std::string &error);
    bool Render(SDL_GPUCommandBuffer *commands,SDL_GPUTexture *colour,const Constants &constants,
        SDL_GPUTexture *sky,SDL_GPUTexture *shroud);
    bool PrepareFirelight(unsigned width,unsigned height,unsigned format,std::string &error);
    bool RenderFirelight(SDL_GPUCommandBuffer *commands,SDL_GPUTexture *colour,const SdlFirelight::Constants &constants);
    SDL_GPUTexture *Depth() const {return ResolvedDepth;}
    Renderer(const Renderer&)=delete;
    Renderer &operator=(const Renderer&)=delete;
};
}
