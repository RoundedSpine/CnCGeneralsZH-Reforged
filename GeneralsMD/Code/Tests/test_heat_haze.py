#!/usr/bin/env python3
"""Exercise native scope cleanup, device guards and the real draw substitution."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest
CODE=Path(__file__).resolve().parents[1]
RENDER=CODE/'GameEngineDevice/Source/PosixDevice/Render'
def method(text,signature):return signature+text.split(signature,1)[1].split('\n}',1)[0]+'\n}\n'
class HeatHaze(unittest.TestCase):
    def test_capture_scope_and_binding_guards(self):
        source=(RENDER/'PosixDevice9SceneEffects.cpp').read_text()
        methods=method(source,'bool PosixDevice9::Begin_Heat_Haze(IDirect3DBaseTexture9 *source)')
        # End is deliberately a single-line method in production.
        methods+=source[source.index('void PosixDevice9::End_Heat_Haze()'):].split('\n',1)[0]+'\n'
        source=(RENDER/'PosixDevice9Draw.cpp').read_text()
        start=source.index('if(HeatSnapshot&&HeatSourceTexture==target.Colour')
        binding=source[start:source.index('if (draw.Textures[slot] != NULL',start)]
        program=r'''
#include <cassert>
#include <string>
#include "Platform/SceneEffects.h"
struct SDL_GPUTexture{} scene,snapshot,other;
struct IDirect3DDevice9{};
struct IDirect3DBaseTexture9{} source,wrong;
struct SdlTarget{SDL_GPUTexture *Colour;};
struct Frame{bool fail=false;unsigned captures=0;
    SDL_GPUTexture *Capture_Heat_Background(const SdlTarget&,std::string&){++captures;return fail?nullptr:&snapshot;}
} frame;
struct Mirrors{SDL_GPUTexture *Texture(IDirect3DBaseTexture9 *p,std::string&){return p==&source?&scene:&other;}} mirrors;
constexpr int ENGINE_SHADER_NONE=0;
struct PosixDevice9:IDirect3DDevice9{
    Frame *Gpu=&frame;struct Mirrors *Mirrors=&mirrors;
    bool SunActive=false,excluded=false,resolve=true;
    SDL_GPUTexture *HeatSnapshot=nullptr,*HeatSourceTexture=nullptr;
    bool Sun_Scene_Excluded(){return excluded;}
    bool Resolve_Target(SdlTarget &t,std::string&){t.Colour=&scene;return resolve;}
    bool Begin_Heat_Haze(IDirect3DBaseTexture9*);void End_Heat_Haze();
    SDL_GPUTexture *binding(SDL_GPUTexture *bound,unsigned slot=0,int texture_stage=0,int pixel_engine=0,SDL_GPUTexture *colour=&scene){
        struct{SDL_GPUTexture *Textures[2];}draw={};draw.Textures[slot]=bound;SdlTarget target={colour};
        // BINDING
        return draw.Textures[slot];
    }
};
// METHODS
unsigned ends=0;
namespace SceneEffects{
bool BeginHeatHaze(IDirect3DDevice9 *d,IDirect3DBaseTexture9 *s){return d&&static_cast<PosixDevice9*>(d)->Begin_Heat_Haze(s);}
void EndHeatHaze(IDirect3DDevice9 *d){++ends;static_cast<PosixDevice9*>(d)->End_Heat_Haze();}
}
int main(){
    PosixDevice9 d;assert(d.binding(&scene)==&scene);
    {SceneEffects::HeatHazeScope scope(&d,&source);assert(scope);assert(frame.captures==1);
        assert(d.binding(&scene)==&snapshot);
        assert(d.binding(&other)==&other);
        assert(d.binding(&scene,1)==&scene);assert(d.binding(&scene,0,1)==&scene);
        assert(d.binding(&scene,0,0,1)==&scene);assert(d.binding(&scene,0,0,0,&other)==&scene);
        {SceneEffects::HeatHazeScope nested(&d,&source);assert(!nested);}
        assert(ends==0&&frame.captures==1&&d.binding(&scene)==&snapshot);
    }
    assert(ends==1&&!d.HeatSnapshot&&!d.HeatSourceTexture&&d.binding(&scene)==&scene);
    auto early=[&](){SceneEffects::HeatHazeScope scope(&d,&source);assert(scope);return;};early();assert(ends==2);
    frame.fail=true;{SceneEffects::HeatHazeScope scope(&d,&source);assert(!scope);}assert(ends==2&&!d.HeatSnapshot);
    frame.fail=false;auto captures=frame.captures;
    assert(!d.Begin_Heat_Haze(&wrong)&&frame.captures==captures);
    d.SunActive=true;assert(!d.Begin_Heat_Haze(&source));d.SunActive=false;
    d.excluded=true;assert(!d.Begin_Heat_Haze(&source));d.excluded=false;
    d.resolve=false;assert(!d.Begin_Heat_Haze(&source));d.resolve=true;
    assert(!d.Begin_Heat_Haze(nullptr));d.Gpu=nullptr;assert(!d.Begin_Heat_Haze(&source));
    assert(frame.captures==captures);
}
'''.replace('// METHODS',methods).replace('// BINDING',binding)
        with tempfile.TemporaryDirectory() as folder:
            path=Path(folder);(path/'test.cpp').write_text(program)
            subprocess.run([os.environ.get('CXX','c++'),'-std=c++17','-Wall','-Wextra','-fsanitize=address,undefined',
                '-I'+str(CODE/'Libraries/Include'),str(path/'test.cpp'),'-o',str(path/'test')],check=True)
            subprocess.run([str(path/'test')],check=True)
if __name__=='__main__':unittest.main()
