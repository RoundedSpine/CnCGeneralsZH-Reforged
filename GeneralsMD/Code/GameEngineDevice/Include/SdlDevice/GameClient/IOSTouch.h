// Bounded, platform-independent iOS gesture state. Coordinates are window points.
// Output goes directly to the existing input dispatcher; it does not queue SDL events.
#pragma once
#include <cmath>
#include <cstdint>

namespace IOSTouch {
enum class Kind { Down, Move, Up, Cancel };
enum class ActionKind { Move, LeftDown, LeftUp, RightDown, RightUp, Wheel, Escape, PanStart, PanMove, PanEnd };
struct Action { ActionKind kind; float x, y, wheel; float dx = 0, dy = 0; };

class State {
    struct Finger {
        std::int64_t device = 0, id = 0;
        float x = 0, y = 0, startX = 0, startY = 0;
        bool active = false;
    };
    Finger fingers[10];
    int used = 0, active = 0, maximum = 0;
    bool moved = false, leftDown = false;
    enum class PairMode { Undecided, Pan, Pinch };
    PairMode pairMode = PairMode::Undecided;
    float separation = 0, initialSeparation = 0;
    float initialX = 0, initialY = 0, lastX = 0, lastY = 0;
    bool pairPending = false;
    std::uint64_t started = 0;

    float distance() const {
        const Finger *first = nullptr;
        for (int i = 0; i < used; ++i) {
            if (!fingers[i].active) continue;
            if (!first) first = &fingers[i];
            else return std::hypot(first->x - fingers[i].x, first->y - fingers[i].y);
        }
        return 0;
    }
    void reset() {
        used = active = maximum = 0; moved = leftDown = false;
        separation = initialSeparation = 0;
        pairMode = PairMode::Undecided; pairPending = false;
    }
public:
    bool isActive() const { return active > 0; }
    bool isMultiTouch() const { return active > 0 && maximum >= 2; }
    // Called after the SDL event batch. Fingers from one UIKit motion packet
    // arrive as separate SDL events; classify using their final positions.
    template<class Sink> void flush(Sink sink) {
        if (!pairPending || active != 2 || maximum != 2 || used != 2) return;
        pairPending = false;
        const float x = (fingers[0].x + fingers[1].x) * .5f;
        const float y = (fingers[0].y + fingers[1].y) * .5f;
        const float now = distance();
        if (pairMode == PairMode::Undecided) {
            const float translation = std::hypot(x - initialX, y - initialY);
            const float radial = std::fabs(now - initialSeparation) * .5f;
            if (translation > 10 && translation > radial * 1.15f) {
                pairMode = PairMode::Pan;
                moved = true;
                sink(Action{ActionKind::PanStart, initialX, initialY, 0});
            } else if (radial > 5 && radial >= translation / 1.15f) {
                pairMode = PairMode::Pinch;
                moved = true;
            } else return;
        }
        if (pairMode == PairMode::Pan) {
            const float dx = x - lastX, dy = y - lastY;
            if (dx != 0 || dy != 0)
                sink(Action{ActionKind::PanMove, lastX, lastY, 0, dx, dy});
        } else if (now > 1 && separation > 1 && now != separation) {
            sink(Action{ActionKind::Wheel, fingers[0].x, fingers[0].y,
                        std::log(now / separation) * 6});
        }
        lastX = x; lastY = y; separation = now;
    }
    template<class Sink> void cancel(Sink sink) {
        if (leftDown) sink(Action{ActionKind::LeftUp, fingers[0].x, fingers[0].y, 0});
        if (pairMode == PairMode::Pan) sink(Action{ActionKind::PanEnd, lastX, lastY, 0});
        reset();
    }

    template<class Sink> void handle(Kind kind, std::int64_t device, std::int64_t id,
                                    float x, float y, std::uint64_t time, Sink sink) {
        if (kind == Kind::Cancel) { cancel(sink); return; }
        if (!std::isfinite(x) || !std::isfinite(y)) return;
        int at = -1;
        for (int i = 0; i < used; ++i)
            if (fingers[i].active && fingers[i].id == id && fingers[i].device == device) at = i;
        if (kind == Kind::Down) {
            if (at >= 0) return; // A duplicated down must not wedge the gesture.
            if (active == 0) { reset(); started = time; }
            if (active == 2) {
                flush(sink);
                if (pairMode == PairMode::Pan) sink(Action{ActionKind::PanEnd, lastX, lastY, 0});
                pairMode = PairMode::Undecided;
            }
            if (used == 10) { moved = true; return; }
            at = used++;
            fingers[at] = Finger{device, id, x, y, x, y, true};
            ++active;
            if (active > maximum) maximum = active;
            if (used > 2 && maximum < 3) moved = true; // Replacing a finger cancels a two-finger chord.
            if (active == 1 && maximum == 1) sink(Action{ActionKind::Move, x, y, 0});
            if (active > 1 && leftDown) {
                sink(Action{ActionKind::LeftUp, fingers[0].x, fingers[0].y, 0});
                leftDown = false;
            }
            separation = initialSeparation = distance();
            if (active == 2 && used == 2) {
                initialX = lastX = (fingers[0].x + fingers[1].x) * .5f;
                initialY = lastY = (fingers[0].y + fingers[1].y) * .5f;
            }
            return;
        }
        if (at < 0) return;
        if (kind == Kind::Up) {
            flush(sink);
            if (pairMode == PairMode::Pan) {
                sink(Action{ActionKind::PanEnd, lastX, lastY, 0});
                pairMode = PairMode::Undecided;
            }
        }
        Finger &finger = fingers[at];
        finger.x = x; finger.y = y;
        if (std::hypot(x - finger.startX, y - finger.startY) > 10) moved = true;
        if (kind == Kind::Move) {
            if (maximum == 1) {
                if (moved && !leftDown) {
                    sink(Action{ActionKind::LeftDown, finger.startX, finger.startY, 0});
                    leftDown = true;
                }
                sink(Action{ActionKind::Move, x, y, 0});
            } else if (active == 2 && maximum == 2 && used == 2) {
                pairPending = true;
            }
            return;
        }
        finger.active = false;
        --active;
        if (leftDown) {
            sink(Action{ActionKind::LeftUp, x, y, 0});
            leftDown = false;
        }
        if (active) return; // No new left drag until every finger of a chord has lifted.
        const std::uint64_t duration = time >= started ? time - started : 0;
        const float targetX = fingers[0].x, targetY = fingers[0].y;
        if (!moved && maximum == 1) {
            bool right = duration >= 550000000ULL;
            sink(Action{right ? ActionKind::RightDown : ActionKind::LeftDown, targetX, targetY, 0});
            sink(Action{right ? ActionKind::RightUp : ActionKind::LeftUp, targetX, targetY, 0});
        } else if (!moved && maximum == 2 && duration <= 400000000ULL) {
            sink(Action{ActionKind::RightDown, targetX, targetY, 0});
            sink(Action{ActionKind::RightUp, targetX, targetY, 0});
        } else if (!moved && maximum == 3 && duration <= 500000000ULL) {
            sink(Action{ActionKind::Escape, targetX, targetY, 0});
        }
        reset();
    }
};
}
