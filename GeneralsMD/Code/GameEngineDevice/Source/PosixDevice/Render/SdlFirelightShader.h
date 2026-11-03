/*
** Command & Conquer Generals Zero Hour(tm)
** Copyright 2025 Electronic Arts Inc.
**
** This program is free software: you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation, either version 3 of the License, or
** (at your option) any later version.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with this program. If not, see <http://www.gnu.org/licenses/>.
*/
// Native opaque fire lighting, adapted from ffshader.h's BLAST_LIGHT_SAMPLING.
#pragma once
#include "Platform/SceneEffects.h"
namespace SdlFirelight {
struct Constants {
    float WorldFromClip[16];
    float Viewport[4],DepthInfo[4],Count[4];
    SceneEffects::PointLight Lights[SceneEffects::MaxFirelights];
};
static_assert(sizeof(Constants)==688,"firelight uniform packing");
inline const char *PixelShader(){return R"(
Texture2D<float> Depth:register(t0,space2);
SamplerState DepthSampler:register(s0,space2);
struct PointLight {float4 Position,Diffuse,Ambient;};
cbuffer FirelightConstants:register(b0,space3) {
    row_major float4x4 WorldFromClip;
    float4 Viewport,DepthInfo,Count;
    PointLight Lights[12];
};
float3 world_at(float2 ndc,float raw) {
    float4 world=mul(float4(ndc,raw,1),WorldFromClip);return world.xyz/world.w;
}
float4 main(float4 position:SV_Position):SV_Target {
    float2 ndc=float2((position.x-Viewport.x)/Viewport.z*2-1,1-(position.y-Viewport.y)/Viewport.w*2);
    float raw=Depth.SampleLevel(DepthSampler,position.xy*DepthInfo.xy,0).r;
    float3 here=world_at(ndc,(raw-DepthInfo.z)*DepthInfo.w),eye=world_at(ndc,0);
    // Derivatives must execute for the complete quad, including skyline helper lanes.
    float3 facing=cross(ddx(here),ddy(here));
    float3 normal=facing*rsqrt(max(dot(facing,facing),1e-20));
    if(dot(normal,eye-here)<0)normal=-normal;
    if(raw>=1||any(position.xy<Viewport.xy)||any(position.xy>=Viewport.xy+Viewport.zw))return 0;
    float3 light=0;
    [loop]for(int i=0;i<min((int)Count.x,12);++i) {
        float3 to=Lights[i].Position.xyz-here;float distance=length(to);
        float reach=Lights[i].Position.w;if(distance>=reach)continue;
        float inner=Lights[i].Diffuse.w;
        float fall=saturate(1-(distance-inner)/max(reach-inner,0.001));
        float shade=saturate(dot(normal,to/max(distance,0.001)));
        light+=fall*(shade*Lights[i].Diffuse.rgb+Lights[i].Ambient.rgb);
    }
    float peak=max(light.r,max(light.g,light.b)),over=max(peak-0.5,0);
    float knee=min(peak,0.5)+0.25*over/(over+0.25);
    light*=knee/max(peak,0.0001);
    return float4(light,0); // dst + dst*gain preserves black shroud and source alpha.
}
)";}
}
