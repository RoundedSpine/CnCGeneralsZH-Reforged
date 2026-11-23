// Camera-only motion for a later temporal upscale. Vectors are current minus previous
// in scene pixels: +X right, +Y down (texture space, the same Y the scene target uses).
// A static pixel's vector is where it moved because the camera moved. Dynamic objects are
// not represented. Distributed under the repository's GPL-3.0 terms; see LICENSE.md.
#pragma once

#include "SdlRenderMatrix.h"

#include <cmath>
#include <cstring>

namespace CameraMotion
{
	// The camera that produced this frame's depth. ClipFromWorld is view * projection,
	// row-major, the same product Ambient_Occlusion inverts. Viewport is the rectangle
	// the projection covers, in target pixels. Width and Height are the depth target.
	struct View
	{
		float ClipFromWorld[16];
		float MinZ;
		float MaxZ;
		float Viewport[4];
		unsigned Width;
		unsigned Height;
	};

	// DepthRemap.x is 1 when PrevClipFromWorld is the previous frame, else 0 and the
	// vector is defined to be zero (first frame, resize, viewport change).
	struct Constants
	{
		float WorldFromClip[16];
		float PrevClipFromWorld[16];
		float Viewport[4];
		float DepthRemap[4]; // x history, y unused, z window-depth minimum, w 1/(MaxZ-MinZ)
		float TargetSize[4]; // xy depth-target pixels
	};
	static_assert(sizeof(Constants) == 176, "camera-motion uniforms must match HLSL");

	inline bool Make(const View &current, const View *previous, Constants &out)
	{
		std::memset(&out, 0, sizeof(out));
		if (!current.Width || !current.Height || current.Viewport[2] <= 0.0f || current.Viewport[3] <= 0.0f)
			return false;
		const float range = current.MaxZ - current.MinZ;
		if (!std::isfinite(range) || range <= 1.0e-8f)
			return false;
		for (unsigned i = 0; i < 16; ++i)
			if (!std::isfinite(current.ClipFromWorld[i]))
				return false;
		if (!render_inverse(current.ClipFromWorld, out.WorldFromClip))
			return false;
		std::memcpy(out.Viewport, current.Viewport, sizeof(out.Viewport));
		out.DepthRemap[2] = current.MinZ;
		out.DepthRemap[3] = 1.0f / range;
		out.TargetSize[0] = static_cast<float>(current.Width);
		out.TargetSize[1] = static_cast<float>(current.Height);
		if (!previous || previous->Width != current.Width || previous->Height != current.Height)
			return true;
		for (unsigned i = 0; i < 4; ++i)
			if (previous->Viewport[i] != current.Viewport[i])
				return true;
		for (unsigned i = 0; i < 16; ++i)
			if (!std::isfinite(previous->ClipFromWorld[i]))
				return true;
		std::memcpy(out.PrevClipFromWorld, previous->ClipFromWorld, sizeof(out.PrevClipFromWorld));
		out.DepthRemap[0] = 1.0f;
		return true;
	}

	// pixelX/pixelY are SV_Position: target pixels, origin top-left, pixel centres at n+0.5.
	// rawDepth is the window-space depth the R32 resolve stores (0 near, 1 far at the default range).
	inline void Vector(const Constants &c, float pixelX, float pixelY, float rawDepth, float &motionX, float &motionY)
	{
		motionX = motionY = 0.0f;
		if (c.DepthRemap[0] < 0.5f || c.Viewport[2] <= 0.0f || c.Viewport[3] <= 0.0f || c.TargetSize[0] <= 0.0f)
			return;
		if (pixelX < c.Viewport[0] || pixelY < c.Viewport[1]
			|| pixelX >= c.Viewport[0] + c.Viewport[2] || pixelY >= c.Viewport[1] + c.Viewport[3])
			return;
		if (!std::isfinite(rawDepth) || rawDepth >= 0.99999f)
			return;
		const float uvX = (pixelX - c.Viewport[0]) / c.Viewport[2];
		const float uvY = (pixelY - c.Viewport[1]) / c.Viewport[3];
		const float clipZ = (rawDepth - c.DepthRemap[2]) * c.DepthRemap[3];
		const float ndcX = uvX * 2.0f - 1.0f;
		const float ndcY = 1.0f - uvY * 2.0f;
		float worldH[4];
		worldH[0] = ndcX * c.WorldFromClip[0] + ndcY * c.WorldFromClip[4] + clipZ * c.WorldFromClip[8] + c.WorldFromClip[12];
		worldH[1] = ndcX * c.WorldFromClip[1] + ndcY * c.WorldFromClip[5] + clipZ * c.WorldFromClip[9] + c.WorldFromClip[13];
		worldH[2] = ndcX * c.WorldFromClip[2] + ndcY * c.WorldFromClip[6] + clipZ * c.WorldFromClip[10] + c.WorldFromClip[14];
		worldH[3] = ndcX * c.WorldFromClip[3] + ndcY * c.WorldFromClip[7] + clipZ * c.WorldFromClip[11] + c.WorldFromClip[15];
		if (!std::isfinite(worldH[3]) || std::fabs(worldH[3]) < 1.0e-8f)
			return;
		const float invW = 1.0f / worldH[3];
		const float worldX = worldH[0] * invW;
		const float worldY = worldH[1] * invW;
		const float worldZ = worldH[2] * invW;
		const float *p = c.PrevClipFromWorld;
		const float prevX = worldX * p[0] + worldY * p[4] + worldZ * p[8] + p[12];
		const float prevY = worldX * p[1] + worldY * p[5] + worldZ * p[9] + p[13];
		const float prevW = worldX * p[3] + worldY * p[7] + worldZ * p[11] + p[15];
		if (!std::isfinite(prevW) || prevW <= 1.0e-6f)
			return;
		const float prevNdcX = prevX / prevW;
		const float prevNdcY = prevY / prevW;
		const float prevUvX = prevNdcX * 0.5f + 0.5f;
		const float prevUvY = 0.5f - prevNdcY * 0.5f;
		motionX = (uvX - prevUvX) * c.Viewport[2];
		motionY = (uvY - prevUvY) * c.Viewport[3];
		if (!std::isfinite(motionX) || !std::isfinite(motionY))
			motionX = motionY = 0.0f;
	}

	// Transcription of Vector(). Keep the two together: the CPU test locks this formula.
	inline const char *VertexShader()
	{
		return R"(
struct Output { float4 Position : SV_Position; };
Output main(uint id : SV_VertexID) {
    Output o; o.Position=float4(id==1?3.0:-1.0,id==2?3.0:-1.0,0,1); return o;
}
)";
	}

	inline const char *PixelShader()
	{
		return R"(
cbuffer CameraMotion : register(b0,space3) {
    row_major float4x4 WorldFromClip;
    row_major float4x4 PrevClipFromWorld;
    float4 Viewport;
    float4 DepthRemap;
    float4 TargetSize;
};
Texture2D<float> Depth : register(t0,space2);
SamplerState Point : register(s0,space2);
float4 main(float4 position : SV_Position) : SV_Target0 {
    if (DepthRemap.x < 0.5) return 0;
    if (position.x < Viewport.x || position.y < Viewport.y
        || position.x >= Viewport.x + Viewport.z || position.y >= Viewport.y + Viewport.w)
        return 0;
    float raw = Depth.SampleLevel(Point, position.xy / TargetSize.xy, 0);
    if (raw >= 0.99999) return 0;
    float2 uv = (position.xy - Viewport.xy) / Viewport.zw;
    float clipZ = (raw - DepthRemap.z) * DepthRemap.w;
    float4 worldH = mul(float4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, clipZ, 1.0), WorldFromClip);
    if (abs(worldH.w) < 1e-8) return 0;
    float3 world = worldH.xyz / worldH.w;
    float4 prev = mul(float4(world, 1.0), PrevClipFromWorld);
    if (prev.w <= 1e-6) return 0;
    float2 prevNdc = prev.xy / prev.w;
    float2 prevUv = float2(prevNdc.x * 0.5 + 0.5, 0.5 - prevNdc.y * 0.5);
    float2 motion = (uv - prevUv) * Viewport.zw;
    if (motion.x != motion.x || motion.y != motion.y) return 0;
    return float4(motion, 0.0, 0.0);
}
)";
	}
}
