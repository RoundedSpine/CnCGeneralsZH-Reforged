// Water reflection mode contract shared by the iOS water renderer and tree-shadow pass.
// Adapted from upstream's WaterScreenReflection mode definitions. The screen-space
// ray-tracing shader and capability synchronization are not enabled on iOS yet.
// Distributed under the repository's GPL-3.0 terms; see LICENSE.md.
#pragma once

namespace WaterScreenReflection {

enum Mode { Mirror, ScreenSpace, Hybrid, SkyOnly };
constexpr unsigned LegacyMirrorSize = 512;
constexpr unsigned HybridMirrorSize = 1024;
inline unsigned MirrorSize(Mode mode, int detail = 1)
{
    const unsigned high = mode == Hybrid ? HybridMirrorSize : LegacyMirrorSize;
    return detail == 0 ? high / 2 : high;
}
inline Mode Resolve(int setting, bool enabled = true)
{
    if (!enabled) return SkyOnly;
    switch (setting) {
        case 0: return Mirror;
        case 1: return ScreenSpace;
        default: return Hybrid;
    }
}
inline bool Traces(Mode mode) { return mode == ScreenSpace || mode == Hybrid; }
inline bool UsesMirror(Mode mode) { return mode == Mirror || mode == Hybrid; }

} // namespace WaterScreenReflection
