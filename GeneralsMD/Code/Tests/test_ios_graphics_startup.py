#!/usr/bin/env python3
"""Regressions for the device log's sandbox and stencil-selection failures."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

CODE = Path(__file__).resolve().parents[1]


def run(program, args=()):
    with tempfile.TemporaryDirectory() as folder:
        path = Path(folder)
        (path / 'test.cpp').write_text(program)
        subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17', '-Wall', '-Wextra',
                        '-fsanitize=address,undefined', '-I' + str(CODE / 'Platform/iOS'),
                        str(path / 'test.cpp'), '-o', str(path / 'test')], check=True)
        subprocess.run([str(path / 'test'), *args], check=True)


class IOSGraphicsStartup(unittest.TestCase):
    def test_user_data_migration_preserves_collisions_and_is_repeatable(self):
        program = r'''
#include "IOSUserDataMigration.h"
#include <cassert>
#include <fstream>
using namespace IOSUserData;
void write(const fs::path &p,const char *s){fs::create_directories(p.parent_path());std::ofstream(p)<<s;}
std::string read(const fs::path &p){std::ifstream f(p);return {std::istreambuf_iterator<char>(f),{}};}
int main(int argc,char **argv){
    assert(argc==2);fs::path root=argv[1],legacy=root/"Library/Old",documents=root/"Documents/UserData";
    write(legacy/"Options.ini","LightingStyle = 1\nVolumetricSmokeShadows = yes\n");
    write(legacy/"Save/mission.sav","save");write(legacy/"ReforgedArt/art.big","art");
    fs::create_directories(documents/"ReforgedArt");
    Migration first;std::string error;assert(migrate(legacy,documents,first,error));
    assert(read(documents/"Options.ini").find("LightingStyle = 1")!=std::string::npos);
    assert(read(documents/"Save/mission.sav")=="save"&&read(documents/"ReforgedArt/art.big")=="art");
    assert(!fs::exists(legacy/"Options.ini")&&first.moved==3);
    write(documents/"Options.ini","new");write(legacy/"options.ini","old");
    write(legacy/"ReforgedArt/art.big","old-art");write(legacy/"Replays/new.rep","replay");
    fs::create_symlink(root/"outside",legacy/"linked");
    Migration next;assert(migrate(legacy,documents,next,error));
    assert(read(documents/"Options.ini")=="new"&&read(legacy/"options.ini")=="old");
    assert(read(documents/"ReforgedArt/art.big")=="art"&&read(legacy/"ReforgedArt/art.big")=="old-art");
    assert(read(documents/"Replays/new.rep")=="replay"&&next.kept==2&&next.links==1);
    assert(!fs::exists(fs::symlink_status(documents/"linked")));
    Migration repeat;assert(migrate(legacy,documents,repeat,error)&&repeat.moved==0);
    Migration absent;assert(migrate(root/"missing",documents,absent,error));
    Migration invalid;assert(!migrate(legacy,legacy/"inside",invalid,error));
    assert(!migrate(legacy,legacy,invalid,error));
    write(root/"file","blocked");assert(!migrate(legacy,root/"file",invalid,error));
    assert(read(legacy/"options.ini")=="old");
}
'''
        with tempfile.TemporaryDirectory() as folder:
            run(program, (folder,))

    def test_legacy_graphics_mode_matches_early_archive_selection(self):
        source=(CODE/'GameEngine/Include/Common/EarlyOptions.h').read_text()
        def method(signature):
            return signature+source.split(signature,1)[1].split('\n}',1)[0]+'\n}\n'
        methods=method('inline bool isEarlyOptionYes( const char *value )')
        methods+=method('inline bool resolveClassicGraphics(const char *classic, const char *lighting, const char *smoke)')
        methods+=method('inline bool getEarlyClassicGraphics()')
        for ios in (False,True):
            program=r'''
#include <cassert>
#include <cstring>
#include <strings.h>
#include <map>
#include <string>
#undef ZH_PLATFORM_IOS
// PLATFORM
std::map<std::string,std::string> file;
bool findEarlyOptionValue(const char *key,char *out,size_t size){
    auto it=file.find(key);if(it==file.end())return false;
    std::strncpy(out,it->second.c_str(),size-1);return true;
}
// METHODS
int main(){
    assert(getEarlyClassicGraphics()); // New install retains the default.
    file["LightingStyle"]="1";
    assert(getEarlyClassicGraphics()==!IOS);
    assert(getEarlyClassicGraphics()==resolveClassicGraphics("","1",""));
    file["ClassicGraphics"]="yes";assert(getEarlyClassicGraphics());
    file["ClassicGraphics"]="no";assert(!getEarlyClassicGraphics());
    file.clear();file["VolumetricSmokeShadows"]="YES";
    assert(getEarlyClassicGraphics()==!IOS);
    assert(getEarlyClassicGraphics()==resolveClassicGraphics("","","YES"));
    file["ClassicGraphics"]="on";assert(getEarlyClassicGraphics());
    file.clear();file["LightingStyle"]="broken";assert(getEarlyClassicGraphics());
    file["LightingStyle"]="0";file["VolumetricSmokeShadows"]="no";assert(getEarlyClassicGraphics());
}
'''
            run(program.replace('// PLATFORM','#define ZH_PLATFORM_IOS 1' if ios else '')
                .replace('// METHODS',methods).replace('!IOS','false' if ios else 'true'))

    def test_ios_selects_window_formats_and_stencil_first(self):
        source = (CODE / 'Libraries/Source/WWVegas/WW3D2/dx8wrapper.cpp').read_text()
        at = source.index('const bool device_windowed')
        choice = source[source.rfind('#if', 0, at):source.index('#endif', at) + len('#endif')]
        signature = 'bool DX8Wrapper::Find_Z_Mode(D3DFORMAT colorbuffer,D3DFORMAT backbuffer, D3DFORMAT *zmode)'
        method = signature + source.split(signature, 1)[1].split('\n}', 1)[0] + '\n}\n'
        program = r'''
#include <cassert>
#undef _WIN32
#undef __linux__
#define ZH_PLATFORM_IOS 1
#define WWDEBUG_SAY(...) ((void)0)
using D3DFORMAT=int;
enum{D3DFMT_D24S8=75,D3DFMT_D32=71,D3DFMT_D24X8=77,D3DFMT_D24X4S4=79,D3DFMT_D16=80,D3DFMT_D15S1=73};
bool stencil=true;unsigned probes=0;
bool Test_Z_Mode(int,int,int mode){++probes;return stencil?mode==D3DFMT_D24S8:mode==D3DFMT_D16;}
struct DX8Wrapper{static bool Find_Z_Mode(D3DFORMAT,D3DFORMAT,D3DFORMAT*);};
// METHOD
int main(){
    // iOS fullscreen pixels need not match a desktop display-mode list.
    // The UIKit-owned window must select formats through the window path.
    // CHOICE
    assert(device_windowed);
    int depth=0;assert(DX8Wrapper::Find_Z_Mode(22,21,&depth));
    assert(depth==D3DFMT_D24S8&&probes==1);
    stencil=false;assert(DX8Wrapper::Find_Z_Mode(22,21,&depth)&&depth==D3DFMT_D16);
}
'''
        run(program.replace('// CHOICE', choice).replace('// METHOD', method))


if __name__ == '__main__':
    unittest.main()
