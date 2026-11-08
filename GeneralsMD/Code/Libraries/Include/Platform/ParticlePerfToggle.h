// Local opt-in particle profiling, isolated from gameplay and multiplayer state.
#pragma once
#include <atomic>
#include <cstdlib>
#include <cstring>
namespace ParticlePerfToggle {
inline std::atomic<bool> &state()
{
    static std::atomic<bool> value([]() {
        const char *env = std::getenv("ZH_PARTICLE_PERF");
        return env && std::strcmp(env, "1") == 0;
    }());
    return value;
}
inline bool enabled() { return state().load(std::memory_order_relaxed); }
inline void setEnabled(bool on) { state().store(on, std::memory_order_relaxed); }
}
