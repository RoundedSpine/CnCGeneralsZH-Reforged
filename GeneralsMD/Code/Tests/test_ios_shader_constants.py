#!/usr/bin/env python3
"""Check iOS constant reuploads after native passes invalidate the wrapper cache."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

CODE = Path(__file__).resolve().parents[1]


class ShaderConstants(unittest.TestCase):
    def test_invalidated_values_and_partial_ranges(self):
        header = (CODE/'Libraries/Source/WWVegas/WW3D2/dx8wrapper.h').read_text()
        methods = header.split('WWINLINE void DX8Wrapper::Set_Vertex_Shader_Constant(', 1)[1]
        methods = 'void DX8Wrapper::Set_Vertex_Shader_Constant(' + methods.split(
            '// shader system updates KJM ^', 1)[0]
        methods = methods.replace('WWINLINE ', '')
        program = r'''
#include <cassert>
#include <cstring>
#include <cstdio>
// This host is Linux. Exercise the actual iOS preprocessor branch without Linux hiding a missing guard.
#undef __linux__
#define ZH_PLATFORM_IOS 1
struct Vector4 {float x,y,z,w;};
unsigned vertexUploads=0,pixelUploads=0,mirrors=0;
struct FakeDevice {
    Vector4 vertex[8]={},pixel[8]={};
    void SetVertexShaderConstantF(int reg,const float *data,int count){
        ++vertexUploads;memcpy(vertex+reg,data,count*sizeof(Vector4));
    }
    void SetPixelShaderConstantF(int reg,const float *data,int count){
        ++pixelUploads;memcpy(pixel+reg,data,count*sizeof(Vector4));
    }
} device;
#define DX8CALL(call) device.call
void Direct3D11_Mirror_Vertex_Shader_Constant(unsigned,const float*,unsigned){++mirrors;}
struct DX8Wrapper {
    static Vector4 Vertex_Shader_Constants[8],Pixel_Shader_Constants[8];
    static bool Vertex_Shader_Constants_Valid[8],Pixel_Shader_Constants_Valid[8];
    static void Set_Vertex_Shader_Constant(int,const void*,int);
    static void Set_Pixel_Shader_Constant(int,const void*,int);
};
Vector4 DX8Wrapper::Vertex_Shader_Constants[8]={},DX8Wrapper::Pixel_Shader_Constants[8]={};
bool DX8Wrapper::Vertex_Shader_Constants_Valid[8]={},DX8Wrapper::Pixel_Shader_Constants_Valid[8]={};
''' + methods + r'''
int main(){
    Vector4 values[2]={{0,0,0,0},{1,2,3,4}};
    using W=DX8Wrapper;
    // A first all-zero constant still needs uploading; then identical valid data is redundant.
    W::Set_Vertex_Shader_Constant(0,values,1);W::Set_Pixel_Shader_Constant(0,values,1);
    assert(vertexUploads==1&&pixelUploads==1);
    W::Set_Vertex_Shader_Constant(0,values,1);W::Set_Pixel_Shader_Constant(0,values,1);
    assert(vertexUploads==1&&pixelUploads==1&&mirrors==2);
    W::Set_Vertex_Shader_Constant(0,values,2);W::Set_Pixel_Shader_Constant(0,values,2);
    assert(vertexUploads==2&&pixelUploads==2);
    // A native pass/reset invalidates a register while the wrapper's cached bytes stay the same.
    device.vertex[1]=device.pixel[1]={9,9,9,9};
    W::Vertex_Shader_Constants_Valid[1]=W::Pixel_Shader_Constants_Valid[1]=false;
    W::Set_Vertex_Shader_Constant(0,values,2);W::Set_Pixel_Shader_Constant(0,values,2);
    assert(vertexUploads==3&&pixelUploads==3);
    assert(memcmp(device.vertex,values,sizeof(values))==0&&memcmp(device.pixel,values,sizeof(values))==0);
    assert(W::Vertex_Shader_Constants_Valid[0]&&W::Vertex_Shader_Constants_Valid[1]);
    assert(W::Pixel_Shader_Constants_Valid[0]&&W::Pixel_Shader_Constants_Valid[1]);
    W::Set_Vertex_Shader_Constant(0,values,2);W::Set_Pixel_Shader_Constant(0,values,2);
    assert(vertexUploads==3&&pixelUploads==3&&mirrors==5);
    puts("iOS shader constant restoration passed");
}
'''
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            (root/'test.cpp').write_text(program)
            exe = root/'constants'
            subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17', '-Wall', '-Wextra',
                            '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                            str(root/'test.cpp'), '-o', str(exe)], check=True)
            result = subprocess.run([str(exe)], capture_output=True, text=True, timeout=10)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn('iOS shader constant restoration passed', result.stdout)


if __name__ == '__main__':
    unittest.main()
