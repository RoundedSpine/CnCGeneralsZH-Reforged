#!/usr/bin/env python3
"""Exercise production particle admission after support/prepare/pressure failures."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest
CODE = Path(__file__).resolve().parents[1]


class Admission(unittest.TestCase):
    def test_native_eligibility_retains_sensitive_effects_on_cpu(self):
        source=(CODE/'GameEngine/Source/GameClient/System/ParticleSys.cpp').read_text()
        # isGpuEligible is a one-line wrapper. The stay reasons sit in the next
        # function, before the following section rule, and that is the code this
        # check has to compile.
        method='Bool ParticleSystemTemplate::isGpuEligible( void ) const'+source.split(
            'Bool ParticleSystemTemplate::isGpuEligible( void ) const',1)[1].split(
                '// ------------------------------------------------------------------------------------------------',1)[0]
        self.assertIn('ParticleSystemTemplate::gpuStayReasons', method)
        program=r'''
#include <cassert>
#include <string>
#include "Platform/ParticleGpuClass.h"
#undef _WIN32
using Bool=bool;
constexpr bool FALSE=false;
enum {PARTICLE=1,ALPHA=2,ADDITIVE=3,WIND_MOTION_NOT_USED=0,WEAPON_TRAIL=5};
struct Name {std::string value;bool isEmpty()const{return value.empty();}bool startsWith(const char *v)const{return value.rfind(v,0)==0;}};
struct ParticleSystemTemplate {
    int m_particleType=PARTICLE,m_priority=0,m_shaderType=ADDITIVE,m_windMotion=WIND_MOTION_NOT_USED;
    bool m_emissive=false,m_isGroundAligned=false,m_fireLook=false,m_isParticleUpTowardsEmitter=false;
    unsigned m_flipbookGridLog2=0;Name m_particleTypeName,m_attachedSystemName;
    Bool isGpuEligible(void) const;
    unsigned gpuStayReasons(void) const;
};
'''+method+r'''
int main(){
    ParticleSystemTemplate normal;assert(normal.isGpuEligible());
    auto trail=normal;trail.m_priority=WEAPON_TRAIL;assert(!trail.isGpuEligible());
    auto alpha=normal;alpha.m_shaderType=ALPHA;assert(!alpha.isGpuEligible());
    auto emissive=normal;emissive.m_emissive=true;assert(!emissive.isGpuEligible());
    auto fire=normal;fire.m_fireLook=true;assert(!fire.isGpuEligible());
    auto flip=normal;flip.m_flipbookGridLog2=2;assert(!flip.isGpuEligible());
    auto wind=normal;wind.m_windMotion=1;assert(!wind.isGpuEligible());
    auto ground=normal;ground.m_isGroundAligned=true;assert(!ground.isGpuEligible());
    auto up=normal;up.m_isParticleUpTowardsEmitter=true;assert(!up.isGpuEligible());
    auto smud=normal;smud.m_particleTypeName.value="SMUDHeat";assert(!smud.isGpuEligible());
    auto attached=normal;attached.m_attachedSystemName.value="Child";assert(!attached.isGpuEligible());
}
'''
        self.compile_run(program)

    def compile_run(self,program):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);(root/'test.cpp').write_text(program);exe=root/'admission'
            subprocess.run([os.environ.get('CXX','c++'),'-std=c++17','-Wall','-Wextra',
                '-fsanitize=address,undefined','-fno-omit-frame-pointer','-I'+str(CODE/'Libraries/Include'),
                str(root/'test.cpp'),'-o',str(exe)],check=True)
            result=subprocess.run([str(exe)],capture_output=True,text=True,timeout=10)
            self.assertEqual(result.returncode,0,result.stderr)

    def test_cached_eligibility_cannot_keep_spawning_after_refusal(self):
        source = (CODE/'GameEngineDevice/Source/W3DDevice/GameClient/W3DParticleSys.cpp').read_text()
        prepare = 'Bool W3DParticleSystemManager::prepareGpu( void )' + source.split(
            'Bool W3DParticleSystemManager::prepareGpu( void )', 1)[1].split(
                '/** The drawn terrain', 1)[0]
        spawn = 'Bool W3DParticleSystemManager::spawnGpuParticle(' + source.split(
            'Bool W3DParticleSystemManager::spawnGpuParticle(', 1)[1].split(
                '\nvoid W3DParticleSystemManager::releaseGpuSystem', 1)[0]
        # Comments between definitions may end with a documentation block; only keep the definition.
        spawn = spawn[:spawn.rfind('}')+1]
        program = r'''
#include <cassert>
#include <cstddef>
#include <vector>
#include <cstdint>
#include "Platform/ParticleBudget.h"
using Bool=bool;using UnsignedInt=unsigned;using std::size_t;constexpr bool TRUE=true,FALSE=false;
constexpr unsigned GPU_NO_SLOT=~0u,GPU_DEATH_FRAMES=65536;
struct Global {bool m_classicGraphics=false;int asked=1<<20;int getEffectiveGpuParticleCapacity(){return asked;}} global;
Global *TheGlobalData=&global;
unsigned gpuCapacityAsked(){return unsigned(global.asked)<NativeParticleBudget::Records?unsigned(global.asked):NativeParticleBudget::Records;}
struct ParticleInfo {unsigned m_lifetime=100;};struct ParticleSystem{};struct ParticleGpuSpawn {unsigned value=0;};
void particleGpuSpawnPack(const ParticleInfo&,unsigned,unsigned,unsigned,ParticleGpuSpawn*){}
namespace GpuParticles {
bool supported=true,works=true;
bool Supported(){return supported;}
bool Prepare(unsigned capacity,bool *fresh){assert(capacity<=NativeParticleBudget::Records);*fresh=true;return works;}
}
struct W3DParticleSystemManager {
    bool m_gpuSupported=false,m_gpuRefused=false,m_gpuFadesDirty=false;
    unsigned m_gpuCapacity=0,m_gpuDirtyFirst=0,m_gpuDirtyEnd=0,m_gpuLive=0,m_gpuSpawned=0,m_gpuEvicted=0,lookups=0;
    uint64_t m_gpuWritten=0,m_gpuTail=0,m_gpuPendingFirst=0;
    std::vector<unsigned> m_gpuDeath,m_gpuDeathCounts,m_gpuSlots,m_gpuTextures,m_gpuSliceQueue;
    std::vector<ParticleGpuSpawn> m_gpuPending;
    unsigned getParticleFrame(){return 100;}
    unsigned gpuSlotFor(ParticleSystem*){++lookups;return 0;}
    void drainGpuDeaths(unsigned){}void sendGpuGround(){}
    Bool prepareGpu();Bool spawnGpuParticle(ParticleSystem*,const ParticleInfo*);
};
''' + prepare + spawn + r'''
int main(){
    ParticleSystem system;ParticleInfo info;
    W3DParticleSystemManager manager;
    assert(manager.prepareGpu()&&manager.m_gpuCapacity==8192&&manager.m_gpuDeath.size()==8192);
    assert(manager.spawnGpuParticle(&system,&info)&&manager.m_gpuPending.size()==1&&manager.lookups==1);
    GpuParticles::supported=false;assert(!manager.prepareGpu());
    assert(manager.m_gpuRefused&&!manager.m_gpuSupported&&manager.m_gpuPending.empty());
    assert(!manager.spawnGpuParticle(&system,&info)&&manager.lookups==1);
    GpuParticles::supported=true;assert(!manager.prepareGpu()); // Refusal is latched.
    assert(!manager.spawnGpuParticle(&system,&info)&&manager.lookups==1);
    W3DParticleSystemManager failed;GpuParticles::works=false;
    assert(!failed.prepareGpu()&&failed.m_gpuRefused);
    assert(!failed.spawnGpuParticle(&system,&info)&&failed.lookups==0);
    GpuParticles::works=true;W3DParticleSystemManager fresh;assert(fresh.prepareGpu());
    global.m_classicGraphics=true;assert(!fresh.spawnGpuParticle(&system,&info)&&fresh.lookups==0);
    global.m_classicGraphics=false;fresh.m_gpuCapacity=0;
    assert(!fresh.spawnGpuParticle(&system,&info)&&fresh.lookups==0); // No modulo by zero.
}
'''
        self.compile_run(program)


if __name__=='__main__': unittest.main()
