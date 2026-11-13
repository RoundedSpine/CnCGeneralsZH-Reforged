#pragma once

#if defined(ZH_PLATFORM_IOS)
#include <SDL3/SDL_gpu.h>

extern "C" bool ZH_SDL_MetalFX_Prepare(SDL_GPUDevice *device, SDL_GPUTexture *input,
	SDL_GPUTexture *output);
extern "C" bool ZH_SDL_MetalFX_Encode(SDL_GPUDevice *device, SDL_GPUTexture *input,
	SDL_GPUTexture *output);
#endif
