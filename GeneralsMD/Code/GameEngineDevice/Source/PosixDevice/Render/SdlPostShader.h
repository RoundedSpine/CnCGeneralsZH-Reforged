// Native scene tone/grade, adapted from dx11post.cpp (GPL-3.0-or-later).
#pragma once
namespace SdlPost {
inline const char *Pixel(){return R"(
Texture2D<float4> Source:register(t0,space2);
SamplerState Point:register(s0,space2);
cbuffer Post:register(b0,space3){float4 Rect;float4 Options;float4 Filter;};
float4 read_colour(float2 uv){
    return Source.SampleLevel(Point,clamp(uv,(Rect.xy+0.5)*Options.zw,(Rect.xy+Rect.zw-0.5)*Options.zw),0);
}
float luminance(float3 c){return dot(c,float3(0.299,0.587,0.114));}
float3 fxaa(float2 uv,float3 c){
    float2 t=Options.zw;
    float nw=luminance(read_colour(uv+float2(-t.x,-t.y)).rgb);
    float ne=luminance(read_colour(uv+float2(t.x,-t.y)).rgb);
    float sw=luminance(read_colour(uv+float2(-t.x,t.y)).rgb);
    float se=luminance(read_colour(uv+float2(t.x,t.y)).rgb);
    float lc=luminance(c),lo=min(lc,min(min(nw,ne),min(sw,se))),hi=max(lc,max(max(nw,ne),max(sw,se)));
    if(hi-lo<max(0.0416,hi*0.125))return c;
    float2 dir=float2(-((nw+ne)-(sw+se)),(nw+sw)-(ne+se));
    float reduce=max((nw+ne+sw+se)*0.25*0.125,0.0078125);
    dir=clamp(dir/(min(abs(dir.x),abs(dir.y))+reduce),-8,8)*t;
    float3 inner=0.5*(read_colour(uv+dir*(1.0/3.0-0.5)).rgb+read_colour(uv+dir*(2.0/3.0-0.5)).rgb);
    float3 outer=0.5*inner+0.25*(read_colour(uv-dir*0.5).rgb+read_colour(uv+dir*0.5).rgb);
    float lum=luminance(outer);return lum<lo||lum>hi?inner:outer;
}
float3 sharpen(float2 uv,float3 c){
    float2 t=Options.zw;
    float3 l=read_colour(uv-float2(t.x,0)).rgb,r=read_colour(uv+float2(t.x,0)).rgb;
    float3 u=read_colour(uv-float2(0,t.y)).rgb,d=read_colour(uv+float2(0,t.y)).rgb;
    return clamp(c+(c-(l+r+u+d)*0.25)*Filter.y,min(c,min(min(l,r),min(u,d))),max(c,max(max(l,r),max(u,d))));
}
float3 tone(float3 colour){
    // Additive half-float blending can overflow to +Inf; (1+0.3*log2(Inf))/Inf is NaN and
    // spreads through the upscale's neighbourhood taps. Clamp into the finite half range.
    colour=min(max(colour,0.0),65000.0);
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
    float2 uv=pos.xy*Options.zw;float4 c=read_colour(uv);
    // Filter.x: 0 tone/grade, 1 FXAA, 2 sharpen, 3 FXAA then sharpen in one pass (the sharpen taps read the
    // unfiltered source, so the pair costs one full-frame read instead of two).
    if(Filter.x>2.5)c.rgb=sharpen(uv,fxaa(uv,c.rgb));
    else if(Filter.x>1.5)c.rgb=sharpen(uv,c.rgb);
    else if(Filter.x>0.5)c.rgb=fxaa(uv,c.rgb);
    else {
        if(Options.x>0.5)c.rgb=tone(max(c.rgb,0));
        if(Options.y>0)c.rgb=grade(c.rgb);
    }
    // Colour grade folded into a filter pass (non-HDR only); the tone/grade pass above already applied it.
    if(Filter.x>0.5&&Options.y>0)c.rgb=grade(c.rgb);
    return c;
}
)";}
}
