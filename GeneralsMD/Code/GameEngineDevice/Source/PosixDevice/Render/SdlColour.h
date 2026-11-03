// Half-float scene storage; legacy CPU surfaces remain BGRA8.
#pragma once
#include <cstdint>
#include <cstring>
#include <cmath>
#include <algorithm>
namespace SdlColour {
inline uint16_t half(float value){
    uint32_t bits;std::memcpy(&bits,&value,4);const uint32_t sign=(bits>>16)&0x8000;
    int exponent=int((bits>>23)&255)-127+15;uint32_t mantissa=bits&0x7fffff;
    if(exponent>=31)return uint16_t(sign|0x7c00);
    if(exponent<=0){if(exponent<-10)return uint16_t(sign);mantissa=(mantissa|0x800000)>>(1-exponent);return uint16_t(sign+((mantissa+0xfff+((mantissa>>13)&1))>>13));}
    uint32_t rounded=mantissa+0xfff+((mantissa>>13)&1);
    return uint16_t(sign+(uint32_t(exponent)<<10)+(rounded>>13));
}
inline float real(uint16_t h){
    const unsigned exponent=(h>>10)&31,mantissa=h&1023;float result;
    if(!exponent)result=std::ldexp(float(mantissa),-24);
    else if(exponent==31)result=mantissa?NAN:INFINITY;
    else result=std::ldexp(float(1024+mantissa),int(exponent)-25);
    return h&0x8000?-result:result;
}
inline void fromBgra(const uint8_t *in,uint16_t *out,unsigned pixels){
    for(unsigned i=0;i<pixels;++i)for(unsigned c=0;c<4;++c)out[i*4+c]=half(in[i*4+(c<3?2-c:c)]/255.0f);
}
inline uint8_t byte(float f){return uint8_t(std::clamp(std::isfinite(f)?f:0.0f,0.0f,1.0f)*255+0.5f);}
inline void toBgra(const uint16_t *in,uint8_t *out,unsigned pixels){
    for(unsigned i=0;i<pixels;++i)for(unsigned c=0;c<4;++c)out[i*4+c]=byte(real(in[i*4+(c<3?2-c:c)]));
}
}
