// Client-only screen drag accumulation and ground-plane translation.
#pragma once
#include <cmath>
namespace ScreenPan {
struct Point { float x = 0, y = 0; };
struct Segment { Point from, to; };
class Pending {
    Point end, delta;
    bool queued = false;
public:
    bool hasMotion() const { return queued; }
    void clear() { queued = false; delta = Point{}; }
    void add(Point from, Point to) {
        if (!std::isfinite(from.x) || !std::isfinite(from.y) ||
            !std::isfinite(to.x) || !std::isfinite(to.y)) return;
        const Point sum{delta.x + to.x - from.x, delta.y + to.y - from.y};
        if (!std::isfinite(sum.x) || !std::isfinite(sum.y)) return;
        delta = sum; end = to; queued = true;
    }
    bool take(Segment &segment) {
        if (!queued) return false;
        segment = Segment{Point{end.x - delta.x, end.y - delta.y}, end};
        clear();
        return true;
    }
};
// A grabbed point of the map follows the fingers: move the camera by old-world
// minus new-world. There is no frame-time multiplier on a displacement gesture.
template<class Project> bool worldDelta(Segment segment, Project project, Point &delta) {
    if (!std::isfinite(segment.from.x) || !std::isfinite(segment.from.y) ||
        !std::isfinite(segment.to.x) || !std::isfinite(segment.to.y)) return false;
    Point oldWorld = project(segment.from), newWorld = project(segment.to);
    if (!std::isfinite(oldWorld.x) || !std::isfinite(oldWorld.y) ||
        !std::isfinite(newWorld.x) || !std::isfinite(newWorld.y)) return false;
    delta = Point{oldWorld.x - newWorld.x, oldWorld.y - newWorld.y};
    return std::isfinite(delta.x) && std::isfinite(delta.y);
}
}
