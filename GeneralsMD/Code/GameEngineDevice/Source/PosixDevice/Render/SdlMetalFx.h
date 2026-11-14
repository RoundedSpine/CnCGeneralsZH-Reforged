#pragma once

#if defined(ZH_PLATFORM_IOS)
#include <SDL3/SDL_gpu.h>

// colorProcessingMode: 0 perceptual (sRGB LDR), 1 linear (tone-mapped linear values),
// 2 HDR (scene-linear radiance, before tone mapping). The engine tone maps before the
// upscale, so HDR scenes pass 1 and LDR scenes pass 0.
extern "C" bool ZH_SDL_MetalFX_Prepare(SDL_GPUDevice *device, SDL_GPUTexture *input,
	SDL_GPUTexture *output, int colorProcessingMode);
extern "C" bool ZH_SDL_MetalFX_Encode(SDL_GPUDevice *device, SDL_GPUTexture *input,
	SDL_GPUTexture *output);
#endif
