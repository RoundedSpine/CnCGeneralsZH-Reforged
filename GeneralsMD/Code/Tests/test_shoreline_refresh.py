#!/usr/bin/env python3
"""Run the production shoreline refresh across live option/map changes."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

CODE = Path(__file__).resolve().parents[1]


class ShorelineRefresh(unittest.TestCase):
    def test_rebuild_after_option_and_water_changes(self):
        source = (CODE/'GameEngineDevice/Source/W3DDevice/GameClient/BaseHeightMap.cpp').read_text()
        body = source.split('void BaseHeightMapRenderObjClass::refreshShorelineState()', 1)[1].split(
            '\nvoid BaseHeightMapRenderObjClass::renderShoreLines(', 1)[0]
        program = r'''
#include <cassert>
#include <cstdio>
using Bool=bool;using Real=float;
struct Global {bool m_showSoftWaterEdge=false;} global;
struct Water {float m_transparentWaterDepth=10,m_minWaterOpacity=.5f;} water;
Global *TheGlobalData=&global;Water *TheWaterTransparency=&water;
void *TheWaterRenderObj=&water;
struct Map {int getXExtent(){return 13;}int getYExtent(){return 9;}} map;
struct DX8Wrapper {static unsigned getBackBufferFormat(){return 21;}};
struct BaseHeightMapRenderObjClass {
    Map *m_map=&map;
    bool m_shorelineStateValid=false,m_shorelineStateEnabled=false;
    float m_shorelineStateDepth=0,m_shorelineStateOpacity=0,m_currentMinWaterOpacity=-1;
    unsigned scans=0,luts=0;int m_numShoreLineTiles=0;
    void refreshShorelineState();
    void updateShorelineTiles(int x,int y,int right,int bottom,Map *m){
        assert(x==0&&y==0&&right==12&&bottom==8&&m==&map);++scans;m_numShoreLineTiles=4;
    }
    void initDestAlphaLUT(){++luts;m_currentMinWaterOpacity=water.m_minWaterOpacity;}
};
void BaseHeightMapRenderObjClass::refreshShorelineState()''' + body + r'''
int main(){
    BaseHeightMapRenderObjClass terrain;
    terrain.refreshShorelineState();assert(terrain.scans==0);
    global.m_showSoftWaterEdge=true;
    terrain.refreshShorelineState();assert(terrain.scans==1&&terrain.luts==1&&terrain.m_numShoreLineTiles==4);
    for(unsigned i=0;i<100;++i)terrain.refreshShorelineState();
    assert(terrain.scans==1&&terrain.luts==1);
    water.m_transparentWaterDepth=20;terrain.refreshShorelineState();assert(terrain.scans==2&&terrain.luts==1);
    water.m_minWaterOpacity=.75f;terrain.refreshShorelineState();assert(terrain.scans==3&&terrain.luts==2);
    global.m_showSoftWaterEdge=false;terrain.refreshShorelineState();assert(terrain.scans==3);
    global.m_showSoftWaterEdge=true;terrain.refreshShorelineState();assert(terrain.scans==4);
    terrain.m_shorelineStateValid=false;terrain.refreshShorelineState();assert(terrain.scans==5);
    TheWaterRenderObj=nullptr;terrain.m_shorelineStateValid=false;terrain.refreshShorelineState();assert(!terrain.m_shorelineStateValid);
    TheWaterRenderObj=&water;terrain.refreshShorelineState();assert(terrain.scans==6);
    water.m_transparentWaterDepth=0;terrain.refreshShorelineState();assert(terrain.scans==6);
}
'''
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            (root/'test.cpp').write_text(program)
            exe = root/'shoreline'
            subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17', '-Wall', '-Wextra',
                            '-DZH_PLATFORM_IOS=1', '-fsanitize=address,undefined',
                            str(root/'test.cpp'), '-o', str(exe)], check=True)
            result = subprocess.run([str(exe)], capture_output=True, text=True, timeout=10)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn('[IOS_SHORELINE] enabled=1', result.stderr)


if __name__ == '__main__':
    unittest.main()
