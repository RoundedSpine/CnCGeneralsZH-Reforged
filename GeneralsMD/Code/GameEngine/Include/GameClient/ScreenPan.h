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

// Glide after a finger pan. The release point stays fixed and each step is the
// screen segment from that point to point + velocity*dt, so the caller can keep
// feeding panByScreenPoints. Decay is exponential. These constants are tuning
// starts for a device, not measurements.
class Momentum {
    Point origin;
    float vx = 0, vy = 0, tau = 0.30f;
    bool live = false;
public:
    bool active() const { return live; }
    void stop() { live = false; vx = vy = 0; }
    bool start(Point at, float ivx, float ivy, float itau) {
        if (!std::isfinite(at.x) || !std::isfinite(at.y) || !std::isfinite(ivx) || !std::isfinite(ivy)
                || !std::isfinite(itau) || !(itau > 0.f)) {
            stop();
            return false;
        }
        origin = at; vx = ivx; vy = ivy; tau = itau; live = true;
        return true;
    }
    // False with no segment when there is nothing to apply this call (inactive,
    // or a zero dt). A non-finite or negative dt kills the glide. A dt above
    // 50 ms is clamped, so a hitch cannot bank a huge step. The distance of a
    // step is the integral of the exponential, which is what the host test sums.
    bool step(float dt, Segment &segment) {
        if (!live) return false;
        if (!std::isfinite(dt) || dt < 0.f) { stop(); return false; }
        if (dt == 0.f) return false;
        if (dt > 0.05f) dt = 0.05f;
        float speed = std::hypot(vx, vy);
        if (!std::isfinite(speed) || speed < 30.f) { stop(); return false; }
        if (speed > 3000.f) {
            const float scale = 3000.f / speed;
            vx *= scale; vy *= scale; speed = 3000.f;
        }
        const float stopDt = tau * std::log(speed / 30.f);
        if (dt > stopDt) dt = stopDt;
        const float decay = std::exp(-dt / tau);
        const float travel = tau * (1.f - decay);
        segment.from = origin;
        segment.to = Point{origin.x + vx * travel, origin.y + vy * travel};
        vx *= decay; vy *= decay;
        if (!std::isfinite(vx) || !std::isfinite(vy) || std::hypot(vx, vy) <= 30.f) stop();
        return std::isfinite(segment.to.x) && std::isfinite(segment.to.y);
    }
};

// The map edge clamped the look point. requested and achieved are world-unit
// lengths of the same step: what the pan asked for, and what the camera still
// holds after the constraint write-back. Under a fifth of the request means
// the glide is into the edge and should die, including when the numbers are
// not finite.
inline bool edgeBlocked(float requested, float achieved) {
    if (!std::isfinite(requested) || !std::isfinite(achieved)) return true;
    if (requested < 1e-3f) return false;
    if (achieved < 0.f) achieved = 0.f;
    return achieved < requested * 0.2f;
}
}
