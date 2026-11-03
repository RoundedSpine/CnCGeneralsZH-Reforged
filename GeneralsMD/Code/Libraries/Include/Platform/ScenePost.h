// GPL-3.0-or-later; see LICENSE.md. Native world filters; bloom/AO have separate passes.
#pragma once
#include <string>
namespace ScenePost {
enum Effect { Copy, Bloom, AO, FXAA, Sharpen };
struct Chain { Effect Effects[4]={}; unsigned Count=0; };
inline bool Parse(const char *text,Chain &chain) {
    chain=Chain{};if(!text)return false;
    std::string rest(text);
    while(!rest.empty()) {
        const auto comma=rest.find(',');const auto name=rest.substr(0,comma);
        rest=comma==std::string::npos?std::string():rest.substr(comma+1);
        if(name.empty())continue;
        if(name=="off"||name=="none"){chain=Chain{};return true;}
        if(chain.Count==4){chain=Chain{};return false;}
        Effect effect;
        if(name=="copy")effect=Copy;else if(name=="bloom"&&chain.Count==0)effect=Bloom;
        else if(name=="ao")effect=AO;else if(name=="fxaa")effect=FXAA;
        else if(name=="sharpen")effect=Sharpen;else {chain=Chain{};return false;}
        chain.Effects[chain.Count++]=effect;
    }
    return true;
}
inline bool Active(Effect effect,unsigned samples,bool constrained) {
    return !constrained&&(effect==Sharpen||(effect==FXAA&&samples<=1));
}
}
