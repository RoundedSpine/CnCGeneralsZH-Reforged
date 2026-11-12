#!/usr/bin/env python3
"""Exercise the real menu save path with Classic-forced runtime settings."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

CODE = Path(__file__).resolve().parents[1]
RENDER = CODE / 'GameEngineDevice/Source/PosixDevice/Render'


def extract(source, signature):
    return signature + source.split(signature, 1)[1].split('\n}', 1)[0] + '\n}\n'


def compile_run(program):
    with tempfile.TemporaryDirectory() as folder:
        path = Path(folder)
        (path / 'test.cpp').write_text(program)
        subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17', '-Wall', '-Wextra',
                        '-fsanitize=address,undefined', '-I' + str(RENDER),
                        str(path / 'test.cpp'), '-o', str(path / 'test')], check=True)
        subprocess.run([str(path / 'test')], check=True)


class GraphicsPreferences(unittest.TestCase):
    def test_sparse_msaa_capabilities_and_menu_round_trip(self):
        source = (CODE / 'GameEngine/Source/GameClient/GUI/GUICallbacks/Menus/OptionsMenu.cpp').read_text()
        catalog = (CODE / 'GameEngine/Source/Common/OptionsCatalog.cpp').read_text()
        frame = (RENDER / 'SdlGpuFrameMsaa.cpp').read_text()
        methods = extract(catalog, 'unsigned msaaSamplesForLevel( Int level )')
        for signature in ('static Int rendererOptionHigh(const OptionDef &def)',
                          'static Bool rendererOptionEntrySupported(const OptionDef &def,Int value)',
                          'static Int clampRendererOptionValue(const OptionDef &def,Int value)',
                          'static Int rendererOptionListIndex(const OptionDef &def,Int value)',
                          'static Int rendererOptionListValue(const OptionDef &def,Int index)',
                          'static Int graphicsQualityShownValue( GameWindow *control )',
                          'static Bool graphicsQualityStepShown( Int step )',
                          'static void setGraphicsQualityControl( GameWindow *control, Int value )'):
            methods += extract(source, signature)
        capabilities = extract(frame, 'bool SdlGpuFrame::Supports_Samples(unsigned count) const')
        capabilities += extract(frame, 'unsigned SdlGpuFrame::Max_Samples() const')
        program = r'''
#include <algorithm>
#include <cassert>
#include <cstring>
#include <initializer_list>
using Int=int;using Bool=bool;using std::min;
constexpr bool TRUE=true,FALSE=false;
constexpr int OPTION_MSAA_LEVEL_COUNT=5;
static const unsigned TheMsaaSamples[OPTION_MSAA_LEVEL_COUNT]={0,2,4,8,16};
using SDL_GPUTextureFormat=unsigned;using SDL_GPUSampleCount=unsigned;
unsigned colourMask=0,depthMask=0;
static unsigned sampleIndex(unsigned count){return count==8?3:count==4?2:count==2?1:0;}
bool SDL_GPUTextureSupportsSampleCount(void*,unsigned format,unsigned sample){return ((format==1?colourMask:depthMask)&(1u<<sample))!=0;}
struct SdlGpuFrame {
    void *GpuDevice=this;bool MsaaFailed=false,MemoryConstrained=false;unsigned DepthFormat=2;
    unsigned Scene_Format()const{return 1;}
    bool Supports_Samples(unsigned)const;unsigned Max_Samples()const;
} gpu;
// CAPABILITIES
namespace SceneEffects {
unsigned MaxSamples(){return gpu.Max_Samples();}
bool SupportsSamples(unsigned count){return gpu.Supports_Samples(count);}
}
struct OptionDef {const char *iniKey;int lo,hi;} msaa={"MSAA",0,3},other={"Other",0,3};
int clampOptionValue(const OptionDef &def,int value){return std::clamp(value,def.lo,def.hi);}
enum {GWS_CHECK_BOX=1,GWS_COMBO_BOX=2};
struct GameWindow {int style=GWS_COMBO_BOX,value=0;int winGetStyle(){return style;}} combo,unrelated;
bool BitTest(int bits,int bit){return (bits&bit)!=0;}
const OptionDef *findOptionDef(const char*){return &msaa;}
GameWindow *findOptionWidget(const OptionDef&){return &combo;}
bool GadgetCheckBoxIsChecked(GameWindow*w){return w->value!=0;}
void GadgetCheckBoxSetChecked(GameWindow*w,bool v){w->value=v;}
void GadgetComboBoxGetSelectedPos(GameWindow*w,int*v){*v=w->value;}
void GadgetComboBoxSetSelectedPos(GameWindow*w,int v){w->value=v;}
int GadgetSliderGetPosition(GameWindow*w){return w->value;}
void GadgetSliderSetPosition(GameWindow*w,int v){w->value=v;}
struct GraphicsQualityRow {int values[4];} TheGraphicsQualityTable[]={{{0,1,2,3}}};
constexpr int TheGraphicsQualityTableCount=1;
GameWindow *graphicsQualityDrivenWidget(const GraphicsQualityRow&){return &combo;}
// PLATFORM
// METHODS
int main(){
    // All intersections, including devices with 4x but no 2x, and mismatched
    // colour/depth support. The renderer must not infer intermediate counts.
    for(colourMask=0;colourMask<16;++colourMask)for(depthMask=0;depthMask<16;++depthMask){
        unsigned highest=1;
        for(unsigned n:{2u,4u,8u}){
            const bool supported=(colourMask&depthMask&(1u<<sampleIndex(n)))!=0;
            assert(gpu.Supports_Samples(n)==supported);if(supported)highest=n;
        }
        assert(gpu.Max_Samples()==highest && gpu.Supports_Samples(1) && !gpu.Supports_Samples(3));
        for(int requested=0;requested<=3;++requested){
            int expected=requested;
#if !defined(_WIN32)
            while(expected>0&&!gpu.Supports_Samples(msaaSamplesForLevel(expected)))--expected;
#endif
            assert(clampRendererOptionValue(msaa,requested)==expected);
            const int index=rendererOptionListIndex(msaa,requested);
            assert(rendererOptionListValue(msaa,index)==expected);
            setGraphicsQualityControl(&combo,requested);
            assert(combo.value==index && graphicsQualityShownValue(&combo)==expected);
            assert(graphicsQualityStepShown(requested));
        }
    }
    // A sparse Off/4x list must save logical 4x (2), never row one (2x).
    colourMask=depthMask=1u<<sampleIndex(4);
#if !defined(_WIN32)
    assert(rendererOptionListValue(msaa,1)==2 && rendererOptionListIndex(msaa,2)==1);
    assert(clampRendererOptionValue(msaa,1)==0);
#endif
    assert(rendererOptionListValue(other,2)==2);
    setGraphicsQualityControl(&unrelated,3);assert(graphicsQualityShownValue(&unrelated)==3);
    for(int failure=0;failure<3;++failure){
        gpu.MemoryConstrained=failure==0;gpu.MsaaFailed=failure==1;gpu.GpuDevice=failure==2?nullptr:&gpu;
        assert(gpu.Max_Samples()==1&&!gpu.Supports_Samples(4)&&gpu.Supports_Samples(1));
    }
}
'''
        program = program.replace('// CAPABILITIES', capabilities).replace('// METHODS', methods)
        for platform in ('#undef _WIN32', '#define _WIN32 1'):
            compile_run(program.replace('// PLATFORM', platform))

    def test_saved_choices_survive_forced_runtime_and_can_be_changed_back(self):
        source = (CODE / 'GameEngine/Source/GameClient/GUI/GUICallbacks/Menus/OptionsMenu.cpp').read_text()

        methods = extract(source, 'static Int catalogMenuValue( const OptionDef& def )')
        methods += extract(source, 'static void readCatalogWidgets( OptionPreferences &out )')
        methods += extract(source, 'static void requestReforgedModeForControl( GameWindow *control )')
        program = r'''
#include <cassert>
#include <cstring>
#include <map>
#include <string>
using Int = int; using Bool = bool; using AsciiString = std::string;
constexpr bool TRUE=true, FALSE=false;
enum {OPTION_BOOL, OPTION_ENUM, OPTION_INT, APPLY_LIVE, APPLY_SHELL_REBUILD};
constexpr int LIGHTING_STYLE_CLASSIC=0;
struct Data {bool m_classicGraphics=true; int m_lightingStyle=0; bool m_volumetricSmokeShadows=false;} data;
Data *TheGlobalData=&data, *TheWritableGlobalData=&data;
struct OptionPreferences : std::map<std::string,std::string> {
    bool getBool(const char *key, bool fallback) const {
        auto it=find(key);return it==end()?fallback:it->second=="yes";
    }
    int getInt(const char *key, int fallback) const {
        auto it=find(key);return it==end()?fallback:std::stoi(it->second);
    }
} preferences;
OptionPreferences *pref=&preferences;
struct OptionDef {const char *iniKey; int kind,apply; Int (*get)();void (*set)(Int); int lo=0;};
OptionDef TheOptionCatalog[] = {
    {"ClassicGraphics", OPTION_BOOL, APPLY_LIVE, []()->int{return data.m_classicGraphics;}, [](int v){data.m_classicGraphics=v;}},
    {"LightingStyle", OPTION_ENUM, APPLY_LIVE, [](){return data.m_lightingStyle;}, [](int v){data.m_lightingStyle=v;}},
    {"VolumetricSmokeShadows", OPTION_BOOL, APPLY_LIVE, []()->int{return data.m_volumetricSmokeShadows;}, [](int v){data.m_volumetricSmokeShadows=v;}}
};
constexpr int TheOptionCatalogCount=3;
struct GameWindow {int value=0;bool enabled=true;bool winGetEnabled(){return enabled;}} widgets[3];
GameWindow *findOptionWidget(const OptionDef &def) {return widgets+(&def-TheOptionCatalog);}
bool smokeSupported=true, defaultsPressed=false, pendingShellRebuild=false;
bool rendererSupportsOption(const OptionDef &def) {return smokeSupported || strcmp(def.iniKey,"VolumetricSmokeShadows")!=0;}
int optionDefaultValue(const OptionDef &def) {return strcmp(def.iniKey,"ClassicGraphics")==0?1:0;}
std::string formatOptionValue(const OptionDef &def,int value) {return def.kind==OPTION_BOOL?(value?"yes":"no"):std::to_string(value);}
int clampRendererOptionValue(const OptionDef &,int value) {return value;}
int rendererOptionListValue(const OptionDef &def,int index) {return def.lo+index;}
bool GadgetCheckBoxIsChecked(GameWindow *w) {return w->value!=0;}
void GadgetCheckBoxSetChecked(GameWindow *w,bool v){w->value=v;}
GameWindow *classicGraphicsCheckBox(){return widgets;}
int graphicsQualityShownValue(GameWindow *w){return w->value;}
bool ignoreSelected=false;unsigned notices=0;
void updateDetailNote(){++notices;}
void GadgetComboBoxGetSelectedPos(GameWindow *w,int *v) {*v=w->value;}
int GadgetSliderGetPosition(GameWindow *w) {return w->value;}
// METHODS
void reopen() {for(int i=0;i<3;++i)widgets[i].value=i==0?pref->getBool("ClassicGraphics",true):catalogMenuValue(TheOptionCatalog[i]);}
int main() {
    // A Classic run can save a Reforged next start without changing live fields.
    preferences["ClassicGraphics"]="yes";preferences["LightingStyle"]="0";preferences["VolumetricSmokeShadows"]="no";
    reopen();widgets[1].value=widgets[2].value=1;
    requestReforgedModeForControl(widgets+1);
    assert(widgets[0].value==0 && notices==1 && !ignoreSelected);
    readCatalogWidgets(preferences);
    assert(preferences["ClassicGraphics"]=="no" && preferences["LightingStyle"]=="1" && preferences["VolumetricSmokeShadows"]=="yes");
    assert(data.m_classicGraphics && data.m_lightingStyle==0 && !data.m_volumetricSmokeShadows);
    reopen();assert(widgets[1].value==1 && widgets[2].value==1);
    readCatalogWidgets(preferences); // Unchanged controls keep the choices.
    assert(preferences["LightingStyle"]=="1" && preferences["VolumetricSmokeShadows"]=="yes");
    // Changing back to the forced values must overwrite the saved opt-ins.
    widgets[1].value=widgets[2].value=0;readCatalogWidgets(preferences);
    assert(preferences["LightingStyle"]=="0" && preferences["VolumetricSmokeShadows"]=="no");
    // A Classic next start must not make an accepted choice disappear on reopen.
    preferences["ClassicGraphics"]="yes";
    widgets[0].value=widgets[1].value=widgets[2].value=1;readCatalogWidgets(preferences);
    reopen();assert(widgets[1].value==1 && widgets[2].value==1);
    // Unsupported effects retain their saved choice when other controls change.
    smokeSupported=false;widgets[2].value=0;readCatalogWidgets(preferences);
    assert(preferences["VolumetricSmokeShadows"]=="yes");
    // On a Reforged restart the saved choices become live and can change normally.
    smokeSupported=true;data.m_classicGraphics=false;
    data.m_lightingStyle=1;data.m_volumetricSmokeShadows=true;
    preferences["ClassicGraphics"]="no";reopen();
    widgets[1].value=widgets[2].value=0;readCatalogWidgets(preferences);
    assert(data.m_lightingStyle==0 && !data.m_volumetricSmokeShadows);
    // Enabling smoke alone also requests Reforged mode; disabled controls and
    // unrelated buttons must not alter it or be read as sliders.
    widgets[0].value=1;widgets[2].value=1;widgets[2].enabled=false;
    requestReforgedModeForControl(widgets+2);assert(widgets[0].value==1);
    widgets[2].enabled=true;GameWindow unrelated;
    requestReforgedModeForControl(&unrelated);assert(widgets[0].value==1);
    requestReforgedModeForControl(widgets+2);assert(widgets[0].value==0);
}
'''
        compile_run(program.replace('// METHODS', methods))

    def test_effect_capability_policy_on_both_renderers(self):
        source = (CODE / 'GameEngine/Source/GameClient/GUI/GUICallbacks/Menus/OptionsMenu.cpp').read_text()
        method = extract(source, 'static Bool rendererSupportsOption( const OptionDef& def )')
        program = r'''
#include <cassert>
#include <cstring>
using Bool=bool;
constexpr bool TRUE=true,FALSE=false;
struct OptionDef {const char *iniKey;};
struct Data {bool m_direct3D11=false;} data;
Data *TheGlobalData=&data;
namespace ModernWater {bool supported=false;bool Supported(){return supported;}}
namespace WaterScreenReflection {bool supported=false;bool Supported(){return supported;}}
namespace HazardCloud {bool supported=false;bool Supported(){return supported;}}
bool isModernWaterOption(const OptionDef& def) {
    return strcmp(def.iniKey,"WaterReflection")==0
        || strcmp(def.iniKey,"WaterReflectionEnabled")==0
        || strcmp(def.iniKey,"WaterFftResolution")==0
        || strcmp(def.iniKey,"WaterRefractionQuality")==0
        || strcmp(def.iniKey,"WaterReflectionDetail")==0;
}
bool modernWaterSettingsSupported(){return WaterScreenReflection::Supported();}
namespace SceneEffects {
bool supported=false,lighting=false,atmosphere=false,firelight=false,heatHaze=false,hdr=false;
bool SupportsAmbientOcclusion(){return supported;}
bool SupportsLighting(){return lighting;}
bool SupportsAtmosphere(){return atmosphere;}
bool SupportsFirelight(){return firelight;}
bool SupportsHeatHaze(){return heatHaze;}
bool SupportsHdr(){return hdr;}
unsigned samples=1;unsigned MaxSamples(){return samples;}
}
// PLATFORM
// METHOD
int main() {
    const char *effects[]={"MapLight","TeamGlow","WaterFoam","GroundMarks","Firelight","HeatHaze","HemiAmbient","InternalHdr","Weather","WaterSplashes","Atmosphere","StructureCollapse"};
    for(bool modern:{false,true}) {
        data.m_direct3D11=modern;
        for(auto key:effects) {
#if defined(_WIN32)
            assert(rendererSupportsOption({key})==modern);
#else
            assert(!rendererSupportsOption({key}));
#endif
        }
#if defined(_WIN32)
        assert(rendererSupportsOption({"MSAA"}));
        assert(rendererSupportsOption({"AmbientOcclusion"})==modern);
#else
        assert(!rendererSupportsOption({"MSAA"}));
        SceneEffects::supported=false;assert(!rendererSupportsOption({"AmbientOcclusion"}));
        SceneEffects::supported=true;assert(rendererSupportsOption({"AmbientOcclusion"}));
#endif
        assert(rendererSupportsOption({"LightingStyle"}));
        assert(rendererSupportsOption({"VolumetricSmokeShadows"}));
        assert(rendererSupportsOption({"Bloom"}));
    }
    assert(!rendererSupportsOption({"WaterStyle"}) && !rendererSupportsOption({"CloudStyle"}));
    ModernWater::supported=HazardCloud::supported=true;
    assert(rendererSupportsOption({"WaterStyle"}) && rendererSupportsOption({"CloudStyle"}));
    for(auto key:{"WaterReflection","WaterReflectionEnabled","WaterFftResolution","WaterRefractionQuality","WaterReflectionDetail"})
        assert(!rendererSupportsOption({key}));
    WaterScreenReflection::supported=true;
    for(auto key:{"WaterReflection","WaterReflectionEnabled","WaterFftResolution","WaterRefractionQuality","WaterReflectionDetail"})
        assert(rendererSupportsOption({key}));
#if !defined(_WIN32)
    SceneEffects::samples=4;assert(rendererSupportsOption({"MSAA"}));
    SceneEffects::hdr=true;assert(rendererSupportsOption({"InternalHdr"}));
    SceneEffects::firelight=true;assert(rendererSupportsOption({"Firelight"}));
    SceneEffects::heatHaze=true;assert(rendererSupportsOption({"HeatHaze"}));
    SceneEffects::lighting=SceneEffects::atmosphere=true;
    for(auto key:{"MapLight","Atmosphere","TeamGlow","HemiAmbient","GroundMarks","WaterSplashes","Weather","StructureCollapse","WaterFoam"})assert(rendererSupportsOption({key}));
    SceneEffects::atmosphere=false;
    assert(!rendererSupportsOption({"MapLight"})&&!rendererSupportsOption({"Atmosphere"}));
    assert(!rendererSupportsOption({"Weather"}));
    assert(rendererSupportsOption({"TeamGlow"})&&rendererSupportsOption({"HemiAmbient"}));
#endif
}
'''
        # Define Windows after standard headers so host libc retains its real ABI.
        program = program.replace('#include <cstring>', '#include <cstring>\n#include <initializer_list>')
        for platform in ('#undef _WIN32', '#define _WIN32 1'):
            compile_run(program.replace('// PLATFORM', platform).replace('// METHOD', method))

    def test_ao_dispatch_accepts_the_main_scene_texture(self):
        frame = extract((RENDER / 'SdlGpuFrameAmbientOcclusion.cpp').read_text(),
                        'bool SdlGpuFrame::Ambient_Occlusion(const SdlTarget &target,const SdlAo::Constants &constants,SDL_GPUTexture *shroud)')
        device = extract((RENDER / 'PosixDevice9AmbientOcclusion.cpp').read_text(),
                         'bool PosixDevice9::Ambient_Occlusion(IDirect3DBaseTexture9 *shroud,const float shroudRect[4])')
        program = r'''
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <new>
#include <string>
#include "SdlAoShader.h"
#include "SdlRenderMatrix.h"
struct SDL_GPUTexture{};
struct SDL_GPUCommandBuffer{} commands;
struct IDirect3DBaseTexture9{};
SDL_GPUTexture back, scene, depth, resolved, fog;
SDL_GPUTexture *lastColour=nullptr,*lastShroud=nullptr;
bool resolveOK=true;
void ZH_IOS_WaitForForeground(){}
SDL_GPUCommandBuffer *SDL_AcquireGPUCommandBuffer(void*){return &commands;}
const char *SDL_GetError(){return "mock GPU error";}
void SDL_CancelGPUCommandBuffer(SDL_GPUCommandBuffer*){}
namespace SdlAo {
struct Renderer {
    explicit Renderer(void*){}
    bool Prepare(unsigned,unsigned,unsigned,std::string&){return true;}
    SDL_GPUTexture *Depth(){return &resolved;}
    bool Render(SDL_GPUCommandBuffer*,SDL_GPUTexture *colour,const Constants&,SDL_GPUTexture *shroud){lastColour=colour;lastShroud=shroud;return true;}
};
}
struct SdlTarget {SDL_GPUTexture *Colour,*Depth;unsigned Width,Height;};
struct SdlGpuFrame {
    void *GpuDevice=this;bool AoFailed=false,SerializeSubmits=false;SdlAo::Renderer *Ao=nullptr;
    SDL_GPUTexture *BackBuffer=&back;unsigned flushes=0,submits=0,invalidations=0;
    ~SdlGpuFrame(){delete Ao;}
    bool Is_Scene_Target(const SdlTarget&)const{return false;}
    unsigned Width()const{return 257;}unsigned Height()const{return 193;}
    bool Supports_Scene_Depth(){return true;}
    bool Memory_Constrained(){return false;}
    enum FlushCause {FlushUnclassified,FlushAmbientOcclusion};
    FlushCause lastFlushCause=FlushUnclassified;
    bool Flush(FlushCause cause=FlushUnclassified){lastFlushCause=cause;++flushes;return true;}
    bool Create_Scene_Depth_Pipeline(){return true;}
    unsigned Scene_Format(){return 1;}
    bool Resolve_Scene_Depth(SDL_GPUCommandBuffer*,SDL_GPUTexture *source,SDL_GPUTexture *dest,unsigned,unsigned){assert(source==&depth && dest==&resolved);return resolveOK;}
    bool Submit(SDL_GPUCommandBuffer*){++submits;return true;}
    bool Submit_Flush(SDL_GPUCommandBuffer *c){return Submit(c);}
    void External_Colour_Write(SDL_GPUTexture *colour){assert(colour==lastColour);}
    void Invalidate_Water_Snapshots(SDL_GPUTexture *colour,void*){assert(colour==lastColour);++invalidations;}
    bool Ambient_Occlusion(const SdlTarget&,const SdlAo::Constants&,SDL_GPUTexture*);
};
struct Matrix {float _11=1,_12=0,_13=0,_14=0,_21=0,_22=1,_23=0,_24=0,_31=0,_32=0,_33=1,_34=0,_41=0,_42=0,_43=0,_44=1;};
struct Posix_Scene_Viewport {
    template<class Viewport>
    Posix_Scene_Viewport(Viewport&,const SdlGpuFrame*,const SdlTarget&){}
};
enum {D3DTS_VIEW,D3DTS_PROJECTION};
struct Mirror {SDL_GPUTexture *Texture(IDirect3DBaseTexture9*,std::string&){return &fog;}} mirror;
struct PosixDevice9 {
    SdlGpuFrame *Gpu;bool SunActive=false,excluded=false;
    explicit PosixDevice9(SdlGpuFrame *frame):Gpu(frame){}
    Matrix Transforms[2];
    struct {unsigned X=0,Y=0,Width=257,Height=193;float MinZ=0,MaxZ=1;} Viewport;
    Mirror *Mirrors=&mirror;
    SdlTarget target{&scene,&depth,257,193};
    bool Sun_Scene_Excluded(){return excluded;}
    bool Resolve_Target(SdlTarget &out,std::string&){out=target;return true;}
    bool Ambient_Occlusion(IDirect3DBaseTexture9*,const float[4]);
};
// METHODS
int main() {
    SdlGpuFrame frame;PosixDevice9 device{&frame};
    // Glow redirects the main view to a texture that is not the back buffer.
    assert(device.target.Colour!=frame.BackBuffer);
    assert(device.Ambient_Occlusion(nullptr,nullptr));
    assert(lastColour==&scene && frame.submits==1 && frame.invalidations==1);
    assert(frame.flushes==1 && frame.lastFlushCause==SdlGpuFrame::FlushAmbientOcclusion);
    // Reflection/custom-scene exclusion remains effective.
    device.excluded=true;assert(!device.Ambient_Occlusion(nullptr,nullptr));
    device.excluded=false;device.SunActive=true;assert(!device.Ambient_Occlusion(nullptr,nullptr));
    assert(frame.submits==1);
    device.SunActive=false;
    IDirect3DBaseTexture9 shroud;const float rect[4]={0,0,1,1};
    assert(device.Ambient_Occlusion(&shroud,rect) && lastShroud==&fog);
    device.Viewport.MaxZ=0;assert(!device.Ambient_Occlusion(nullptr,nullptr));
    device.Viewport.MaxZ=1;
    device.target.Depth=nullptr;assert(!device.Ambient_Occlusion(nullptr,nullptr));
    device.target.Depth=&depth;
    resolveOK=false;assert(!device.Ambient_Occlusion(nullptr,nullptr));
    assert(frame.AoFailed);const unsigned failedFlushes=frame.flushes;
    assert(!device.Ambient_Occlusion(nullptr,nullptr) && frame.flushes==failedFlushes);
}
'''
        compile_run(program.replace('// METHODS', frame + device))

    def test_native_shared_effects_keep_mode_and_client_boundary_guards(self):
        base = (CODE / 'GameEngineDevice/Source/W3DDevice/GameClient/BaseHeightMap.cpp').read_text()
        marks = extract(base, 'Bool BaseHeightMapRenderObjClass::groundMarksOn(void)')
        source = (CODE / 'GameEngineDevice/Source/W3DDevice/GameClient/W3DGameClient.cpp').read_text()
        signature = 'void W3DGameClient::splashInWater(const Coord3D *pos, Real radius)'
        splash = signature + source.split(signature, 1)[1].split('\n\t// any higher', 1)[0] + '\n    ++splashes;\n}\n'
        snow = (CODE / 'GameEngineDevice/Source/W3DDevice/GameClient/W3DSnow.cpp').read_text()
        quads = extract(snow, 'static Bool snowUsesPointSprites(void)')
        program = r'''
#include <cassert>
#include <functional>
using Bool=bool;using Real=float;
#undef __linux__
#define ZH_PLATFORM_IOS 1
constexpr bool FALSE=false;
struct Coord3D{float x,y,z;};
struct Data{bool m_groundMarks=true,m_classicGraphics=false,m_waterSplashes=true;int m_waterStyle=1;} data;
Data *TheGlobalData=&data;
bool presenting=false,gpu=true,modernWater=false,deferred=false;
unsigned queries=0,splashes=0;std::function<void()> pending;
bool Direct3D11_Present_Is_Enabled(){return presenting;}
bool snowWantsPointSprites(){return true;}
namespace SceneEffects{bool SupportsLighting(){++queries;return gpu;}}
namespace ModernWater{bool Supported(){return modernWater;}}
#define LOGIC_AFTER_TICK(...) if(deferred){pending=__VA_ARGS__;return;}
struct BaseHeightMapRenderObjClass{Bool groundMarksOn();};
struct W3DGameClient{void splashInWater(const Coord3D*,Real);};
// METHODS
int main(){
    BaseHeightMapRenderObjClass terrain;
    assert(terrain.groundMarksOn());
    data.m_classicGraphics=true;assert(!terrain.groundMarksOn());
    data.m_classicGraphics=false;gpu=false;assert(!terrain.groundMarksOn());gpu=true;
    data.m_groundMarks=false;assert(!terrain.groundMarksOn());
    W3DGameClient client;Coord3D point{1,2,3};queries=0;deferred=true;
    client.splashInWater(&point,10);assert(pending&&queries==0&&splashes==0);
    deferred=false;pending();assert(splashes==1); // Saved Modern style, actual original-water fallback.
    modernWater=true;client.splashInWater(&point,10);assert(splashes==1);
    modernWater=false;gpu=false;client.splashInWater(&point,10);assert(splashes==1);
    gpu=true;data.m_classicGraphics=true;client.splashInWater(&point,10);assert(splashes==1);
    data.m_classicGraphics=false;data.m_waterSplashes=false;client.splashInWater(&point,10);assert(splashes==1);
    assert(!snowUsesPointSprites()); // iOS must use the supported quad path.
}
'''
        compile_run(program.replace('// METHODS', marks + splash + quads))


if __name__ == '__main__':
    unittest.main()
