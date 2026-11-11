/*
**	Copyright 2026 İlyas Akın
**	Additional terms under GNU GPL section 7 apply: see LICENSE.md.
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
**
**	This program is distributed in the hope that it will be useful,
**	but WITHOUT ANY WARRANTY; without even the implied warranty of
**	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**	GNU General Public License for more details.
**
**	You should have received a copy of the GNU General Public License
**	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

// The device's frame on SDL3 GPU (decision 7, phase A3a).  See SdlGpuFrame.h.

#include "SdlGpuFrame.h"
#include "Platform/ParticlePerfToggle.h"
#include "Platform/ParticlePerfStats.h"
#include "SdlColour.h"
#include "SdlGpuDepthFormat.h"
#include "sdl3shadercompile.h"
#include "Platform/WaterFftSpectrum.h"
#include "Platform/IOSLifecycle.h"
#include "Platform/IOSWindowDiagnostics.h"
#include "Platform/WaterPerformanceSettings.h"

#include <SDL3/SDL.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__APPLE__)
// Opt-in hook in our pinned SDL3 Metal backend, implemented by
// Libraries/Source/sdl3-metal-gpu-timing.patch.
extern "C" void ZH_SDL_Metal_SetTimingEnabled(bool enabled);
extern "C" void ZH_SDL_Metal_TakeTiming(Uint64 *totalNs, Uint64 *maximumNs,
                                         Uint32 *samples, Uint32 *unavailable);
extern "C" void ZH_SDL_Metal_SetNextRenderPassTag(Uint32 category);
extern "C" void ZH_SDL_Metal_GetTimestampScale(double *scale);
extern "C" void ZH_SDL_Metal_TakePassTiming(Uint64 *ns, Uint64 *maxNs,
                                             Uint32 *count, Uint32 *missing);
extern "C" void ZH_SDL_Metal_TakeFragmentStatistics(Uint64 *fragmentInvocations,
                                                       Uint64 *fragmentsPassed,
                                                       Uint32 *count, Uint32 *missing);
#endif

// The back buffer's format.  D3D9's X8R8G8B8 and A8R8G8B8 are these bytes in this order.
static const SDL_GPUTextureFormat BACK_BUFFER_FORMAT = SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM;

// The gamma pass: one triangle over the whole target, and each channel looked up in its ramp.  Written
// for SDL3's register spaces (sdl3target.h: pixel textures and samplers in space2).  The ramp is read
// with a point sampler at its entry's centre, so the lookup is exact.
static const char GAMMA_VERTEX_HLSL[] =
	"struct Output\n"
	"{\n"
	"	float4 Position : SV_Position;\n"
	"	[[vk::location(0)]] float2 Uv : TEXCOORD0;\n"
	"};\n"
	"Output main(uint id : SV_VertexID)\n"
	"{\n"
	"	Output output;\n"
	"	float2 corner = float2(id == 1 ? 3.0 : -1.0, id == 2 ? 3.0 : -1.0);\n"
	"	output.Position = float4(corner, 0.0, 1.0);\n"
	"	output.Uv = float2(corner.x * 0.5 + 0.5, 0.5 - corner.y * 0.5);\n"
	"	return output;\n"
	"}\n";

static const char GAMMA_PIXEL_HLSL[] =
	"Texture2D BackBuffer : register(t0, space2);\n"
	"SamplerState BackSampler : register(s0, space2);\n"
	"Texture2D Ramp : register(t1, space2);\n"
	"SamplerState RampSampler : register(s1, space2);\n"
	"float entry(float value, int channel)\n"
	"{\n"
	"	float index = round(saturate(value) * 255.0);\n"
	"	return Ramp.SampleLevel(RampSampler, float2((index + 0.5) / 256.0, 0.5), 0.0)[channel];\n"
	"}\n"
	"float4 main([[vk::location(0)]] float2 uv : TEXCOORD0) : SV_Target0\n"
	"{\n"
	"	float4 colour = BackBuffer.SampleLevel(BackSampler, uv, 0.0);\n"
	"	return float4(entry(colour.r, 0), entry(colour.g, 1), entry(colour.b, 2), colour.a);\n"
	"}\n";

// A separate colour-only pass avoids both read/write attachment feedback and backend-specific
// depth/stencil copy restrictions. Point sampling preserves object edges and the original depth.
static const char SCENE_DEPTH_PIXEL_HLSL[] =
	"Texture2D<float> SceneDepth : register(t0, space2);\n"
	"SamplerState DepthSampler : register(s0, space2);\n"
	"float main([[vk::location(0)]] float2 uv : TEXCOORD0) : SV_Target0\n"
	"{ return SceneDepth.SampleLevel(DepthSampler, uv, 0.0); }\n";

bool Sdl_Gamma_Is_Identity(const uint16_t (*ramp)[256])
{
	if (ramp == NULL) {
		return true;
	}
	for (int channel = 0; channel < 3; ++channel) {
		for (int index = 0; index < 256; ++index) {
			if (ramp[channel][index] != (uint16_t)(index * 257)) {
				return false;
			}
		}
	}
	return true;
}

unsigned int SdlGpuFrame::Target_Format()
{
	return BACK_BUFFER_FORMAT;
}

unsigned int SdlGpuFrame::Depth_Usage() const
{
	return SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET
		| (DepthSampleable ? SDL_GPU_TEXTUREUSAGE_SAMPLER : 0);
}

SdlGpuFrame::SdlGpuFrame() :
	SerializeSubmits(false),
	FlushMs(0.0),
	FenceMs(0.0),
	Flushes(0),
	AcquireMs(0.0),
	NotVisible(0),
	NotVisibleTotal(0),
	NotShown(0),
	NotShownTotal(0),
	ShownWidth(0),
	ShownHeight(0),
	FlushLimit(6),
	FlushesTotal(0),
	SubmitCallsTotal(0),
	FlushInFlightMost(0),
	FlushWaits(0),
	ParticleGpuWaitNs(0),
	ParticleGpuMaxWaitNs(0),
	ParticleGpuWaitAbove1Ms(0),
	ParticleGpuWaitAbove5Ms(0),
	ParticleGpuWaitAbove16Ms(0),
	ParticleGpuSubmitNs(0),
	ParticleGpuSubmitCount(0),
	ParticleGpuBlendedDraws(0),
	ParticleGpuAdditiveDraws(0),
	ParticleGpuBlendScissorPixels(0),
	ParticleGpuRecordedDraws(0),
	ParticleGpuUploadBytes(0),
	ParticleGpuRenderPasses(0),
	ParticleGpuDepthSnapshots(0),
	ParticleGpuBlits(0),
	ParticleGpuCopyPasses(0),
	ParticleGpuPerfFrames(0),
	ParticleGpuPerfFlushStart(0),
	ParticleGpuPerfWaitStart(0),
	BatchDrawLimit(2048),
	DrawLimitFlushes(0),
	OffscreenMs(0.0),
	OffscreenPresents(false),
	OffscreenHz(0),
	DisplayTexture(NULL),
	InFlightNext(0),
	NextTickNs(0),
	GpuDevice(NULL),
	Window(NULL),
	BackBuffer(NULL),
	DepthStencil(NULL),
	FrontCopy(NULL),
	DepthFormat(SDL_GPU_TEXTUREFORMAT_INVALID),
	BackWidth(0),
	BackHeight(0),
	BatchNumber(1),
	ClearVertex(NULL),
	ClearPixel(NULL),
	StreamBuffer(NULL),
	StreamBufferSize(0),
	Transfer(NULL),
	TransferSize(0),
	GammaVertex(NULL),
	GammaPixel(NULL),
	GammaPipeline(NULL),
	GammaPipelineFormat(SDL_GPU_TEXTUREFORMAT_INVALID),
	RampTexture(NULL),
	PointSampler(NULL)
{
	memset(LastConstants,0,sizeof(LastConstants));memset(LastConstantsSize,0,sizeof(LastConstantsSize));
	memset(ClearPipelines, 0, sizeof(ClearPipelines));
	memset(ParticleGpuFlushReasons, 0, sizeof(ParticleGpuFlushReasons));
	InFlight[0] = InFlight[1] = NULL;
	// Experimental bounded submission window: six (previously three) mid-frame flushes can
	// remain outstanding before a fence wait. The iOS profiling capture showed frequent
	// three-slot ring waits; keep this reversible via ZH_GPU_FLUSH_LIMIT=3 for A/B tests.
	// ZH_GPU_FLUSH_LIMIT=<n>: maximum mid-frame flushes in flight (default 6); 0 is unbounded.
	// the ring, which is how the ring's effect is measured.
	if (const char * limit = getenv("ZH_GPU_FLUSH_LIMIT")) {
		FlushLimit = (unsigned int)strtoul(limit, NULL, 10);
	}
	// ZH_GPU_BATCH_DRAWS=<n>: at most n draws in one batch (default 2048); 0 lets a batch grow until Present.
	if (const char * draws = getenv("ZH_GPU_BATCH_DRAWS")) {
		BatchDrawLimit = (unsigned int)strtoul(draws, NULL, 10);
	}
	memset(&CurrentTarget, 0, sizeof(CurrentTarget));
	TargetSet = false;
}

SdlGpuFrame * SdlGpuFrame::Create(RenderWindow window, unsigned int width, unsigned int height, std::string & error, bool hdr,unsigned samples)
{
	ZH_IOS_WaitForForeground();
	SdlGpuFrame * frame = new SdlGpuFrame();
	// Debug validation only when asked: it is slow, and the game runs without it.
	const bool debug = getenv("ZH_GPU_DEBUG") != NULL;
	frame->GpuDevice = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_MSL, debug, NULL);
	if (frame->GpuDevice == NULL) {
		error = std::string("SDL_CreateGPUDevice: ") + SDL_GetError();
		delete frame;
		return NULL;
	}
	ZH_IOS_RegisterGPU(frame->GpuDevice);
	// The one place a RenderWindow is taken back to what it is: C2 hands the device its SDL_Window.
	frame->Window = reinterpret_cast<SDL_Window *>(window);
	ZH_IOS_TraceWindow("gpu.claim.begin", frame->Window, frame->GpuDevice);
	if (frame->Window != NULL && !SDL_ClaimWindowForGPUDevice(frame->GpuDevice, frame->Window)) {
		error = std::string("SDL_ClaimWindowForGPUDevice: ") + SDL_GetError();
		ZH_IOS_TraceWindow("gpu.claim.failed", frame->Window, frame->GpuDevice);
		frame->Window = NULL;
		delete frame;
		return NULL;
	}
	ZH_IOS_TraceWindow("gpu.claim.end", frame->Window, frame->GpuDevice);
	const SdlGpuDepthFormat depth = Sdl_Select_Depth_Format(
		[frame](SDL_GPUTextureFormat format, SDL_GPUTextureType type, SDL_GPUTextureUsageFlags usage) {
			return SDL_GPUTextureSupportsFormat(frame->GpuDevice, format, type, usage);
		});
	frame->DepthFormat = depth.Format;
	frame->DepthSampleable = depth.Sampleable;
	if (frame->DepthFormat == SDL_GPU_TEXTUREFORMAT_INVALID) {
		error = "no supported depth/stencil render-target format";
		delete frame;
		return NULL;
	}

    frame->SceneFormat=BACK_BUFFER_FORMAT;
    if(hdr&&frame->Supports_Hdr()) {
        frame->SceneFormat=SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT;frame->HdrActive=true;
        if(!frame->Prepare_Post()){frame->SceneFormat=BACK_BUFFER_FORMAT;frame->HdrActive=false;}
    }
    fprintf(stderr,"[SDL_SCENE] hdr_requested=%d hdr_active=%d format=%u\n",int(hdr),int(frame->HdrActive),frame->SceneFormat);

    for(unsigned candidate:{8u,4u,2u})if(candidate<=samples&&frame->Supports_Samples(candidate)) {
        frame->SampleCount=candidate;break;
    }
    if(frame->SampleCount>1&&!frame->Prepare_Multisampling()){frame->MsaaFailed=true;frame->SampleCount=1;}
    fprintf(stderr,"[SDL_MSAA] requested=%u active=%u\n",samples,frame->SampleCount);
	if (!frame->Create_Targets(width, height)) {
		error = std::string("the back buffer: ") + SDL_GetError();
		delete frame;
		return NULL;
	}
	return frame;
}

bool SdlGpuFrame::Configure_VSync(bool enabled)
{
	if (Window == NULL) return true;
	SDL_GPUPresentMode requested = SDL_GPU_PRESENTMODE_VSYNC;
	if (!enabled) {
		if (SDL_WindowSupportsGPUPresentMode(GpuDevice, Window, SDL_GPU_PRESENTMODE_IMMEDIATE))
			requested = SDL_GPU_PRESENTMODE_IMMEDIATE;
		else if (SDL_WindowSupportsGPUPresentMode(GpuDevice, Window, SDL_GPU_PRESENTMODE_MAILBOX))
			requested = SDL_GPU_PRESENTMODE_MAILBOX;
	}
	// Reset/Create call this outside Present, with no acquired swapchain image. Submit pending
	// readers and finish outstanding GPU work before SDL replaces the swapchain.
	bool applied = true;
	if (PresentMode != uint32_t(requested)) {
		applied = Flush(FlushMaintenance) && SDL_WaitForGPUIdle(GpuDevice)
			&& SDL_SetGPUSwapchainParameters(GpuDevice, Window, SDL_GPU_SWAPCHAINCOMPOSITION_SDR, requested);
		if (applied) PresentMode = uint32_t(requested);
	}
	const char *mode = PresentMode == SDL_GPU_PRESENTMODE_IMMEDIATE ? "immediate"
		: PresentMode == SDL_GPU_PRESENTMODE_MAILBOX ? "mailbox" : "vsync";
	fprintf(stderr, "[SDL_PRESENT] vsync_requested=%d mode=%s fallback=%d applied=%d%s%s\n",
		int(enabled), mode, int(!enabled && requested == SDL_GPU_PRESENTMODE_VSYNC), int(applied),
		applied ? "" : " error=", applied ? "" : SDL_GetError());
	return applied;
}

SdlGpuFrame::~SdlGpuFrame()
{
	ZH_IOS_UnregisterGPU(GpuDevice);
	if (GpuDevice == NULL) {
		return;
	}
	for (int i = 0; i < 2; ++i) {
		if (InFlight[i] != NULL) {
			SDL_WaitForGPUFences(GpuDevice, true, &InFlight[i], 1);
			SDL_ReleaseGPUFence(GpuDevice, InFlight[i]);
		}
	}
	for (size_t i = 0; i < FlushFences.size(); ++i) {
		SDL_WaitForGPUFences(GpuDevice, true, &FlushFences[i], 1);
		SDL_ReleaseGPUFence(GpuDevice, FlushFences[i]);
	}
	FlushFences.clear();
	Release_Ocean();
	Release_Targets();
	Commands.clear();
	End_Batch();
	Release_Sun_Smoke();
	Release_Sun_Shadow();
	Release_Ambient_Occlusion();
	Release_Atmosphere();
	Release_Post();
    Release_Particles();
	Release_Heat();
	if (StreamBuffer != NULL) SDL_ReleaseGPUBuffer(GpuDevice, StreamBuffer);
	for (int i = 0; i < 64; ++i) if (ClearPipelines[i] != NULL) SDL_ReleaseGPUGraphicsPipeline(GpuDevice, ClearPipelines[i]);
    Release_Multisampling(true);
	if (ClearVertex != NULL) SDL_ReleaseGPUShader(GpuDevice, ClearVertex);
	if (ClearPixel != NULL) SDL_ReleaseGPUShader(GpuDevice, ClearPixel);
	if (Transfer != NULL) SDL_ReleaseGPUTransferBuffer(GpuDevice, Transfer);
	if (GammaPipeline != NULL) SDL_ReleaseGPUGraphicsPipeline(GpuDevice, GammaPipeline);
	if (GammaVertex != NULL) SDL_ReleaseGPUShader(GpuDevice, GammaVertex);
	if (GammaPixel != NULL) SDL_ReleaseGPUShader(GpuDevice, GammaPixel);
	if (RampTexture != NULL) Release_Texture(RampTexture);
	if (PointSampler != NULL) SDL_ReleaseGPUSampler(GpuDevice, PointSampler);
	if (SceneDepthPipeline != NULL) SDL_ReleaseGPUGraphicsPipeline(GpuDevice, SceneDepthPipeline);
	if (SceneDepthVertex != NULL) SDL_ReleaseGPUShader(GpuDevice, SceneDepthVertex);
	if (SceneDepthPixel != NULL) SDL_ReleaseGPUShader(GpuDevice, SceneDepthPixel);
	if (SceneDepthSampler != NULL) SDL_ReleaseGPUSampler(GpuDevice, SceneDepthSampler);
	ZH_IOS_TraceWindow("gpu.release.begin", Window, GpuDevice);
	if (Window != NULL) SDL_ReleaseWindowFromGPUDevice(GpuDevice, Window);
	ZH_IOS_TraceWindow("gpu.release.end", Window, GpuDevice);
	SDL_DestroyGPUDevice(GpuDevice);
}

bool SdlGpuFrame::Create_Targets(unsigned int width, unsigned int height)
{
	SDL_GPUTextureCreateInfo info;
	SDL_zero(info);
	info.type = SDL_GPU_TEXTURETYPE_2D;
	info.format = (SDL_GPUTextureFormat)Scene_Format();
	info.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
	info.width = width;
	info.height = height;
	info.layer_count_or_depth = 1;
	info.num_levels = 1;
	info.sample_count = SDL_GPU_SAMPLECOUNT_1;
	SDL_GPUTexture *back = SDL_CreateGPUTexture(GpuDevice, &info);
	SDL_GPUTexture *front = SDL_CreateGPUTexture(GpuDevice, &info);
	info.format = (SDL_GPUTextureFormat)DepthFormat;
	info.usage = Depth_Usage();
	SDL_GPUTexture *depth = SDL_CreateGPUTexture(GpuDevice, &info);
	if (!back || !front || !depth) {
		if (back) Release_Texture(back);
		if (front) Release_Texture(front);
		if (depth) Release_Texture(depth);
		return false;
	}
	// Keep the old attachments, scratch depths and snapshots until every replacement exists.
	Release_Targets();
	BackBuffer = back;
	FrontCopy = front;
    Track_Colour(back);Track_Colour(front);
	DepthStencil = depth;
	BackWidth = width;
	BackHeight = height;
	return true;
}

bool SdlGpuFrame::Handle_Memory_Pressure()
{
    // Keep the lower budget until this device is recreated. A timer alone can
    // rebuild the same caches while iOS is still under pressure. Repeated warnings
    // must not cycle a mandatory scene-copy cache through release/reallocation.
    MemoryConstrained=true;
    if(SampleCount>1&&!Disable_Multisampling("iOS memory pressure"))return false;
    if(MemoryTrimmed)return true;
    if(!Trim_Transient_Memory())return false;
    MemoryTrimmed=true;
    fprintf(stderr,"[SDL_MEMORY] constrained=1 paused=ao,atmosphere,firelight,fft,post-filters,particle-overflow msaa=off recovery=device-recreation preferences=retained\n");
    return true;
}

bool SdlGpuFrame::Trim_Transient_Memory()
{
    if(!Flush(FlushMaintenance)||!SDL_WaitForGPUIdle(GpuDevice))return false;
    const size_t cpuStaging=Retained_Staging_Bytes();
    const uint64_t gpuStaging=uint64_t(StreamBufferSize)+TransferSize;
    // End_Batch clears sizes but keeps peak capacity. Under pressure, discard
    // that retained CPU allocation and the idle GPU upload/stream buffers too.
    decltype(StreamBytes)().swap(StreamBytes);decltype(UploadBytes)().swap(UploadBytes);
    decltype(ConstantBytes)().swap(ConstantBytes);decltype(Draws)().swap(Draws);
    decltype(Commands)().swap(Commands);decltype(Targets)().swap(Targets);decltype(Uploads)().swap(Uploads);
    if(StreamBuffer)SDL_ReleaseGPUBuffer(GpuDevice,StreamBuffer);
    if(Transfer)SDL_ReleaseGPUTransferBuffer(GpuDevice,Transfer);
    StreamBuffer=nullptr;Transfer=nullptr;StreamBufferSize=TransferSize=0;
    Release_Ambient_Occlusion();Release_Atmosphere();Release_Heat();Release_Ocean();Release_Multisampling(false);Release_Particles();
    for(auto &snapshot:WaterSnapshots) {
        if(snapshot.Colour)Release_Texture(snapshot.Colour);
        if(snapshot.Depth)Release_Texture(snapshot.Depth);
    }
    WaterSnapshots.clear();
    fprintf(stderr,"[SDL_MEMORY] released transient AO/atmosphere/effect/ocean/scene-copy caches cpu_staging_released_mib=%.1f gpu_staging_nominal_mib=%.1f\n",
        cpuStaging/1048576.0,gpuStaging/1048576.0);
    return true;
}

void SdlGpuFrame::Release_Targets()
{
    Release_Multisampling(false);
	for (size_t i = 0; i < WaterSnapshots.size(); ++i) {
		if (WaterSnapshots[i].Colour) Release_Texture(WaterSnapshots[i].Colour);
		if (WaterSnapshots[i].Depth) Release_Texture(WaterSnapshots[i].Depth);
	}
	WaterSnapshots.clear();
	for (size_t i = 0; i < ScratchDepths.size(); ++i) Release_Texture(ScratchDepths[i].Texture);
	ScratchDepths.clear();
	if (BackBuffer != NULL) Release_Texture(BackBuffer);
	if (DepthStencil != NULL) Release_Texture(DepthStencil);
	if (FrontCopy != NULL) Release_Texture(FrontCopy);
	if (DisplayTexture != NULL) Release_Texture(DisplayTexture);
	BackBuffer = NULL;
	DepthStencil = NULL;
	FrontCopy = NULL;
	DisplayTexture = NULL;		// made again at the new size by the next offscreen Present
}

// Private native float targets bypass the legacy BGRA texture mirror. The field is independent of
// window dimensions: resizing scene targets keeps both its seed and paused frame unchanged.
struct SdlGpuFrame::OceanResources
{
	WaterFftSpectrum::Configuration Configuration = WaterFftSpectrum::ForResolution(WaterFftSpectrum::Resolution);
	SDL_GPUTexture *Initial[3]={}, *Ping[2][6]={}, *Maps[6]={}, *Butterfly=NULL;
	SDL_GPUShader *Vertex=NULL, *Pixels[4]={};
	SDL_GPUGraphicsPipeline *Pipelines[4]={};
	SDL_GPUSampler *Point=NULL,*Linear=NULL;

	bool Pass(SDL_GPUCommandBuffer *commands,unsigned int which,SDL_GPUTexture **destination,
		SDL_GPUTexture **source,unsigned int count,const float *parameters)
	{
		SDL_GPUColorTargetInfo targets[6]={};
		for(unsigned i=0;i<6;++i) {
			targets[i].texture=destination[i];targets[i].load_op=SDL_GPU_LOADOP_DONT_CARE;
			targets[i].store_op=SDL_GPU_STOREOP_STORE;
			// No cycling: all prior reads were submitted first and must see their old field. SDL
			// orders the attachment/sampler transitions on this queue without a CPU wait/readback.
			for(unsigned j=0;j<count;++j) if(source[j]==destination[i]) return false;
		}
		#if defined(__APPLE__)
ZH_SDL_Metal_SetNextRenderPassTag(4u);
#endif
SDL_GPURenderPass *pass=SDL_BeginGPURenderPass(commands,targets,6,NULL);
		if(!pass) return false;
		SDL_BindGPUGraphicsPipeline(pass,Pipelines[which]);
		SDL_GPUTextureSamplerBinding bindings[7]={};
		for(unsigned i=0;i<count;++i) {bindings[i].texture=source[i];bindings[i].sampler=Point;}
		SDL_BindGPUFragmentSamplers(pass,0,bindings,count);
		const unsigned side=which==3?Configuration.resolution>>unsigned(parameters[2]):Configuration.resolution;
		const SDL_GPUViewport viewport={0,0,float(side),float(side),0,1};
		SDL_SetGPUViewport(pass,&viewport);
		SDL_PushGPUFragmentUniformData(commands,0,parameters,8*sizeof(float));
		SDL_DrawGPUPrimitives(pass,3,1,0,0);
		SDL_EndGPURenderPass(pass);
		return true;
	}
};

// SDL's native Vulkan mip generator uses linear blits, whose format capabilities are not
// covered by SDL_GPUTextureSupportsFormat. An explicit four-texel average only needs the
// same float sampler/colour/copy capabilities as the FFT itself. Existing ping targets are
// scratch; never sample a texture attached for writing, even at a different mip level.
static std::string Ocean_Mip_Hlsl()
{
	std::string source=WaterFftSpectrum::Resources(6);
	source+="Result main(float4 position:SV_Position) { int2 p=int2(position.xy)*2; int level=(int)Parameters.z-1; Result result;\n";
	for(unsigned i=0;i<6;++i) {
		const std::string s=std::to_string(i);
		source+="result.v"+s+"=(T"+s+".Load(int3(p,level))+T"+s+".Load(int3(p+int2(1,0),level))+T"+s+
			".Load(int3(p+int2(0,1),level))+T"+s+".Load(int3(p+int2(1,1),level)))*0.25;\n";
	}
	return source+"return result; }\n";
}

void SdlGpuFrame::Release_Ocean()
{
	if(!Ocean) return;
	for(auto *texture:Ocean->Initial) if(texture) SDL_ReleaseGPUTexture(GpuDevice,texture);
	for(auto &pair:Ocean->Ping) for(auto *texture:pair) if(texture) SDL_ReleaseGPUTexture(GpuDevice,texture);
	for(auto *texture:Ocean->Maps) if(texture) SDL_ReleaseGPUTexture(GpuDevice,texture);
	if(Ocean->Butterfly) SDL_ReleaseGPUTexture(GpuDevice,Ocean->Butterfly);
	for(auto *pipeline:Ocean->Pipelines) if(pipeline) SDL_ReleaseGPUGraphicsPipeline(GpuDevice,pipeline);
	for(auto *shader:Ocean->Pixels) if(shader) SDL_ReleaseGPUShader(GpuDevice,shader);
	if(Ocean->Vertex) SDL_ReleaseGPUShader(GpuDevice,Ocean->Vertex);
	if(Ocean->Point) SDL_ReleaseGPUSampler(GpuDevice,Ocean->Point);
	if(Ocean->Linear) SDL_ReleaseGPUSampler(GpuDevice,Ocean->Linear);
	delete Ocean;Ocean=NULL;OceanReady=false;
}

bool SdlGpuFrame::Fail_Ocean(const char *reason)
{
	if(!OceanFailed) fprintf(stderr,"SdlGpuFrame: GPU ocean unavailable; keeping analytic water: %s\n",reason);
	OceanFailed=true;Release_Ocean();return false;
}

bool SdlGpuFrame::Create_Ocean(WaterFftSpectrum::Configuration configuration)
{
	using namespace WaterFftSpectrum;
	if (FailNextOceanRebuild) { FailNextOceanRebuild = false; return Fail_Ocean("injected rebuild failure"); }
	const unsigned Resolution = configuration.resolution, Phases = configuration.phases;
	const SDL_GPUTextureUsageFlags usage=SDL_GPU_TEXTUREUSAGE_COLOR_TARGET|SDL_GPU_TEXTUREUSAGE_SAMPLER;
	if(!SDL_GPUTextureSupportsFormat(GpuDevice,SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT,SDL_GPU_TEXTURETYPE_2D,usage))
		return Fail_Ocean("RGBA32F colour-target/sampler usage unsupported");
	Ocean=new OceanResources;
	Ocean->Configuration = configuration;
	SDL_GPUTextureCreateInfo texture={};texture.type=SDL_GPU_TEXTURETYPE_2D;
	texture.format=SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT;texture.usage=usage;
	texture.width=texture.height=Resolution;texture.layer_count_or_depth=texture.num_levels=1;
	for(auto *&target:Ocean->Initial) target=SDL_CreateGPUTexture(GpuDevice,&texture);
	for(auto &pair:Ocean->Ping) for(auto *&target:pair) target=SDL_CreateGPUTexture(GpuDevice,&texture);
	texture.num_levels=Phases+1;
	for(auto *&target:Ocean->Maps) target=SDL_CreateGPUTexture(GpuDevice,&texture);
	texture.num_levels=1;texture.width=Phases;Ocean->Butterfly=SDL_CreateGPUTexture(GpuDevice,&texture);
	for(auto *target:Ocean->Initial) if(!target) return Fail_Ocean(SDL_GetError());
	for(auto &pair:Ocean->Ping) for(auto *target:pair) if(!target) return Fail_Ocean(SDL_GetError());
	for(auto *target:Ocean->Maps) if(!target) return Fail_Ocean(SDL_GetError());
	if(!Ocean->Butterfly) return Fail_Ocean(SDL_GetError());
	SDL_GPUSamplerCreateInfo sampler={};
	sampler.address_mode_u=sampler.address_mode_v=sampler.address_mode_w=SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
	sampler.min_filter=sampler.mag_filter=SDL_GPU_FILTER_NEAREST;sampler.mipmap_mode=SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
	Ocean->Point=SDL_CreateGPUSampler(GpuDevice,&sampler);
	sampler.min_filter=sampler.mag_filter=SDL_GPU_FILTER_LINEAR;
	sampler.mipmap_mode=SDL_GPU_SAMPLERMIPMAPMODE_LINEAR;sampler.max_lod=float(Phases);
	Ocean->Linear=SDL_CreateGPUSampler(GpuDevice,&sampler);
	if(!Ocean->Point || !Ocean->Linear) return Fail_Ocean(SDL_GetError());
	std::vector<unsigned char> spirv;std::string error;
	if(!SDL3_Compile_HLSL_To_SPIRV(VertexHlsl(),true,spirv,error) ||
		!(Ocean->Vertex=SDL3_Create_Shader(GpuDevice,spirv,true,error))) return Fail_Ocean(error.c_str());
	const std::string pixels[4]={SpectrumHlsl(),ButterflyHlsl(configuration),PostHlsl(),Ocean_Mip_Hlsl()};
	SDL_GPUColorTargetDescription targets[6]={};
	for(auto &target:targets) target.format=texture.format;
	for(unsigned i=0;i<4;++i) {
		if(!SDL3_Compile_HLSL_To_SPIRV(pixels[i],false,spirv,error) ||
			!(Ocean->Pixels[i]=SDL3_Create_Shader(GpuDevice,spirv,false,error))) return Fail_Ocean(error.c_str());
		SDL_GPUGraphicsPipelineCreateInfo pipeline={};pipeline.vertex_shader=Ocean->Vertex;pipeline.fragment_shader=Ocean->Pixels[i];
		pipeline.primitive_type=SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;pipeline.rasterizer_state.cull_mode=SDL_GPU_CULLMODE_NONE;
		pipeline.rasterizer_state.fill_mode=SDL_GPU_FILLMODE_FILL;
		pipeline.target_info.color_target_descriptions=targets;pipeline.target_info.num_color_targets=6;
		Ocean->Pipelines[i]=SDL_CreateGPUGraphicsPipeline(GpuDevice,&pipeline);
		if(!Ocean->Pipelines[i]) return Fail_Ocean(SDL_GetError());
	}
	const std::vector<float> initial=InitialSpectrum(configuration),butterfly=Butterfly(configuration);
	const Uint32 fieldBytes=Resolution*Resolution*4*sizeof(float),butterflyBytes=Phases*Resolution*4*sizeof(float);
	SDL_GPUTransferBufferCreateInfo transferInfo={};transferInfo.usage=SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
	transferInfo.size=fieldBytes*3+butterflyBytes;
	SDL_GPUTransferBuffer *transfer=SDL_CreateGPUTransferBuffer(GpuDevice,&transferInfo);
	if(!transfer) return Fail_Ocean(SDL_GetError());
	void *mapped=SDL_MapGPUTransferBuffer(GpuDevice,transfer,false);
	if(!mapped) {SDL_ReleaseGPUTransferBuffer(GpuDevice,transfer);return Fail_Ocean(SDL_GetError());}
	memcpy(mapped,initial.data(),fieldBytes*3);memcpy((char*)mapped+fieldBytes*3,butterfly.data(),butterflyBytes);
	SDL_UnmapGPUTransferBuffer(GpuDevice,transfer);
	ZH_IOS_WaitForForeground();
	SDL_GPUCommandBuffer *commands=SDL_AcquireGPUCommandBuffer(GpuDevice);
	if(!commands) {SDL_ReleaseGPUTransferBuffer(GpuDevice,transfer);return Fail_Ocean(SDL_GetError());}
	SDL_GPUCopyPass *copy=SDL_BeginGPUCopyPass(commands);
	if(!copy) {SDL_CancelGPUCommandBuffer(commands);SDL_ReleaseGPUTransferBuffer(GpuDevice,transfer);return Fail_Ocean(SDL_GetError());}
	for(unsigned i=0;i<4;++i) {
		SDL_GPUTextureTransferInfo upload={};upload.transfer_buffer=transfer;upload.offset=i*fieldBytes;
		upload.pixels_per_row=i==3?Phases:Resolution;upload.rows_per_layer=Resolution;
		SDL_GPUTextureRegion region={};region.texture=i==3?Ocean->Butterfly:Ocean->Initial[i];
		region.w=upload.pixels_per_row;region.h=Resolution;region.d=1;
		SDL_UploadToGPUTexture(copy,&upload,&region,false);
	}
	SDL_EndGPUCopyPass(copy);
	const bool submitted=SerializeSubmits?Submit(commands):Submit_Flush(commands);
	SDL_ReleaseGPUTransferBuffer(GpuDevice,transfer);
	if(!submitted) return Fail_Ocean(SDL_GetError());
	return true;
}

bool SdlGpuFrame::Prepare_Ocean(float time, unsigned resolution)
{
	if (MemoryConstrained || !std::isfinite(time)) return false;
	const auto requested = WaterFftSpectrum::ForResolution(resolution);
	if (OceanRequestedResolution != requested.resolution) {
		OceanFailedResolution = 0;
		OceanRequestedResolution = requested.resolution;
	}
	const bool boundary = OceanPrepareFrame != WaterFrame;
	OceanPrepareFrame = WaterFrame;
	const bool rebuild = !Ocean || (boundary && Ocean->Configuration.resolution != requested.resolution
		&& OceanFailedResolution != requested.resolution);
	if (OceanFailed && !Ocean && OceanFailedResolution == requested.resolution) return false;
	if (!rebuild && OceanReady && time == OceanTime) return true;
	// Submit older readers before replacing resources. Keep a working field on allocation failure.
	if ((!Commands.empty() || !Uploads.empty()) && !Flush(FlushOcean)) return false;
	OceanResources *previous = rebuild ? Ocean : NULL;
	const bool previousReady = OceanReady;
	const float previousTime = OceanTime;
	if (rebuild) { Ocean = NULL; OceanReady = false; OceanFailed = false; }
	auto failure = [&](const char *reason) {
		Fail_Ocean(reason);
		OceanFailedResolution = requested.resolution;
		if (previous) {
			Ocean = previous; OceanReady = previousReady; OceanTime = previousTime; OceanFailed = false;
			return OceanReady;
		}
		return false;
	};
	if (rebuild && !Create_Ocean(requested)) {
		OceanFailedResolution = requested.resolution;
		if (previous) {
			Ocean = previous; OceanReady = previousReady; OceanTime = previousTime; OceanFailed = false;
			return OceanReady;
		}
		return false;
	}
	const unsigned Resolution = Ocean->Configuration.resolution, Phases = Ocean->Configuration.phases;
	const Uint64 started=SDL_GetTicksNS();
	ZH_IOS_WaitForForeground();
	SDL_GPUCommandBuffer *commands=SDL_AcquireGPUCommandBuffer(GpuDevice);
	if(!commands) return failure(SDL_GetError());
	using namespace WaterFftSpectrum;
	float parameters[8]={float(Resolution),time,0,0,Sizes[0],Sizes[1],Sizes[2],0};
	bool ok=Ocean->Pass(commands,0,Ocean->Ping[0],Ocean->Initial,3,parameters);
	unsigned ping=0;
	for(unsigned axis=0;ok && axis<2;++axis) for(unsigned phase=0;ok && phase<Phases;++phase) {
		SDL_GPUTexture *sources[7];for(unsigned i=0;i<6;++i) sources[i]=Ocean->Ping[ping][i];sources[6]=Ocean->Butterfly;
		parameters[2]=float(phase);parameters[3]=float(axis);
		ok=Ocean->Pass(commands,1,Ocean->Ping[1-ping],sources,7,parameters);ping=1-ping;
	}
	if(ok) ok=Ocean->Pass(commands,2,Ocean->Maps,Ocean->Ping[ping],6,parameters);
	for(unsigned level=1;ok && level<=Phases;++level) {
		parameters[2]=float(level);
		ok=Ocean->Pass(commands,3,Ocean->Ping[0],Ocean->Maps,6,parameters);
		if(!ok) break;
		SDL_GPUCopyPass *copy=SDL_BeginGPUCopyPass(commands);
		if(!copy) {ok=false;break;}
		for(unsigned i=0;i<6;++i) {
			SDL_GPUTextureLocation source={};source.texture=Ocean->Ping[0][i];
			SDL_GPUTextureLocation destination={};destination.texture=Ocean->Maps[i];destination.mip_level=level;
			SDL_CopyGPUTextureToTexture(copy,&source,&destination,Resolution>>level,Resolution>>level,1,false);
		}
		SDL_EndGPUCopyPass(copy);
	}
	if(!ok) {SDL_CancelGPUCommandBuffer(commands);return failure("could not record independent FFT passes");}
	ok=SerializeSubmits?Submit(commands):Submit_Flush(commands);
	FlushMs+=double(SDL_GetTicksNS()-started)/1.0e6;++Flushes;++FlushesTotal;
	if(!ok) return failure(SDL_GetError());
	if (previous) {
		OceanResources *replacement = Ocean; Ocean = previous; Release_Ocean(); Ocean = replacement;
	}
	if (rebuild) OceanFailedResolution = 0;
	OceanTime=time;OceanReady=true;++OceanUpdates;return true;
}

WaterFftSpectrum::Configuration SdlGpuFrame::Ocean_Configuration() const
{
	return Ocean ? Ocean->Configuration : WaterFftSpectrum::ForResolution(WaterFftSpectrum::Resolution);
}

SDL_GPUTexture *SdlGpuFrame::Ocean_Map(unsigned int index) const
{
	return OceanReady && Ocean && index<6?Ocean->Maps[index]:NULL;
}

SDL_GPUSampler *SdlGpuFrame::Ocean_Sampler() const
{
	return OceanReady && Ocean?Ocean->Linear:NULL;
}

void SdlGpuFrame::Invalidate_Water_Snapshots(SDL_GPUTexture *colour, SDL_GPUTexture *depth)
{
	for (size_t i = 0; i < WaterSnapshots.size(); ++i) {
		WaterSnapshot &snapshot = WaterSnapshots[i];
		if ((colour && snapshot.Source.Colour == colour) || (depth && snapshot.Source.Depth == depth)) {
			snapshot.ColourReady = snapshot.DepthReady = snapshot.Frozen = false;
		}
	}
}

void SdlGpuFrame::Refresh_Water_Snapshots()
{
	++WaterFrame;
	for (size_t i = 0; i < WaterSnapshots.size(); ++i) WaterSnapshots[i].ColourReady = WaterSnapshots[i].DepthReady = WaterSnapshots[i].Frozen = false;
}

SdlGpuFrame::WaterSnapshot *SdlGpuFrame::Water_Snapshot(const SdlTarget &source, bool colour, bool depth)
{
	if (source.Width == 0 || source.Height == 0 || !source.Colour) return NULL;
	WaterSnapshot *snapshot = NULL;
	for (size_t i = 0; i < WaterSnapshots.size(); ++i) {
		WaterSnapshot &candidate = WaterSnapshots[i];
		if (candidate.Source.Colour == source.Colour && candidate.Source.Depth == source.Depth
			&& candidate.Source.Width == source.Width && candidate.Source.Height == source.Height) {
			snapshot = &candidate;
			break;
		}
	}
	if (!snapshot) {
		WaterSnapshot fresh = {};
		fresh.Source = source;
		if (WaterSnapshots.size() == 8) {
			// The cache is bounded even when effects continually allocate intermediate targets. Recorded
			// draws can keep using the retired textures until this batch has been submitted.
			size_t oldest = 0;
			for (size_t i = 1; i < WaterSnapshots.size(); ++i)
				if (WaterSnapshots[i].UsedFrame < WaterSnapshots[oldest].UsedFrame) oldest = i;
			if (WaterSnapshots[oldest].Colour) DeadTextures.push_back(WaterSnapshots[oldest].Colour);
			if (WaterSnapshots[oldest].Depth) DeadTextures.push_back(WaterSnapshots[oldest].Depth);
			WaterSnapshots[oldest] = fresh;
			snapshot = &WaterSnapshots[oldest];
		}
		else {
			WaterSnapshots.push_back(fresh);
			snapshot = &WaterSnapshots.back();
		}
	}
	// A demand declaration freezes both channels at one pre-water point. Missing channels
	// cannot be filled later from a source that may already contain water.
	if (!snapshot->Frozen) {
		SDL_GPUTextureCreateInfo info={};info.type=SDL_GPU_TEXTURETYPE_2D;
		info.format=(SDL_GPUTextureFormat)Scene_Format();info.usage=SDL_GPU_TEXTUREUSAGE_COLOR_TARGET|SDL_GPU_TEXTUREUSAGE_SAMPLER;
		info.width=source.Width;info.height=source.Height;info.layer_count_or_depth=info.num_levels=1;
		if(colour && !snapshot->Colour) { snapshot->Colour=SDL_CreateGPUTexture(GpuDevice,&info); Track_Colour(snapshot->Colour); }
		if(depth && !snapshot->Depth && source.Depth && DepthSampleable && Create_Scene_Depth_Pipeline()) {
			info.format=SDL_GPU_TEXTUREFORMAT_R32_FLOAT;snapshot->Depth=SDL_CreateGPUTexture(GpuDevice,&info);
		}
		{
			const int32_t rectangle[4]={0,0,(int32_t)source.Width,(int32_t)source.Height};
			if(colour && snapshot->Colour) {
				Record_Blit(source.Colour,rectangle,snapshot->Colour,rectangle,false);
				snapshot->ColourReady=true;++WaterColourCaptures;
			}
			if(depth && snapshot->Depth) {
				Command command={};command.IsDepthSnapshot=true;command.BlitSource=source.Depth;
				command.BlitDestination=snapshot->Depth;memcpy(command.Rect,rectangle,sizeof(rectangle));
				Commands.push_back(command);snapshot->DepthReady=true;++WaterDepthCaptures;
			}
			snapshot->CaptureSerial=++WaterCaptureSerial;snapshot->Frozen=true;
		}
	}
	snapshot->UsedFrame = WaterFrame;
	return snapshot;
}

void SdlGpuFrame::Prepare_Water_Snapshot(const SdlTarget &source, bool colour, bool depth)
{
	Water_Snapshot(source,colour,depth);
}

SDL_GPUTexture *SdlGpuFrame::Water_Background(const SdlTarget &source)
{
	WaterSnapshot *snapshot = Water_Snapshot(source,true,true);
	return snapshot && snapshot->ColourReady ? snapshot->Colour : NULL;
}

SDL_GPUTexture *SdlGpuFrame::Water_Depth(const SdlTarget &source)
{
	WaterSnapshot *snapshot = Water_Snapshot(source,true,true);
	return snapshot && snapshot->DepthReady ? snapshot->Depth : NULL;
}

SDL_GPUTexture *SdlGpuFrame::Scene_Depth(const SdlTarget &source)
{
	Prepare_Water_Snapshot(source,WaterPerformanceSettings::Applied().sceneColourRequired,true);
	SDL_GPUTexture *depth = Water_Depth(source);
	if (!depth) return NULL;
	// Publish the depth-to-colour capture before recording consumers. Queue ordering is enough;
	// do not wait for GPU idle.
	if (!Flush(FlushSceneDepth)) {
		Invalidate_Water_Snapshots(source.Colour, source.Depth);
		return NULL;
	}
	return depth;
}

SDL_GPUTexture *SdlGpuFrame::Ready_Scene_Depth(const SdlTarget &source, uint64_t &serial) const
{
	serial = 0;
	for (const WaterSnapshot &snapshot : WaterSnapshots) {
		if (snapshot.DepthReady && snapshot.Depth && snapshot.Source.Colour == source.Colour
			&& snapshot.Source.Depth == source.Depth && snapshot.Source.Width == source.Width
			&& snapshot.Source.Height == source.Height) {
			serial = snapshot.CaptureSerial;
			return snapshot.Depth;
		}
	}
	return NULL;
}

bool SdlGpuFrame::Create_Scene_Depth_Pipeline()
{
	if (SceneDepthPipeline && SceneDepthSampler) return true;
	std::string log;
	if (!SceneDepthVertex) {
		std::vector<unsigned char> spirv;
		if (!SDL3_Compile_HLSL_To_SPIRV(GAMMA_VERTEX_HLSL, true, spirv, log)
			|| !(SceneDepthVertex = SDL3_Create_Shader(GpuDevice, spirv, true, log))) {
			static bool reported = false;
			if (!reported) {
				reported = true;
				fprintf(stderr, "SdlGpuFrame: scene depth vertex: %s\n", log.c_str());
			}
			return false;
		}
	}
	if (!SceneDepthPixel) {
		std::vector<unsigned char> spirv;
		if (!SDL3_Compile_HLSL_To_SPIRV(SCENE_DEPTH_PIXEL_HLSL, false, spirv, log)
			|| !(SceneDepthPixel = SDL3_Create_Shader(GpuDevice, spirv, false, log))) {
			static bool reported = false;
			if (!reported) {
				reported = true;
				fprintf(stderr, "SdlGpuFrame: scene depth pixel: %s\n", log.c_str());
			}
			return false;
		}
	}
	if (!SceneDepthPipeline) {
		SDL_GPUColorTargetDescription target = {};
		target.format = SDL_GPU_TEXTUREFORMAT_R32_FLOAT;
		SDL_GPUGraphicsPipelineCreateInfo info = {};
		info.vertex_shader = SceneDepthVertex;
		info.fragment_shader = SceneDepthPixel;
		info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
		info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
		info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
		info.target_info.color_target_descriptions = &target;
		info.target_info.num_color_targets = 1;
		SceneDepthPipeline = SDL_CreateGPUGraphicsPipeline(GpuDevice, &info);
	}
	if (!SceneDepthSampler) {
		SDL_GPUSamplerCreateInfo sampler = {};
		sampler.min_filter = sampler.mag_filter = SDL_GPU_FILTER_NEAREST;
		sampler.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
		sampler.address_mode_u = sampler.address_mode_v = sampler.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
		SceneDepthSampler = SDL_CreateGPUSampler(GpuDevice, &sampler);
	}
	if (!SceneDepthPipeline || !SceneDepthSampler) {
		static bool reported = false;
		if (!reported) {
			reported = true;
			fprintf(stderr, "SdlGpuFrame: scene depth resolve: %s\n", SDL_GetError());
		}
	}
	return SceneDepthPipeline && SceneDepthSampler;
}

bool SdlGpuFrame::Resolve_Scene_Depth(SDL_GPUCommandBuffer *commands, SDL_GPUTexture *source,
	SDL_GPUTexture *destination, unsigned int width, unsigned int height)
{
	SDL_GPUColorTargetInfo colour = {};
	colour.texture = destination;
	colour.load_op = SDL_GPU_LOADOP_DONT_CARE;
	colour.store_op = SDL_GPU_STOREOP_STORE;
	#if defined(__APPLE__)
ZH_SDL_Metal_SetNextRenderPassTag(2u);
#endif
SDL_GPURenderPass *pass = SDL_BeginGPURenderPass(commands, &colour, 1, NULL);
	if (!pass) return false;
	SDL_BindGPUGraphicsPipeline(pass, SceneDepthPipeline);
	const SDL_GPUTextureSamplerBinding binding = { source, SceneDepthSampler };
	SDL_BindGPUFragmentSamplers(pass, 0, &binding, 1);
	const SDL_GPUViewport viewport = { 0, 0, (float)width, (float)height, 0, 1 };
	SDL_SetGPUViewport(pass, &viewport);
	SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);
	SDL_EndGPURenderPass(pass);
	return true;
}

bool SdlGpuFrame::Resize(unsigned int width, unsigned int height, ResizeFailure *failure)
{
	if (failure) *failure = RESIZE_NONE;
	if (!width || !height) {
		if (failure) *failure = RESIZE_INVALID_SIZE;
		return false;
	}
	// Submit before replacing anything: queued targets and mirror uploads still name the old resources.
	if (!Flush(FlushMaintenance)) {
		if (failure) *failure = RESIZE_FLUSH_FAILED;
		return false;
	}
	if (!Create_Targets(width, height)) {
		if (failure) *failure = RESIZE_ALLOCATION_FAILED;
		return false;
	}
	TargetSet = false;
	return true;
}

void SdlGpuFrame::Clear_Back_Buffer(bool colour, bool depth, bool stencil, uint32_t argb, float z, uint32_t stencil_value)
{
	const SdlTarget target = TargetSet ? CurrentTarget : Back_Buffer_Target();
	Invalidate_Water_Snapshots(colour ? target.Colour : NULL, depth ? target.Depth : NULL);
	if (!colour && !depth && !stencil) {
		return;
	}
	Command command;
	memset(&command, 0, sizeof(command));
	command.Colour = colour;
	command.Depth = depth;
	command.Stencil = stencil;
	command.Argb = argb;
	command.Z = z;
	command.StencilValue = stencil_value;
	command.Target = Target_Index();
	Commands.push_back(command);
}


bool SdlGpuFrame::Flush(FlushCause cause)
{
	if (ParticlePerfToggle::enabled() && cause < FlushCauseCount)
		++ParticleGpuFlushReasons[cause];
	const Uint64 start = SDL_GetTicksNS();
	ZH_IOS_WaitForForeground();
	if (Commands.empty() && Uploads.empty()) {
		End_Batch();
		FlushMs += (double)(SDL_GetTicksNS() - start) / 1.0e6;
		++Flushes;
		++FlushesTotal;
		return true;
	}
	SDL_GPUCommandBuffer * commands = SDL_AcquireGPUCommandBuffer(GpuDevice);
	if (commands == NULL) {
		return false;
	}
	const bool recorded = Record_Batch(commands);
	const bool submitted = SerializeSubmits ? Submit(commands) : Submit_Flush(commands);
	End_Batch();
	FlushMs += (double)(SDL_GetTicksNS() - start) / 1.0e6;
	++Flushes;
	++FlushesTotal;
	return submitted && recorded;
}

// A flush's submit, with a fence kept until the GPU is done: the ones done are let go, and past FlushLimit the
// oldest is waited for.  Present needs none of this, as the swapchain already bounds the frames in flight;
// flushes had no bound (a texture updated twice in one batch flushes, as does every restore after a reset).
bool SdlGpuFrame::Submit_Flush(SDL_GPUCommandBuffer * commands)
{
	ZH_IOS_WaitForForeground();
	const bool profile = ParticlePerfToggle::enabled();
	const Uint64 submitStart = profile ? SDL_GetTicksNS() : 0;
	SDL_GPUFence * fence = SDL_SubmitGPUCommandBufferAndAcquireFence(commands);
	++SubmitCallsTotal;
	if (profile) {
		ParticleGpuSubmitNs += SDL_GetTicksNS() - submitStart;
		++ParticleGpuSubmitCount;
	}
	if (fence == NULL) {
		return false;
	}
	size_t kept = 0;
	for (size_t i = 0; i < FlushFences.size(); ++i) {
		if (SDL_QueryGPUFence(GpuDevice, FlushFences[i])) {
			SDL_ReleaseGPUFence(GpuDevice, FlushFences[i]);
		} else {
			FlushFences[kept++] = FlushFences[i];
		}
	}
	FlushFences.resize(kept);
	FlushFences.push_back(fence);
	if (FlushFences.size() > FlushInFlightMost) {
		FlushInFlightMost = (unsigned int)FlushFences.size();
		if (FlushInFlightMost >= 8 && (FlushInFlightMost & (FlushInFlightMost - 1)) == 0) {
			fprintf(stderr, "SdlGpuFrame: %u mid-frame flushes in flight at once (flush %u)\n", FlushInFlightMost, FlushesTotal + 1);
		}
	}
	if (FlushLimit > 0 && FlushFences.size() > FlushLimit) {
		const Uint64 waitStart = profile ? SDL_GetTicksNS() : 0;
		SDL_WaitForGPUFences(GpuDevice, true, &FlushFences[0], 1);
		if (profile) {
			const Uint64 waitNs = SDL_GetTicksNS() - waitStart;
			ParticleGpuWaitNs += waitNs;
			if (waitNs > ParticleGpuMaxWaitNs) ParticleGpuMaxWaitNs = waitNs;
			ParticleGpuWaitAbove1Ms += waitNs >= 1000000ull;
			ParticleGpuWaitAbove5Ms += waitNs >= 5000000ull;
			ParticleGpuWaitAbove16Ms += waitNs >= 16000000ull;
		}
		// Also counted in FlushMs; do not add these times to derive total CPU frame time.
		SDL_ReleaseGPUFence(GpuDevice, FlushFences[0]);
		FlushFences.erase(FlushFences.begin());
		++FlushWaits;
	}
	return true;
}

bool SdlGpuFrame::Submit(SDL_GPUCommandBuffer * commands)
{
	if (!SerializeSubmits) {
		ZH_IOS_WaitForForeground();
		++SubmitCallsTotal;
		return SDL_SubmitGPUCommandBuffer(commands);
	}
	ZH_IOS_WaitForForeground();
	SDL_GPUFence * fence = SDL_SubmitGPUCommandBufferAndAcquireFence(commands);
	++SubmitCallsTotal;
	if (fence == NULL) {
		return false;
	}
	const Uint64 start = SDL_GetTicksNS();
	const bool waited = SDL_WaitForGPUFences(GpuDevice, true, &fence, 1);
	FenceMs += (double)(SDL_GetTicksNS() - start) / 1.0e6;
	SDL_ReleaseGPUFence(GpuDevice, fence);
	return waited;
}

void SdlGpuFrame::Set_Offscreen_Presents(unsigned int hz)
{
	OffscreenPresents = true;
	// 1 to 1000 a second; anything else (ZH_OFFSCREEN_HZ=-1 reads as 4294967295) is unpaced, never a
	// period of zero.
	OffscreenHz = hz <= 1000 ? hz : 0;
}

// -offscreen's submit: a fence for this frame, a wait for the one two frames back, then the pacer.
bool SdlGpuFrame::Submit_Offscreen(SDL_GPUCommandBuffer * commands)
{
	if (SerializeSubmits) {
		return Submit(commands);		// ZH_GPU_TIMING_SYNC: every submit waits for its fence already
	}
	ZH_IOS_WaitForForeground();
	SDL_GPUFence * fence = SDL_SubmitGPUCommandBufferAndAcquireFence(commands);
	++SubmitCallsTotal;
	if (fence == NULL) {
		return false;
	}
	const Uint64 start = SDL_GetTicksNS();
	SDL_GPUFence *& oldest = InFlight[InFlightNext];
	if (oldest != NULL) {
		SDL_WaitForGPUFences(GpuDevice, true, &oldest, 1);
		SDL_ReleaseGPUFence(GpuDevice, oldest);
	}
	oldest = fence;
	InFlightNext = (InFlightNext + 1) % 2;
	if (OffscreenHz > 0) {
		// The next tick after the last one, as vsync puts a frame on the next refresh: a late frame waits
		// for the tick after, and the ticks keep their phase.
		const Uint64 period = 1000000000ull / OffscreenHz;
		const Uint64 now = SDL_GetTicksNS();
		if (NextTickNs == 0) {
			NextTickNs = now;
		}
		NextTickNs += period;
		if (NextTickNs < now) {
			NextTickNs += ((now - NextTickNs) / period + 1) * period;
		}
		SDL_DelayPrecise(NextTickNs - now);
	}
	OffscreenMs += (double)(SDL_GetTicksNS() - start) / 1.0e6;
	return true;
}

void SdlGpuFrame::Take_Timing(double & flush_ms, double & fence_ms, unsigned int & flushes, double & acquire_ms,
	double & offscreen_ms, unsigned int & not_visible, unsigned int & not_shown)
{
	// Present spacing, including vsync. A few stores; the draw budget reads it
	// even when the particle log is off. Not a GPU-elapsed measurement.
	{
		static uint64_t lastPresentNs = 0;
		const uint64_t nowNs = SDL_GetTicksNS();
		if (lastPresentNs != 0 && nowNs > lastPresentNs)
			ParticlePerfStats::notePresentInterval(double(nowNs - lastPresentNs) / 1.0e6);
		lastPresentNs = nowNs;
	}
	flush_ms = FlushMs;
	fence_ms = FenceMs;
	flushes = Flushes;
	acquire_ms = AcquireMs;
	offscreen_ms = OffscreenMs;
	not_visible = NotVisible;
	NotVisible = 0;
	not_shown = NotShown;
	NotShown = 0;
	OffscreenMs = 0.0;
	FlushMs = 0.0;
	FenceMs = 0.0;
	Flushes = 0;
	AcquireMs = 0.0;
	const bool profiling = ParticlePerfToggle::enabled();
#if defined(__APPLE__)
	const char *gpuDriver = GpuDevice ? SDL_GetGPUDeviceDriver(GpuDevice) : NULL;
	const bool metalTimingAvailable = gpuDriver && strcmp(gpuDriver, "metal") == 0;
	if (metalTimingAvailable) ZH_SDL_Metal_SetTimingEnabled(profiling);
#else
	const bool metalTimingAvailable = false;
#endif
	if (profiling) {
		// A Take_Timing call normally represents one presentation interval. No per-frame stderr.
		if (++ParticleGpuPerfFrames >= 120) {
			Uint64 gpuCompletedNs = 0, gpuMaximumNs = 0;
			Uint32 gpuSamples = 0, gpuUnavailable = 0;
#if defined(__APPLE__)
			if (metalTimingAvailable)
				ZH_SDL_Metal_TakeTiming(&gpuCompletedNs, &gpuMaximumNs,
					&gpuSamples, &gpuUnavailable);
			Uint64 passNs[12] = {}, passMaxNs[12] = {};
			Uint32 passCounts[12] = {}, passMissing[12] = {};
			if (metalTimingAvailable)
				ZH_SDL_Metal_TakePassTiming(passNs, passMaxNs, passCounts, passMissing);
			Uint64 fragmentInvocations[12] = {}, fragmentsPassed[12] = {};
			Uint32 statsSamples[12] = {}, statsMissing[12] = {};
			if (metalTimingAvailable)
				ZH_SDL_Metal_TakeFragmentStatistics(fragmentInvocations,
					fragmentsPassed, statsSamples, statsMissing);
			double nanosecondsPerGpuTick = 0.0;
			if (metalTimingAvailable)
				ZH_SDL_Metal_GetTimestampScale(&nanosecondsPerGpuTick);
			fprintf(stderr, "[GPU_COUNTER_CALIBRATION] ns_per_gpu_tick=%.9f valid=%u source=Metal_sampleTimestamps_cpu_gpu_pairs note=pass_times_converted_only_when_valid\n",
				nanosecondsPerGpuTick, nanosecondsPerGpuTick > 0.0 ? 1u : 0u);
			const char *passNames[12] = {
				"unlabelled", "scene_backbuffer", "intermediate", "depth_only",
				"ocean_fft", "heat", "msaa_colour", "native_particles",
				"postprocess", "sun_smoke_shadow", "gamma", "msaa_depth"
			};
			for (unsigned p = 0; p < 12; ++p) {
				fprintf(stderr, "[GPU_PASS_PERF] bucket=%s completed=%u unavailable=%u gpu_ms=%.3f max_ms=%.3f stats_completed=%u stats_unavailable=%u fragment_invocations=%llu fragments_passed=%llu fragment_invocations_per_passed=%.3f note=async_gpu_stage_counters_pass_wide_not_transparent_only\n",
					passNames[p], passCounts[p], passMissing[p],
					double(passNs[p]) / 1.0e6, double(passMaxNs[p]) / 1.0e6,
					statsSamples[p], statsMissing[p],
					static_cast<unsigned long long>(fragmentInvocations[p]),
					static_cast<unsigned long long>(fragmentsPassed[p]),
					fragmentsPassed[p] ? double(fragmentInvocations[p]) / double(fragmentsPassed[p]) : 0.0);
			}
			const bool passTimes = nanosecondsPerGpuTick > 0.0;
			const auto overdraw = [](uint64_t invocations, uint64_t passed, uint32_t samples) {
				return (samples && passed) ? double(invocations) / double(passed) : -1.0;
			};
			ParticlePerfStats::publishGpuWindow(
				passTimes ? double(passNs[7]) / 1.0e6 : -1.0,
				passTimes ? double(passNs[3] + passNs[11]) / 1.0e6 : -1.0,
				passTimes ? double(passNs[5]) / 1.0e6 : -1.0,
				overdraw(fragmentInvocations[1], fragmentsPassed[1], statsSamples[1]),
				overdraw(fragmentInvocations[7], fragmentsPassed[7], statsSamples[7]),
				ParticleGpuPerfFrames);
#endif
			fprintf(stderr, "[GPU_SYNC_PERF] intervals=%u flush_limit=%u flushes=%u ring_waits=%u ring_wait_cpu_ms=%.3f ring_wait_max_ms=%.3f waits_ge_1ms=%u waits_ge_5ms=%u waits_ge_16ms=%u submit_calls=%u submit_cpu_ms=%.3f gpu_cmd_completed=%u gpu_cmd_unavailable=%u gpu_cmd_total_ms=%.3f gpu_cmd_max_ms=%.3f gpu_time_source=%s render_passes=%u depth_snapshots=%u blits=%u copy_passes=%u recorded_draws=%llu upload_mib=%.3f note=gpu_time_is_async_completed_command_buffer_sum_not_per_frame\n",
				ParticleGpuPerfFrames, FlushLimit, FlushesTotal - ParticleGpuPerfFlushStart,
				FlushWaits - ParticleGpuPerfWaitStart, double(ParticleGpuWaitNs) / 1.0e6,
				double(ParticleGpuMaxWaitNs) / 1.0e6,
				ParticleGpuWaitAbove1Ms, ParticleGpuWaitAbove5Ms, ParticleGpuWaitAbove16Ms,
				ParticleGpuSubmitCount, double(ParticleGpuSubmitNs) / 1.0e6,
				gpuSamples, gpuUnavailable, double(gpuCompletedNs) / 1.0e6,
				double(gpuMaximumNs) / 1.0e6,
				metalTimingAvailable ? "metal_GPUStartTime_GPUEndTime" : "unavailable",
				ParticleGpuRenderPasses, ParticleGpuDepthSnapshots, ParticleGpuBlits,
				ParticleGpuCopyPasses,
				static_cast<unsigned long long>(ParticleGpuRecordedDraws),
				double(ParticleGpuUploadBytes) / (1024.0 * 1024.0));
			const char *flushNames[FlushCauseCount] = {
				"unclassified", "batch_limit", "ocean", "scene_depth",
				"ambient_occlusion", "atmosphere", "heat", "msaa",
				"native_particles", "smoke", "sun_shadow", "maintenance"
			};
			fprintf(stderr, "[GPU_FLUSH_CAUSES] intervals=%u", ParticleGpuPerfFrames);
			for (unsigned i = 0; i < FlushCauseCount; ++i)
				fprintf(stderr, " %s=%llu", flushNames[i],
					static_cast<unsigned long long>(ParticleGpuFlushReasons[i]));
			fprintf(stderr, "\n");
			fprintf(stderr, "[GPU_TRANSPARENCY_PERF] blended_draws=%llu additive_draws=%llu backbuffer_blended_scissor_mpixels=%.3f backbuffer_scissor_fullscreen_equiv=%.3f note=scissor_upper_bound_not_actual_transparent_fragment_overdraw\n",
				static_cast<unsigned long long>(ParticleGpuBlendedDraws),
				static_cast<unsigned long long>(ParticleGpuAdditiveDraws),
				double(ParticleGpuBlendScissorPixels) / 1.0e6,
				BackWidth && BackHeight ? double(ParticleGpuBlendScissorPixels) /
					(double(BackWidth) * double(BackHeight)) : 0.0);
			ParticleGpuPerfFrames = 0;
			ParticleGpuPerfFlushStart = FlushesTotal;
			ParticleGpuPerfWaitStart = FlushWaits;
			ParticleGpuWaitNs = ParticleGpuMaxWaitNs = ParticleGpuSubmitNs = 0;
			ParticleGpuWaitAbove1Ms = ParticleGpuWaitAbove5Ms = ParticleGpuWaitAbove16Ms = 0;
			ParticleGpuSubmitCount = 0;
		ParticleGpuRecordedDraws = ParticleGpuUploadBytes = 0;
		ParticleGpuBlendedDraws = ParticleGpuAdditiveDraws = 0;
		ParticleGpuBlendScissorPixels = 0;
		ParticleGpuRenderPasses = ParticleGpuDepthSnapshots = ParticleGpuBlits = ParticleGpuCopyPasses = 0;
		memset(ParticleGpuFlushReasons, 0, sizeof(ParticleGpuFlushReasons));
		}
	} else {
		// Start a fresh diagnostic window if the runtime flag is enabled later.
		ParticleGpuPerfFrames = 0;
		ParticleGpuPerfFlushStart = FlushesTotal;
		ParticleGpuPerfWaitStart = FlushWaits;
		ParticleGpuWaitNs = ParticleGpuMaxWaitNs = ParticleGpuSubmitNs = 0;
		ParticleGpuWaitAbove1Ms = ParticleGpuWaitAbove5Ms = ParticleGpuWaitAbove16Ms = 0;
		ParticleGpuSubmitCount = 0;
		ParticleGpuRecordedDraws = ParticleGpuUploadBytes = 0;
		ParticleGpuBlendedDraws = ParticleGpuAdditiveDraws = 0;
		ParticleGpuBlendScissorPixels = 0;
		ParticleGpuRenderPasses = ParticleGpuDepthSnapshots = ParticleGpuBlits = ParticleGpuCopyPasses = 0;
		memset(ParticleGpuFlushReasons, 0, sizeof(ParticleGpuFlushReasons));
	}
}

SDL_GPUGraphicsPipeline * SdlGpuFrame::Gamma_Pipeline(unsigned int format)
{
	if (GammaPipeline != NULL && GammaPipelineFormat == format) {
		return GammaPipeline;
	}
	std::string log;
	if (GammaVertex == NULL) {
		std::vector<unsigned char> spirv;
		if (!SDL3_Compile_HLSL_To_SPIRV(GAMMA_VERTEX_HLSL, true, spirv, log)
			|| (GammaVertex = SDL3_Create_Shader(GpuDevice, spirv, true, log)) == NULL) {
			fprintf(stderr, "SdlGpuFrame: the gamma pass's vertex program: %s\n", log.c_str());
			return NULL;
		}
	}
	if (GammaPixel == NULL) {
		std::vector<unsigned char> spirv;
		if (!SDL3_Compile_HLSL_To_SPIRV(GAMMA_PIXEL_HLSL, false, spirv, log)
			|| (GammaPixel = SDL3_Create_Shader(GpuDevice, spirv, false, log)) == NULL) {
			fprintf(stderr, "SdlGpuFrame: the gamma pass's pixel program: %s\n", log.c_str());
			return NULL;
		}
	}
	if (GammaPipeline != NULL) {
		SDL_ReleaseGPUGraphicsPipeline(GpuDevice, GammaPipeline);
		GammaPipeline = NULL;
	}
	SDL_GPUColorTargetDescription target;
	SDL_zero(target);
	target.format = (SDL_GPUTextureFormat)format;
	SDL_GPUGraphicsPipelineCreateInfo info;
	SDL_zero(info);
	info.vertex_shader = GammaVertex;
	info.fragment_shader = GammaPixel;
	info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
	info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
	info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
	info.target_info.color_target_descriptions = &target;
	info.target_info.num_color_targets = 1;
	GammaPipeline = SDL_CreateGPUGraphicsPipeline(GpuDevice, &info);
	GammaPipelineFormat = format;
	if (GammaPipeline == NULL) {
		fprintf(stderr, "SdlGpuFrame: the gamma pass's pipeline: %s\n", SDL_GetError());
	}
	return GammaPipeline;
}

// The three ramps as a 256 x 1 R16G16B16A16 texture, uploaded only when they change.
bool SdlGpuFrame::Upload_Ramp(SDL_GPUCommandBuffer * commands, const uint16_t (*ramp)[256])
{
	std::vector<uint16_t> texels(256 * 4);
	for (int index = 0; index < 256; ++index) {
		texels[index * 4 + 0] = ramp[0][index];
		texels[index * 4 + 1] = ramp[1][index];
		texels[index * 4 + 2] = ramp[2][index];
		texels[index * 4 + 3] = 0xFFFF;
	}
	if (RampTexture != NULL && texels == UploadedRamp) {
		return true;
	}
	if (RampTexture == NULL) {
		SDL_GPUTextureCreateInfo info;
		SDL_zero(info);
		info.type = SDL_GPU_TEXTURETYPE_2D;
		info.format = SDL_GPU_TEXTUREFORMAT_R16G16B16A16_UNORM;
		info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
		info.width = 256;
		info.height = 1;
		info.layer_count_or_depth = 1;
		info.num_levels = 1;
		RampTexture = SDL_CreateGPUTexture(GpuDevice, &info);
		SDL_GPUSamplerCreateInfo sampler;
		SDL_zero(sampler);
		sampler.min_filter = SDL_GPU_FILTER_NEAREST;
		sampler.mag_filter = SDL_GPU_FILTER_NEAREST;
		sampler.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
		sampler.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
		sampler.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
		sampler.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
		PointSampler = SDL_CreateGPUSampler(GpuDevice, &sampler);
		if (RampTexture == NULL || PointSampler == NULL) {
			return false;
		}
	}
	const Uint32 bytes = (Uint32)(texels.size() * sizeof(uint16_t));
	SDL_GPUTransferBufferCreateInfo transfer_info;
	SDL_zero(transfer_info);
	transfer_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
	transfer_info.size = bytes;
	SDL_GPUTransferBuffer * transfer = SDL_CreateGPUTransferBuffer(GpuDevice, &transfer_info);
	if (transfer == NULL) {
		return false;
	}
	void * mapped = SDL_MapGPUTransferBuffer(GpuDevice, transfer, false);
	if (mapped == NULL) {
		SDL_ReleaseGPUTransferBuffer(GpuDevice, transfer);
		return false;
	}
	memcpy(mapped, &texels[0], bytes);
	SDL_UnmapGPUTransferBuffer(GpuDevice, transfer);
	SDL_GPUCopyPass * copy = SDL_BeginGPUCopyPass(commands);
	SDL_GPUTextureTransferInfo source;
	SDL_zero(source);
	source.transfer_buffer = transfer;
	SDL_GPUTextureRegion region;
	SDL_zero(region);
	region.texture = RampTexture;
	region.w = 256;
	region.h = 1;
	region.d = 1;
	SDL_UploadToGPUTexture(copy, &source, &region, false);
	SDL_EndGPUCopyPass(copy);
	SDL_ReleaseGPUTransferBuffer(GpuDevice, transfer);
	UploadedRamp = texels;
	return true;
}

bool SdlGpuFrame::Present_Into(SDL_GPUCommandBuffer * commands, SDL_GPUTexture * target, unsigned int width,
	unsigned int height, unsigned int format, const uint16_t (*ramp)[256])
{
	if (Sdl_Gamma_Is_Identity(ramp)) {
		SDL_GPUBlitInfo blit;
		SDL_zero(blit);
		blit.source.texture = BackBuffer;
		blit.source.w = BackWidth;
		blit.source.h = BackHeight;
		blit.destination.texture = target;
		blit.destination.w = width;
		blit.destination.h = height;
		blit.load_op = SDL_GPU_LOADOP_DONT_CARE;
		blit.filter = SDL_GPU_FILTER_LINEAR;
		SDL_BlitGPUTexture(commands, &blit);
		return true;
	}
	SDL_GPUGraphicsPipeline * pipeline = Gamma_Pipeline(format);
	if (pipeline == NULL || !Upload_Ramp(commands, ramp)) {
		return false;
	}
	SDL_GPUColorTargetInfo colour;
	SDL_zero(colour);
	colour.texture = target;
	colour.load_op = SDL_GPU_LOADOP_DONT_CARE;
	colour.store_op = SDL_GPU_STOREOP_STORE;
	#if defined(__APPLE__)
ZH_SDL_Metal_SetNextRenderPassTag(10u);
#endif
SDL_GPURenderPass * pass = SDL_BeginGPURenderPass(commands, &colour, 1, NULL);
	if (pass == NULL) {
		return false;
	}
	SDL_BindGPUGraphicsPipeline(pass, pipeline);
	SDL_GPUTextureSamplerBinding bindings[2];
	bindings[0].texture = BackBuffer;
	bindings[0].sampler = PointSampler;
	bindings[1].texture = RampTexture;
	bindings[1].sampler = PointSampler;
	SDL_BindGPUFragmentSamplers(pass, 0, bindings, 2);
	SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);
	SDL_EndGPURenderPass(pass);
	return true;
}

// The back buffer into the front copy, after the batch and before the picture goes out.
bool SdlGpuFrame::Copy_To_Front(SDL_GPUCommandBuffer * commands)
{
	SDL_GPUBlitInfo blit;
	SDL_zero(blit);
	blit.source.texture = BackBuffer;
	blit.source.w = BackWidth;
	blit.source.h = BackHeight;
	blit.destination.texture = FrontCopy;
	blit.destination.w = BackWidth;
	blit.destination.h = BackHeight;
	blit.load_op = SDL_GPU_LOADOP_DONT_CARE;
	blit.filter = SDL_GPU_FILTER_NEAREST;
	SDL_BlitGPUTexture(commands, &blit);
	return true;
}

bool SdlGpuFrame::Present(const uint16_t (*ramp)[256])
{
	Refresh_Water_Snapshots();
	ZH_IOS_WaitForForeground();
	SDL_GPUCommandBuffer * commands = SDL_AcquireGPUCommandBuffer(GpuDevice);
	if (commands == NULL) {
		return false;
	}
	bool ok = Record_Batch(commands) && Copy_To_Front(commands);
	if (Window != NULL) {
		SDL_GPUTexture * swapchain = NULL;
		Uint32 width = 0;
		Uint32 height = 0;
		// Asked before the acquire: SDL keeps these flags from the window's own events (on macOS, OCCLUDED from
		// NSWindow's occlusion state), and a window in any of them is not paced by the display.
		if ((SDL_GetWindowFlags(Window) & (SDL_WINDOW_HIDDEN | SDL_WINDOW_OCCLUDED | SDL_WINDOW_MINIMIZED)) != 0) {
			++NotVisible;
			++NotVisibleTotal;
		}
		// No texture (a minimised window) is not a failure: there is nothing to show this frame.
		const Uint64 acquire_start = SDL_GetTicksNS();
		const bool acquired = SDL_WaitAndAcquireGPUSwapchainTexture(commands, Window, &swapchain, &width, &height);
		AcquireMs += (double)(SDL_GetTicksNS() - acquire_start) / 1.0e6;
		if (acquired && swapchain != NULL) {
			if (width != ShownWidth || height != ShownHeight) {
				// What the picture is scaled to on its way out: on a Retina Mac, the window's pixels.
				fprintf(stderr, "SdlGpuFrame: the %ux%u back buffer is shown on a %ux%u swapchain\n", BackWidth, BackHeight,
					(unsigned)width, (unsigned)height);
				ShownWidth = width;
				ShownHeight = height;
			}
			ok = Present_Into(commands, swapchain, width, height, SDL_GetGPUSwapchainTextureFormat(GpuDevice, Window), ramp)
				&& ok;
		} else {
			// Nothing reaches the display and nothing waits for vsync, so this frame's time is not the
			// display's pacing: the timing report says how many there were.
			++NotShown;
			++NotShownTotal;
		}
	} else if (OffscreenPresents) {
		if (DisplayTexture == NULL) {
			SDL_GPUTextureCreateInfo info;
			SDL_zero(info);
			info.type = SDL_GPU_TEXTURETYPE_2D;
			info.format = BACK_BUFFER_FORMAT;
			info.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
			info.width = BackWidth;
			info.height = BackHeight;
			info.layer_count_or_depth = 1;
			info.num_levels = 1;
			info.sample_count = SDL_GPU_SAMPLECOUNT_1;
			DisplayTexture = SDL_CreateGPUTexture(GpuDevice, &info);
		}
		if (DisplayTexture != NULL) {
			ok = Present_Into(commands, DisplayTexture, BackWidth, BackHeight, BACK_BUFFER_FORMAT, ramp) && ok;
		}
		const bool submitted = Submit_Offscreen(commands);
		End_Batch();
		return submitted && ok && DisplayTexture != NULL;
	}
	const bool submitted = Submit(commands);
	End_Batch();
	return submitted && ok;
}

bool SdlGpuFrame::Present_To(SDL_GPUTexture * target, unsigned int width, unsigned int height,
	const uint16_t (*ramp)[256])
{
	Refresh_Water_Snapshots();
	ZH_IOS_WaitForForeground();
	SDL_GPUCommandBuffer * commands = SDL_AcquireGPUCommandBuffer(GpuDevice);
	if (commands == NULL) {
		return false;
	}
	bool ok = Record_Batch(commands) && Copy_To_Front(commands);
	ok = Present_Into(commands, target, width, height, BACK_BUFFER_FORMAT, ramp) && ok;
	const bool submitted = Submit(commands);
	End_Batch();
	return submitted && ok;
}

bool SdlGpuFrame::Upload_Back_Buffer(const std::vector<uint8_t> & bgra)
{
    if(!Flush(FlushMaintenance))return false;
    External_Colour_Write(BackBuffer);
	const Uint32 bytes = BackWidth * BackHeight * (HdrActive?8:4);
	if (bgra.size() != BackWidth*BackHeight*4) {
		return false;
	}
	SDL_GPUTransferBufferCreateInfo transfer_info;
	SDL_zero(transfer_info);
	transfer_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
	transfer_info.size = bytes;
	SDL_GPUTransferBuffer * transfer = SDL_CreateGPUTransferBuffer(GpuDevice, &transfer_info);
	if (transfer == NULL) {
		return false;
	}
	void * mapped = SDL_MapGPUTransferBuffer(GpuDevice, transfer, false);
	if (mapped == NULL) {
		SDL_ReleaseGPUTransferBuffer(GpuDevice, transfer);
		return false;
	}
	if(HdrActive)SdlColour::fromBgra(bgra.data(),(uint16_t*)mapped,BackWidth*BackHeight);
    else memcpy(mapped, &bgra[0], bytes);
	SDL_UnmapGPUTransferBuffer(GpuDevice, transfer);
	ZH_IOS_WaitForForeground();
	SDL_GPUCommandBuffer * commands = SDL_AcquireGPUCommandBuffer(GpuDevice);
	SDL_GPUCopyPass * copy = SDL_BeginGPUCopyPass(commands);
	SDL_GPUTextureTransferInfo source;
	SDL_zero(source);
	source.transfer_buffer = transfer;
	source.pixels_per_row = BackWidth;
	source.rows_per_layer = BackHeight;
	SDL_GPUTextureRegion region;
	SDL_zero(region);
	region.texture = BackBuffer;
	region.w = BackWidth;
	region.h = BackHeight;
	region.d = 1;
	SDL_UploadToGPUTexture(copy, &source, &region, false);
	SDL_EndGPUCopyPass(copy);
	ZH_IOS_WaitForForeground();
	++SubmitCallsTotal;
	const bool ok = SDL_SubmitGPUCommandBuffer(commands);
	SDL_ReleaseGPUTransferBuffer(GpuDevice, transfer);
	return ok;
}

bool SdlGpuFrame::Read_Bytes(SDL_GPUTexture * texture, unsigned int width, unsigned int height,unsigned bpp,std::vector<uint8_t> & bgra)
{
	if (!Flush(FlushMaintenance)) {
		return false;
	}
	const unsigned pitch=(width+63)&~63u;
    const Uint32 bytes = pitch * height * bpp;
	SDL_GPUTransferBufferCreateInfo transfer_info;
	SDL_zero(transfer_info);
	transfer_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
	transfer_info.size = bytes;
	SDL_GPUTransferBuffer * transfer = SDL_CreateGPUTransferBuffer(GpuDevice, &transfer_info);
	if (transfer == NULL) {
		return false;
	}
	ZH_IOS_WaitForForeground();
	SDL_GPUCommandBuffer * commands = SDL_AcquireGPUCommandBuffer(GpuDevice);
	SDL_GPUCopyPass * copy = SDL_BeginGPUCopyPass(commands);
	SDL_GPUTextureRegion region;
	SDL_zero(region);
	region.texture = texture;
	region.w = width;
	region.h = height;
	region.d = 1;
	SDL_GPUTextureTransferInfo destination;
	SDL_zero(destination);
	destination.transfer_buffer = transfer;
	destination.pixels_per_row = pitch;
	destination.rows_per_layer = height;
	SDL_DownloadFromGPUTexture(copy, &region, &destination);
	SDL_EndGPUCopyPass(copy);
	ZH_IOS_WaitForForeground();
	SDL_GPUFence * fence = SDL_SubmitGPUCommandBufferAndAcquireFence(commands);
	++SubmitCallsTotal;
	bool ok = fence != NULL && SDL_WaitForGPUFences(GpuDevice, true, &fence, 1);
	if (fence != NULL) {
		SDL_ReleaseGPUFence(GpuDevice, fence);
	}
	if (ok) {
		const void * mapped = SDL_MapGPUTransferBuffer(GpuDevice, transfer, false);
		ok = mapped != NULL;
		if (ok) {
			bgra.resize(width*height*bpp);
            for(unsigned y=0;y<height;++y)memcpy(bgra.data()+y*width*bpp,(const uint8_t*)mapped+y*pitch*bpp,width*bpp);
			SDL_UnmapGPUTransferBuffer(GpuDevice, transfer);
		}
	}
	SDL_ReleaseGPUTransferBuffer(GpuDevice, transfer);
	return ok;
}

void SdlGpuFrame::Release_Texture(SDL_GPUTexture *texture){if(texture){
    auto colour=SampleColours.find(texture);if(colour!=SampleColours.end()){SDL_ReleaseGPUTexture(GpuDevice,colour->second.Texture);SampleColours.erase(colour);}
    auto depth=SampleDepths.find(texture);if(depth!=SampleDepths.end()){SDL_ReleaseGPUTexture(GpuDevice,depth->second.Texture);SampleDepths.erase(depth);}
HdrTextures.erase(texture);SDL_ReleaseGPUTexture(GpuDevice,texture);}}
bool SdlGpuFrame::Read_Back(SDL_GPUTexture *texture,unsigned width,unsigned height,std::vector<uint8_t>&bgra)
{
    std::vector<uint8_t> bytes;if(!Read_Bytes(texture,width,height,Is_Hdr_Texture(texture)?8:4,bytes))return false;
    if(!Is_Hdr_Texture(texture)){bgra=std::move(bytes);return true;}
    bgra.resize(width*height*4);SdlColour::toBgra((const uint16_t*)bytes.data(),bgra.data(),width*height);return true;
}
bool SdlGpuFrame::Read_Float_Colour(SDL_GPUTexture *texture,unsigned width,unsigned height,std::vector<float>&colour)
{
    if(!Is_Hdr_Texture(texture))return false;
    std::vector<uint8_t> bytes;if(!Read_Bytes(texture,width,height,8,bytes))return false;
    colour.resize(width*height*4);const uint16_t *halves=(const uint16_t*)bytes.data();
    for(unsigned i=0;i<colour.size();++i)colour[i]=SdlColour::real(halves[i]);return true;
}
void SdlGpuFrame::Convert_Colour_Upload(SDL_GPUTexture *texture,unsigned width,unsigned height,uint32_t &offset)
{
    if(!Is_Hdr_Texture(texture))return;
    std::vector<uint8_t> bgra(width*height*4);memcpy(bgra.data(),UploadBytes.data()+offset,bgra.size());
    uint32_t native;auto *destination=Upload_Space(width*height*8,native);
    SdlColour::fromBgra(bgra.data(),(uint16_t*)destination,width*height);offset=native;
}
