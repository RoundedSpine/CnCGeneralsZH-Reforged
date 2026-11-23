// Distributed under the repository's GPL-3.0 terms; see LICENSE.md.
#include "SdlGpuFrame.h"
#include "CameraMotion.h"
#include "sdl3shadercompile.h"
#include "Platform/IOSLifecycle.h"

#include <SDL3/SDL.h>
#if defined(__APPLE__)
extern "C" void ZH_SDL_Metal_SetNextRenderPassTag(Uint32 category);
#endif

#include <cstdio>
#include <string>

void SdlGpuFrame::Release_Camera_Motion()
{
	if (GpuDevice == NULL)
		return;
	if (CameraMotionPipeline) SDL_ReleaseGPUGraphicsPipeline(GpuDevice, CameraMotionPipeline);
	if (CameraMotionVertex) SDL_ReleaseGPUShader(GpuDevice, CameraMotionVertex);
	if (CameraMotionPixel) SDL_ReleaseGPUShader(GpuDevice, CameraMotionPixel);
	if (CameraMotionSampler) SDL_ReleaseGPUSampler(GpuDevice, CameraMotionSampler);
	if (CameraMotionTexture) Release_Texture(CameraMotionTexture);
	if (CameraMotionDepth) Release_Texture(CameraMotionDepth);
	CameraMotionPipeline = nullptr;
	CameraMotionVertex = CameraMotionPixel = nullptr;
	CameraMotionSampler = nullptr;
	CameraMotionTexture = CameraMotionDepth = nullptr;
	CameraMotionWidth = CameraMotionHeight = 0;
}

bool SdlGpuFrame::Prepare_Camera_Motion(unsigned width, unsigned height, std::string &error)
{
	const SDL_GPUTextureUsageFlags usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
	SDL_GPUTextureFormat format = SDL_GPU_TEXTUREFORMAT_INVALID;
	if (SDL_GPUTextureSupportsFormat(GpuDevice, SDL_GPU_TEXTUREFORMAT_R16G16_FLOAT, SDL_GPU_TEXTURETYPE_2D, usage))
		format = SDL_GPU_TEXTUREFORMAT_R16G16_FLOAT;
	else if (SDL_GPUTextureSupportsFormat(GpuDevice, SDL_GPU_TEXTUREFORMAT_R32G32_FLOAT, SDL_GPU_TEXTURETYPE_2D, usage))
		format = SDL_GPU_TEXTUREFORMAT_R32G32_FLOAT;
	else {
		error = "no RG16Float or RG32Float motion target";
		return false;
	}
	if (!SDL_GPUTextureSupportsFormat(GpuDevice, SDL_GPU_TEXTUREFORMAT_R32_FLOAT, SDL_GPU_TEXTURETYPE_2D, usage)) {
		error = "no R32Float depth resolve for camera motion";
		return false;
	}
	const bool sizeChanged = CameraMotionTexture && (CameraMotionWidth != width || CameraMotionHeight != height
		|| CameraMotionFormat != static_cast<unsigned>(format));
	if (sizeChanged) {
		// The previous frame's pass may still be in flight. Resize is rare.
		SDL_WaitForGPUIdle(GpuDevice);
		if (CameraMotionTexture) Release_Texture(CameraMotionTexture);
		if (CameraMotionDepth) Release_Texture(CameraMotionDepth);
		CameraMotionTexture = CameraMotionDepth = nullptr;
		CameraMotionWidth = CameraMotionHeight = 0;
	}
	auto make = [&](SDL_GPUTextureFormat textureFormat) {
		SDL_GPUTextureCreateInfo info;
		SDL_zero(info);
		info.type = SDL_GPU_TEXTURETYPE_2D;
		info.format = textureFormat;
		info.usage = usage;
		info.width = width;
		info.height = height;
		info.layer_count_or_depth = 1;
		info.num_levels = 1;
		info.sample_count = SDL_GPU_SAMPLECOUNT_1;
		return SDL_CreateGPUTexture(GpuDevice, &info);
	};
	if (!CameraMotionTexture) {
		CameraMotionTexture = make(format);
		CameraMotionDepth = make(SDL_GPU_TEXTUREFORMAT_R32_FLOAT);
		if (!CameraMotionTexture || !CameraMotionDepth) {
			error = SDL_GetError();
			if (CameraMotionTexture) Release_Texture(CameraMotionTexture);
			if (CameraMotionDepth) Release_Texture(CameraMotionDepth);
			CameraMotionTexture = CameraMotionDepth = nullptr;
			return false;
		}
		CameraMotionWidth = width;
		CameraMotionHeight = height;
		CameraMotionFormat = static_cast<unsigned>(format);
	}
	if (!CameraMotionVertex) {
		std::vector<unsigned char> spirv;
		if (!SDL3_Compile_HLSL_To_SPIRV(CameraMotion::VertexShader(), true, spirv, error)
			|| !(CameraMotionVertex = SDL3_Create_Shader(GpuDevice, spirv, true, error)))
			return false;
	}
	if (!CameraMotionPixel) {
		std::vector<unsigned char> spirv;
		if (!SDL3_Compile_HLSL_To_SPIRV(CameraMotion::PixelShader(), false, spirv, error)
			|| !(CameraMotionPixel = SDL3_Create_Shader(GpuDevice, spirv, false, error)))
			return false;
	}
	if (!CameraMotionSampler) {
		SDL_GPUSamplerCreateInfo sampler;
		SDL_zero(sampler);
		sampler.min_filter = sampler.mag_filter = SDL_GPU_FILTER_NEAREST;
		sampler.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
		sampler.address_mode_u = sampler.address_mode_v = sampler.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
		CameraMotionSampler = SDL_CreateGPUSampler(GpuDevice, &sampler);
		if (!CameraMotionSampler) {
			error = SDL_GetError();
			return false;
		}
	}
	if (!CameraMotionPipeline) {
		SDL_GPUColorTargetDescription colour = {};
		colour.format = format;
		colour.blend_state.enable_color_write_mask = true;
		colour.blend_state.color_write_mask = SDL_GPU_COLORCOMPONENT_R | SDL_GPU_COLORCOMPONENT_G;
		SDL_GPUGraphicsPipelineCreateInfo info = {};
		info.vertex_shader = CameraMotionVertex;
		info.fragment_shader = CameraMotionPixel;
		info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
		info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
		info.target_info.num_color_targets = 1;
		info.target_info.color_target_descriptions = &colour;
		CameraMotionPipeline = SDL_CreateGPUGraphicsPipeline(GpuDevice, &info);
		if (!CameraMotionPipeline) {
			error = SDL_GetError();
			return false;
		}
	}
	return true;
}

bool SdlGpuFrame::Record_Camera_Motion(SDL_GPUCommandBuffer *commands, const CameraMotion::Constants &constants)
{
	SDL_GPUColorTargetInfo colour = {};
	colour.texture = CameraMotionTexture;
	colour.load_op = SDL_GPU_LOADOP_DONT_CARE;
	colour.store_op = SDL_GPU_STOREOP_STORE;
#if defined(__APPLE__)
	ZH_SDL_Metal_SetNextRenderPassTag(2u);
#endif
	SDL_GPURenderPass *pass = SDL_BeginGPURenderPass(commands, &colour, 1, nullptr);
	if (!pass)
		return false;
	SDL_BindGPUGraphicsPipeline(pass, CameraMotionPipeline);
	const SDL_GPUTextureSamplerBinding binding = { CameraMotionDepth, CameraMotionSampler };
	SDL_BindGPUFragmentSamplers(pass, 0, &binding, 1);
	SDL_PushGPUFragmentUniformData(commands, 0, &constants, sizeof(constants));
	const SDL_GPUViewport viewport = { 0, 0, static_cast<float>(CameraMotionWidth), static_cast<float>(CameraMotionHeight), 0, 1 };
	SDL_SetGPUViewport(pass, &viewport);
	SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);
	SDL_EndGPURenderPass(pass);
	return true;
}

bool SdlGpuFrame::Camera_Motion(const SdlTarget &target, const CameraMotion::View &view)
{
	if (CameraMotionFailed || !GpuDevice || !target.Depth || !target.Width || !target.Height)
		return false;
	if (!Supports_Scene_Depth()) {
		if (!CameraMotionUnsupportedLogged) {
			CameraMotionUnsupportedLogged = true;
			fprintf(stderr, "[SDL_MOTION] disabled reason=sampleable scene depth unsupported\n");
		}
		return false;
	}
	CameraMotion::Constants constants;
	if (!CameraMotion::Make(view, CameraMotionHavePrevious ? &CameraMotionPrevious : nullptr, constants))
		return false;
	std::string error;
	if (!Prepare_Camera_Motion(target.Width, target.Height, error)) {
		CameraMotionFailed = true;
		fprintf(stderr, "[SDL_MOTION] disabled reason=%s\n", error.empty() ? SDL_GetError() : error.c_str());
		Release_Camera_Motion();
		return false;
	}
	if (!Flush(FlushCameraMotion) || !Create_Scene_Depth_Pipeline())
		return false;

	const bool history = constants.DepthRemap[0] > 0.5f;
	ZH_IOS_WaitForForeground();
	SDL_GPUCommandBuffer *commands = SDL_AcquireGPUCommandBuffer(GpuDevice);
	if (!commands)
		return false;
	bool recorded = true;
	if (history)
		recorded = Resolve_Scene_Depth(commands, target.Depth, CameraMotionDepth, target.Width, target.Height)
			&& Record_Camera_Motion(commands, constants);
	else {
		SDL_GPUColorTargetInfo colour = {};
		colour.texture = CameraMotionTexture;
		colour.load_op = SDL_GPU_LOADOP_CLEAR;
		colour.store_op = SDL_GPU_STOREOP_STORE;
		colour.clear_color.r = colour.clear_color.g = colour.clear_color.b = colour.clear_color.a = 0;
#if defined(__APPLE__)
		ZH_SDL_Metal_SetNextRenderPassTag(2u);
#endif
		SDL_GPURenderPass *pass = SDL_BeginGPURenderPass(commands, &colour, 1, nullptr);
		recorded = pass != nullptr;
		if (pass)
			SDL_EndGPURenderPass(pass);
	}
	if (!recorded) {
		SDL_CancelGPUCommandBuffer(commands);
		return false;
	}
	if (!(SerializeSubmits ? Submit(commands) : Submit_Flush(commands)))
		return false;

	CameraMotionPrevious = view;
	CameraMotionHavePrevious = true;
	CameraMotionHistory = history;
	if (!CameraMotionReadyLogged) {
		CameraMotionReadyLogged = true;
		fprintf(stderr, "[SDL_MOTION] camera vectors ready size=%ux%u format=%s\n",
			target.Width, target.Height,
			CameraMotionFormat == SDL_GPU_TEXTUREFORMAT_R16G16_FLOAT ? "RG16Float" : "RG32Float");
	}
	return true;
}
