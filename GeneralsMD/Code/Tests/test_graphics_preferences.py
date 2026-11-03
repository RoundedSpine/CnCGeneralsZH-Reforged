#!/usr/bin/env python3
"""Exercise the real menu save path with Classic-forced runtime settings."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

CODE = Path(__file__).resolve().parents[1]


class GraphicsPreferences(unittest.TestCase):
    def test_saved_choices_survive_forced_runtime_and_can_be_changed_back(self):
        source = (CODE / 'GameEngine/Source/GameClient/GUI/GUICallbacks/Menus/OptionsMenu.cpp').read_text()

        def method(signature):
            return signature + source.split(signature, 1)[1].split('\n}', 1)[0] + '\n}\n'

        methods = method('static Int catalogMenuValue( const OptionDef& def )')
        methods += method('static void readCatalogWidgets( OptionPreferences &out )')
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
struct GameWindow {int value=0;} widgets[3];
GameWindow *findOptionWidget(const OptionDef &def) {return widgets+(&def-TheOptionCatalog);}
bool smokeSupported=true, defaultsPressed=false, pendingShellRebuild=false;
bool rendererSupportsOption(const OptionDef &def) {return smokeSupported || strcmp(def.iniKey,"VolumetricSmokeShadows")!=0;}
int optionDefaultValue(const OptionDef &def) {return strcmp(def.iniKey,"ClassicGraphics")==0?1:0;}
std::string formatOptionValue(const OptionDef &def,int value) {return def.kind==OPTION_BOOL?(value?"yes":"no"):std::to_string(value);}
int clampOptionValue(const OptionDef &,int value) {return value;}
bool GadgetCheckBoxIsChecked(GameWindow *w) {return w->value!=0;}
void GadgetComboBoxGetSelectedPos(GameWindow *w,int *v) {*v=w->value;}
int GadgetSliderGetPosition(GameWindow *w) {return w->value;}
// METHODS
void reopen() {for(int i=0;i<3;++i)widgets[i].value=i==0?pref->getBool("ClassicGraphics",true):catalogMenuValue(TheOptionCatalog[i]);}
int main() {
    // A Classic run can save a Reforged next start without changing live fields.
    preferences["ClassicGraphics"]="yes";preferences["LightingStyle"]="0";preferences["VolumetricSmokeShadows"]="no";
    reopen(); widgets[0].value=0;widgets[1].value=widgets[2].value=1;
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
}
'''
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder)
            (path / 'test.cpp').write_text(program.replace('// METHODS', methods))
            subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17', '-Wall', '-Wextra',
                            '-fsanitize=address,undefined', str(path / 'test.cpp'), '-o', str(path / 'test')], check=True)
            subprocess.run([str(path / 'test')], check=True)


if __name__ == '__main__':
    unittest.main()
