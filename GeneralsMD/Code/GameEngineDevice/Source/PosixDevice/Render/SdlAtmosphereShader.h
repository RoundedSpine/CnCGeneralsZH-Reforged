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
// Native adaptation of WW3D2/dx11post.cpp's horizon/sky and ground haze.
#pragma once
#include "Platform/SceneEffects.h"
#include "SdlAoShader.h"
namespace SdlAtmosphere {
struct Constants {
    float WorldFromClip[16];
    float Viewport[4];
    float DepthInfo[4]; // inverse texture extent, viewport minimum and inverse range
    SceneEffects::AtmosphereParameters Parameters;
    float ShroudRect[4], Flags[4]; // x shroud, y sky
};
static_assert(sizeof(Constants)==240,"atmosphere uniform packing");
inline const char *VertexShader() { return SdlAo::VertexShader(); }
inline const char *PixelShader() { return R"(
Texture2D<float> Depth : register(t0,space2);
SamplerState DepthSampler : register(s0,space2);
Texture2D Shroud : register(t1,space2);
SamplerState ShroudSampler : register(s1,space2);
Texture2D Sky : register(t2,space2);
SamplerState SkySampler : register(s2,space2);
cbuffer AtmosphereConstants : register(b0,space3) {
    row_major float4x4 WorldFromClip;
    float4 Viewport,DepthInfo;
    float4 Haze,HazeColour,HazeSun,Fade,Horizon,Tint,MapSize;
    float4 ShroudRect,Flags;
};
float3 world_at(float2 ndc,float raw) {
    float4 world=mul(float4(ndc,raw,1),WorldFromClip);
    return world.xyz/world.w;
}
float3 horizon_at(float up) {
    return Horizon.rgb*lerp(0.55,1.0,saturate(1+up/0.25));
}
float haze_hash(float2 p) {return frac(sin(dot(p,float2(127.1,311.7)))*43758.5453);}
float haze_noise(float2 p) {
    float2 cell=floor(p),f=frac(p);f=f*f*(3-2*f);
    return lerp(lerp(haze_hash(cell),haze_hash(cell+float2(1,0)),f.x),
        lerp(haze_hash(cell+float2(0,1)),haze_hash(cell+float2(1,1)),f.x),f.y);
}
float4 main(float4 position:SV_Position):SV_Target {
    if(any(position.xy<Viewport.xy)||any(position.xy>=Viewport.xy+Viewport.zw)) return 0;
    float2 ndc=float2((position.x-Viewport.x)/Viewport.z*2-1,1-(position.y-Viewport.y)/Viewport.w*2);
    float raw=Depth.SampleLevel(DepthSampler,position.xy*DepthInfo.xy,0).r;
    float3 eye=world_at(ndc,0),farPoint=world_at(ndc,1);
    float3 farRay=farPoint-eye;
    float farReach=max(length(farRay),0.0001),farUp=farRay.z/farReach;
    if(raw>=1.0) {
        if(Fade.z<=0||Flags.y==0) return 0;
        float skyOpen=smoothstep(0,0.30,farUp);
        float2 plane=farRay.xy/(max(farUp,0.05)*farReach)*0.35;
        float3 sky=Sky.SampleLevel(SkySampler,plane,3*(1-skyOpen)).rgb*Tint.rgb;
        return float4(lerp(horizon_at(farUp),sky,skyOpen),1);
    }
    float3 here=world_at(ndc,(raw-DepthInfo.z)*DepthInfo.w),ray=here-eye;
    float reach=max(length(ray),0.0001);
    float lit=Flags.x!=0?Shroud.SampleLevel(ShroudSampler,(here.xy-ShroudRect.xy)*ShroudRect.zw,0).g:1;
    float hazeAmount=0;float3 hazeColour=0;
    if(Haze.z>0) {
        float below=saturate((Haze.x-here.z)/max(Haze.y,0.0001));
        float layer=min(below*below*(3-2*below),0.5);
        hazeAmount=Haze.z*layer*(0.65+0.35*saturate(reach/max(Haze.w,0.0001)));
        float bank=saturate((here.z-HazeColour.w+2)/5);bank=bank*bank*(3-2*bank);
        hazeAmount*=bank;
        float entry=eye.z>Haze.x&&abs(ray.z)>0.0001?saturate((Haze.x-eye.z)/ray.z):0;
        float patches=0;
        for(int tap=0;tap<4;++tap) {
            float3 at=eye+ray*lerp(entry,1,((float)tap+0.5)*0.25);
            float2 q=at.xy/320+at.z*0.011;
            patches+=haze_noise(q)*0.7+haze_noise(q*2.7+17)*0.3;
        }
        float dense=smoothstep(0.28,0.72,patches*0.25);
        hazeAmount*=lerp(0.65,1.35,dense);
        float2 look=ray.xy*rsqrt(max(dot(ray.xy,ray.xy),0.0001));
        float2 sun=HazeSun.xy*rsqrt(max(dot(HazeSun.xy,HazeSun.xy),0.0001));
        hazeColour=HazeColour.rgb*(1+HazeSun.w*dot(look,sun)*float3(0.08,0.02,-0.07));
        hazeColour*=(1+0.06*(dense-0.5))*lit;
        hazeAmount=saturate(hazeAmount*lit*lit*lit);
    }
    float atmosphereAmount=0;float3 atmosphereColour=0;
    if(Fade.z>0) {
        float3 centreEye=world_at(float2(0,0),0),axis=world_at(float2(0,0),1)-centreEye;
        float axisLength=max(length(axis),0.0001);
        float focus=axis.z<0?min((Fade.w-centreEye.z)/axis.z*axisLength,2000):2000;
        float past=max(reach-max(focus*1.1,Fade.x),0);
        atmosphereAmount=Fade.z*(1-exp(-past/max(Fade.y,0.0001)));
        float2 outside=max(-here.xy,here.xy-MapSize.xy);
        float closing=max(smoothstep(0.85,1,dot(ray,axis)/(axisLength*axisLength)),
            smoothstep(-20,20,max(outside.x,outside.y)));
        atmosphereColour=horizon_at(ray.z/reach)*lerp(lit,1,closing);
        atmosphereAmount=saturate(max(atmosphereAmount,closing));
    }
    // Equivalent to haze, then atmosphere, with one RGB-only alpha blend.
    float alpha=atmosphereAmount+hazeAmount*(1-atmosphereAmount);
    float3 premultiplied=atmosphereColour*atmosphereAmount+hazeColour*hazeAmount*(1-atmosphereAmount);
    return float4(premultiplied/max(alpha,0.0001),alpha);
}
)"; }
}
