/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
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

// Render-only SSAO, following dx11post.cpp's world AO radius, taps and filtering.
// Adapted from the GPL-3.0-or-later Windows implementation by İlyas Akın.
#pragma once
#include <string>
namespace SdlAo {
struct Constants {
    float WorldFromClip[16];
    float Viewport[4]; // xy origin, zw extent in full-resolution pixels
    float TexelSize[4]; // xy reciprocal source extent, zw source extent
    float Projection[4]; // xy projection scale, z viewport depth minimum, w inverse depth range
    float Blur[4];
    float ShroudRect[4], Shroud[4];
};
static_assert(sizeof(Constants)==160,"AO uniforms must match HLSL");
inline const char *VertexShader() { return R"(
struct Output { float4 Position : SV_Position; };
Output main(uint id : SV_VertexID) {
    Output o; o.Position=float4(id==1?3.0:-1.0,id==2?3.0:-1.0,0,1); return o;
}
)"; }
inline std::string PixelShader(unsigned pass) {
    const std::string common=R"(
cbuffer AoConstants : register(b0,space3) {
    row_major float4x4 WorldFromClip;
    float4 Viewport, TexelSize, Projection, Blur, ShroudRect, Shroud;
};
Texture2D<float4> Source : register(t0,space2);
SamplerState SourceSampler : register(s0,space2);
Texture2D<float> Depth : register(t1,space2);
SamplerState DepthSampler : register(s1,space2);
float raw_at(int2 p) {
    p=clamp(p,int2(0,0),int2(Blur.zw)-1);
    return Depth.SampleLevel(DepthSampler,(float2(p)+0.5)/Blur.zw,0);
}
float clip_depth(float raw) { return (raw-Projection.z)*Projection.w; }
float eye_distance(float raw) {
    return 1.0/max(clip_depth(raw)*WorldFromClip[2][3]+WorldFromClip[3][3],1e-8);
}
float3 world_at(int2 p,float raw) {
    float2 uv=(float2(p)+0.5-Viewport.xy)/Viewport.zw;
    float4 world=mul(float4(uv.x*2-1,1-uv.y*2,clip_depth(raw),1),WorldFromClip);
    return world.xyz/world.w;
}
float3 world_of(int2 p) { return world_at(p,raw_at(p)); }
float3 shorter(float3 a,float3 b) { return dot(a,a)<dot(b,b)?a:b; }
float2 ao_at(int2 p) {
    p=clamp(p,int2(0,0),int2(TexelSize.zw)-1);
    return Source.SampleLevel(SourceSampler,(float2(p)+0.5)*TexelSize.xy,0).rg;
}
)";
    if(pass==0) return common+R"(
float4 main(float4 position:SV_Position):SV_Target0 {
    int2 texel=min(int2(position.xy)*2+1,int2(Blur.zw)-1);
    float raw=raw_at(texel), distance_along=eye_distance(raw);
    if(raw>=0.99999 || any(float2(texel)<Viewport.xy) || any(float2(texel)>=Viewport.xy+Viewport.zw))
        return float4(1,distance_along,0,0);
    float3 here=world_at(texel,raw);
    float3 across=shorter(here-world_of(texel-int2(1,0)),world_of(texel+int2(1,0))-here);
    float3 down=shorter(here-world_of(texel-int2(0,1)),world_of(texel+int2(0,1))-here);
    float3 facing=cross(across,down);
    facing*=rsqrt(max(dot(facing,facing),1e-12));
    if(dot(facing,world_at(texel,0)-here)<0) facing=-facing;
    float2 reach=22.0*0.5*Projection.xy*Viewport.zw/distance_along;
    reach*=min(1.0,120.0/max(reach.y,1e-5));
    float turn=frac(52.9829189*frac(dot(position.xy,float2(0.06711056,0.00583715))))*6.2831853;
    float closed=0;
    for(int tap=0;tap<14;++tap) {
        float angle=turn+2.39996*float(tap);
        int2 at=texel+int2(float2(cos(angle),sin(angle))*reach*((float(tap)+0.5)/14.0));
        if(any(float2(at)<Viewport.xy)||any(float2(at)>=Viewport.xy+Viewport.zw)||raw_at(at)>=0.99999) continue;
        float3 to=world_of(at)-here;
        float squared=dot(to,to), fade=saturate(1-squared/(22.0*22.0));
        closed+=fade*fade*saturate(dot(to,facing)*rsqrt(squared+0.01)-0.12);
    }
    float kept=saturate(1-2.8*closed/14.0);
    float lit=1;
    if(Shroud.x>0) lit=Source.SampleLevel(SourceSampler,(here.xy-ShroudRect.xy)*ShroudRect.zw,0).g;
    return float4(lerp(1.0,kept,lit*lit*lit),distance_along,0,0);
}
)";
    if(pass==1) return common+R"(
float4 main(float4 position:SV_Position):SV_Target0 {
    static const float weights[5]={0.2,0.18,0.14,0.09,0.05};
    int2 texel=int2(position.xy),step=int2(Blur.xy);
    float2 centre=ao_at(texel);
    float allowed=max(40.0*centre.g/(Projection.y*Viewport.w),1e-5);
    float sum=centre.r*weights[0], total=weights[0];
    for(int i=1;i<5;++i) {
        float2 a=ao_at(texel+step*i), b=ao_at(texel-step*i);
        float wa=weights[i]*saturate(1-abs(a.g-centre.g)/allowed);
        float wb=weights[i]*saturate(1-abs(b.g-centre.g)/allowed);
        sum+=a.r*wa+b.r*wb; total+=wa+wb;
    }
    return float4(sum/total,centre.g,0,0);
}
)";
    return common+R"(
float4 main(float4 position:SV_Position):SV_Target0 {
    int2 texel=int2(position.xy);
    if(raw_at(texel)>=0.99999 || any(position.xy<Viewport.xy)||any(position.xy>=Viewport.xy+Viewport.zw)) return 1;
    float wanted=eye_distance(raw_at(texel));
    float allowed=max(20.0*wanted/(Projection.y*Viewport.w),1e-5);
    float2 at=position.xy*0.5-0.75;
    int2 corner=int2(floor(at));
    float2 part=frac(at);
    float sum=0,total=0;
    for(int y=0;y<2;++y) for(int x=0;x<2;++x) {
        float2 tap=ao_at(corner+int2(x,y));
        float bilinear=(x?part.x:1-part.x)*(y?part.y:1-part.y);
        float weight=bilinear*(0.001+saturate(1-abs(tap.g-wanted)/allowed));
        sum+=tap.r*weight; total+=weight;
    }
    return float4((sum/max(total,1e-8)).xxx,1);
}
)";
}
}
