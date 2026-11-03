// Native scene tone/grade, adapted from dx11post.cpp (GPL-3.0-or-later).
#pragma once
namespace SdlPost {
inline const char *Pixel(){return R"(
Texture2D<float4> Source:register(t0,space2);
SamplerState Point:register(s0,space2);
cbuffer Post:register(b0,space3){float4 Rect;float4 Options;};
float3 tone(float3 colour){
    float peak=max(colour.r,max(colour.g,colour.b));
    if(peak>1)colour*=(1+0.3*log2(peak))/peak;
    float knee=0.8,headroom=1-knee;
    return min(colour,knee)+headroom*(1-exp(-max(colour-knee,0)/headroom));
}
float3 grade(float3 colour){
    float3 c=saturate(colour);float pivot=0.28,k=1.18;
    float3 low=pivot*pow(max(c,0.00001)/pivot,k);
    float3 high=1-(1-pivot)*pow(max(1-c,0.00001)/(1-pivot),k);
    float3 graded=(c<pivot)?low:high;
    float grey=dot(graded,float3(0.299,0.587,0.114));
    graded=lerp(grey.xxx,graded,0.86)+float3(1,0.25,-1)*(grey-pivot)*0.05;
    return lerp(c,saturate(graded),Options.y);
}
float4 main(float4 pos:SV_Position):SV_Target{
    if(any(pos.xy<Rect.xy)||any(pos.xy>=Rect.xy+Rect.zw))discard;
    float4 c=Source.SampleLevel(Point,pos.xy*Options.zw,0);
    if(Options.x>0.5)c.rgb=tone(max(c.rgb,0));
    if(Options.y>0)c.rgb=grade(c.rgb);
    return c;
}
)";}
}
