// Bounded, platform-independent iOS gesture state. Coordinates are window points.
// Output goes directly to the existing input dispatcher; it does not queue SDL events.
#pragma once
#include "SdlDevice/GameClient/IOSTouchChrome.h"
#include <cmath>
#include <cstdint>

namespace IOSTouch {
enum class Kind { Down, Move, Up, Cancel };
enum class ActionKind {
    Move, LeftDown, LeftUp, RightDown, RightUp, Wheel, Escape, PanStart, PanMove, PanEnd,
    HoldArmed, // the long-press threshold was crossed while the finger was still down
    PanFling   // the pan lifted fast enough to glide; vx, vy are points per second
};
enum class Key { LShift, LCtrl, A, S, G, D0, D1, D2, D3, D4, D5, D6, D7, D8, D9 };
enum class Momentum { Off, Short, Normal };

struct Action {
    ActionKind kind;
    float x, y, wheel;
    float dx = 0, dy = 0;
    int clicks = 1;
    float vx = 0, vy = 0;
};

// On-screen buttons inject the same keys a hardware keyboard would. Letters are the
// fixed defaults HotKey.cpp tests (attack-move A, stop S, guard G). Scatter has no
// fixed letter in that table; its binding lives in CommandMap.ini, which is game
// data, so it is not on the bar. Group keys are the number row: Ctrl held (the
// sticky Ctrl button) assigns, a bare number recalls. A player rebind in
// Hotkeys.ini is not read here.
class Controls {
    bool shift = false, ctrl = false, hidden = false;
public:
    void setHidden(bool hide) { hidden = hide; }
    bool isHidden() const { return hidden; }
    bool shiftHeld() const { return shift; }
    bool ctrlHeld() const { return ctrl; }
    template<class Sink> void press(int id, Sink sink) {
        if (hidden) return;
        if (id == ZH_IOS_CONTROL_SHIFT) { shift = !shift; sink(Key::LShift, shift); return; }
        if (id == ZH_IOS_CONTROL_CTRL) { ctrl = !ctrl; sink(Key::LCtrl, ctrl); return; }
        Key key;
        switch (id) {
            case ZH_IOS_CONTROL_ATTACK_MOVE: key = Key::A; break;
            case ZH_IOS_CONTROL_STOP: key = Key::S; break;
            case ZH_IOS_CONTROL_GUARD: key = Key::G; break;
            case ZH_IOS_CONTROL_GROUP_1: key = Key::D1; break;
            case ZH_IOS_CONTROL_GROUP_2: key = Key::D2; break;
            case ZH_IOS_CONTROL_GROUP_3: key = Key::D3; break;
            case ZH_IOS_CONTROL_GROUP_4: key = Key::D4; break;
            case ZH_IOS_CONTROL_GROUP_5: key = Key::D5; break;
            case ZH_IOS_CONTROL_GROUP_6: key = Key::D6; break;
            case ZH_IOS_CONTROL_GROUP_7: key = Key::D7; break;
            case ZH_IOS_CONTROL_GROUP_8: key = Key::D8; break;
            case ZH_IOS_CONTROL_GROUP_9: key = Key::D9; break;
            case ZH_IOS_CONTROL_GROUP_0: key = Key::D0; break;
            default: return;
        }
        sink(key, true);
        sink(key, false);
    }
};

class State {
    struct Finger {
        std::int64_t device = 0, id = 0;
        float x = 0, y = 0, startX = 0, startY = 0;
        bool active = false;
    };
    struct Sample { float x = 0, y = 0; std::uint64_t t = 0; };
    Finger fingers[10];
    int used = 0, active = 0, maximum = 0;
    bool moved = false, leftDown = false;
    bool panning = false, pinching = false, panStarted = false;
    bool oneFingerPan = false;
    float separation = 0, initialSeparation = 0;
    float initialX = 0, initialY = 0, lastX = 0, lastY = 0;
    bool pairPending = false;
    std::uint64_t started = 0, lastTime = 0;
    std::uint64_t holdDelay = 550000000ULL;
    bool holdFired = false;
    bool flingArmed = false;
    Momentum momentum = Momentum::Normal;
    Sample trail[4];
    int trailCount = 0;
    bool lastTapValid = false;
    float lastTapX = 0, lastTapY = 0;
    std::uint64_t lastTapTime = 0;

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
        panning = pinching = panStarted = false; pairPending = false;
        holdFired = false; flingArmed = false; trailCount = 0;
    }
    void remember(float x, float y) {
        if (trailCount == 4) {
            for (int i = 1; i < 4; ++i) trail[i - 1] = trail[i];
            --trailCount;
        }
        trail[trailCount++] = Sample{x, y, lastTime};
    }
    // Newest sample younger than 80 ms, and speed over the samples inside the
    // last 100 ms above 200 pt/s. A zero span (one flush, or tests that stamp
    // every event with the same time) is not a fling.
    bool takeFling(float &vx, float &vy) const {
        if (momentum == Momentum::Off || trailCount < 2) return false;
        const Sample &newest = trail[trailCount - 1];
        if (lastTime < newest.t || lastTime - newest.t > 80000000ULL) return false;
        const Sample *oldest = nullptr;
        for (int i = 0; i < trailCount; ++i)
            if (newest.t - trail[i].t <= 100000000ULL) { oldest = &trail[i]; break; }
        if (!oldest || newest.t <= oldest->t) return false;
        const float dt = (newest.t - oldest->t) / 1e9f;
        vx = (newest.x - oldest->x) / dt;
        vy = (newest.y - oldest->y) / dt;
        return std::isfinite(vx) && std::isfinite(vy) && std::hypot(vx, vy) >= 200.f;
    }
    // fling: emit PanFling when the samples qualify.
    // keep: the other finger is still down, so remember the samples until it lifts.
    // An abort (cancel, third finger) passes both false and leaves nothing armed.
    template<class Sink> void endPan(Sink sink, bool fling, bool keep) {
        if (!panStarted) {
            if (fling && flingArmed) {
                float vx = 0, vy = 0;
                if (takeFling(vx, vy))
                    sink(Action{ActionKind::PanFling, lastX, lastY, 0, 0, 0, 1, vx, vy});
            }
            flingArmed = false;
            if (!keep) trailCount = 0;
            return;
        }
        if (fling) {
            float vx = 0, vy = 0;
            if (takeFling(vx, vy))
                sink(Action{ActionKind::PanFling, lastX, lastY, 0, 0, 0, 1, vx, vy});
        }
        sink(Action{ActionKind::PanEnd, lastX, lastY, 0});
        panStarted = false; panning = false; pinching = false;
        flingArmed = keep;
        if (!keep) trailCount = 0;
    }
    template<class Sink> void considerHold(std::uint64_t now, Sink sink) {
        if (holdFired || moved || active != 1 || maximum != 1) return;
        if (now < started || now - started < holdDelay) return;
        holdFired = true;
        lastTapValid = false;
        const float x = fingers[0].x, y = fingers[0].y;
        sink(Action{ActionKind::HoldArmed, x, y, 0});
        sink(Action{ActionKind::RightDown, x, y, 0});
        sink(Action{ActionKind::RightUp, x, y, 0});
    }
public:
    bool isActive() const { return active > 0; }
    bool isMultiTouch() const { return active > 0 && maximum >= 2; }
    void setHoldDelay(std::uint64_t ns) { if (ns >= 200000000ULL && ns <= 2000000000ULL) holdDelay = ns; }
    std::uint64_t holdDelayNs() const { return holdDelay; }
    void setMomentum(Momentum mode) { momentum = mode; }
    Momentum momentumMode() const { return momentum; }
    // Short and Normal are tuning starts for a device, not measured preferences.
    float momentumTau() const {
        if (momentum == Momentum::Short) return 0.18f;
        if (momentum == Momentum::Normal) return 0.30f;
        return 0.f;
    }
    void setOneFingerPan(bool enabled) { oneFingerPan = enabled; }
    bool oneFingerPanEnabled() const { return oneFingerPan; }

    // Called after the SDL event batch. Fingers from one UIKit motion packet
    // arrive as separate SDL events; classify using their final positions.
    // A pan and a pinch can share the gesture: the first motion still has to
    // win its threshold, and a later motion may add the other axis. Players
    // do not have to lift between them.
    template<class Sink> void flush(Sink sink) {
        if (!pairPending || active != 2 || maximum != 2 || used != 2) return;
        pairPending = false;
        const float x = (fingers[0].x + fingers[1].x) * .5f;
        const float y = (fingers[0].y + fingers[1].y) * .5f;
        const float now = distance();
        const float stepTrans = std::hypot(x - lastX, y - lastY);
        const float stepRadial = std::fabs(now - separation) * .5f;
        const bool started = panning || pinching;
        const float classTrans = started ? stepTrans : std::hypot(x - initialX, y - initialY);
        const float classRadial = started ? stepRadial : std::fabs(now - initialSeparation) * .5f;
        if (!panning && classTrans > 10.f && classTrans > classRadial * 1.15f) panning = true;
        if (!pinching && classRadial > 5.f && classRadial >= classTrans / 1.15f) pinching = true;
        if (started) {
            if (!panning && stepTrans > 8.f) panning = true;
            if (!pinching && stepRadial > 6.f) pinching = true;
        }
        if (!panning && !pinching) return;
        if (panning && !panStarted) {
            panStarted = true;
            moved = true;
            sink(Action{ActionKind::PanStart, initialX, initialY, 0});
        }
        if (pinching) moved = true;
        if (panning) {
            const float dx = x - lastX, dy = y - lastY;
            if (dx != 0 || dy != 0) {
                sink(Action{ActionKind::PanMove, lastX, lastY, 0, dx, dy});
                remember(x, y);
            }
        }
        // Zoom about the midpoint, not the first finger. The log notch map is unchanged.
        if (pinching && now > 1.f && separation > 1.f && now != separation)
            sink(Action{ActionKind::Wheel, x, y, std::log(now / separation) * 6.f});
        lastX = x; lastY = y; separation = now;
    }
    // The frame pump. A stationary finger produces no SDL event, so the hold
    // cannot be armed from the event stream alone.
    template<class Sink> void poll(std::uint64_t now, Sink sink) {
        if (now) lastTime = now;
        considerHold(lastTime, sink);
        flush(sink);
    }
    template<class Sink> void cancel(Sink sink) {
        if (leftDown) sink(Action{ActionKind::LeftUp, fingers[0].x, fingers[0].y, 0});
        flingArmed = false;
        endPan(sink, false, false); // a system cancel is not a fling
        lastTapValid = false;
        reset();
    }

    template<class Sink> void handle(Kind kind, std::int64_t device, std::int64_t id,
                                    float x, float y, std::uint64_t time, Sink sink) {
        if (kind == Kind::Cancel) { cancel(sink); return; }
        if (!std::isfinite(x) || !std::isfinite(y)) return;
        lastTime = time;
        int at = -1;
        for (int i = 0; i < used; ++i)
            if (fingers[i].active && fingers[i].id == id && fingers[i].device == device) at = i;
        if (kind == Kind::Down) {
            if (at >= 0) return; // A duplicated down must not wedge the gesture.
            if (active == 0) { reset(); started = time; }
            if (active == 2) {
                flush(sink);
                flingArmed = false;
                endPan(sink, false, false); // a third finger aborts; it does not fling
            }
            if (used == 10) { moved = true; return; }
            at = used++;
            fingers[at] = Finger{device, id, x, y, x, y, true};
            ++active;
            if (active > maximum) maximum = active;
            if (used > 2 && maximum < 3) moved = true; // Replacing a finger cancels a two-finger chord.
            if (active > 1) lastTapValid = false;
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
            // A two-finger pan ends on the first lift and flings on the second,
            // so the glide does not start under a finger that is still down.
            if (panStarted && active >= 2) endPan(sink, false, true);
            else endPan(sink, true, false);
        }
        Finger &finger = fingers[at];
        finger.x = x; finger.y = y;
        if (std::hypot(x - finger.startX, y - finger.startY) > 10) moved = true;
        if (kind == Kind::Move) {
            if (flingArmed) { flingArmed = false; trailCount = 0; }
            if (maximum == 1 && oneFingerPan && moved) {
                if (!panStarted) {
                    panStarted = true; panning = true;
                    lastX = finger.startX; lastY = finger.startY;
                    sink(Action{ActionKind::PanStart, finger.startX, finger.startY, 0});
                }
                const float dx = x - lastX, dy = y - lastY;
                if (dx != 0 || dy != 0) {
                    sink(Action{ActionKind::PanMove, lastX, lastY, 0, dx, dy});
                    remember(x, y);
                    lastX = x; lastY = y;
                }
                return;
            }
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
        if (holdFired) {
            // The click already went out at the threshold. The lift is not another one.
            lastTapValid = false;
        } else if (!moved && maximum == 1) {
            if (duration >= holdDelay) {
                lastTapValid = false;
                sink(Action{ActionKind::HoldArmed, targetX, targetY, 0});
                sink(Action{ActionKind::RightDown, targetX, targetY, 0});
                sink(Action{ActionKind::RightUp, targetX, targetY, 0});
            } else {
                int clicks = 1;
                if (lastTapValid && time >= lastTapTime && time - lastTapTime <= 300000000ULL
                        && std::hypot(targetX - lastTapX, targetY - lastTapY) <= 30.f)
                    clicks = 2;
                lastTapValid = true;
                lastTapTime = time; lastTapX = targetX; lastTapY = targetY;
                sink(Action{ActionKind::LeftDown, targetX, targetY, 0, 0, 0, clicks});
                sink(Action{ActionKind::LeftUp, targetX, targetY, 0, 0, 0, clicks});
            }
        } else if (!moved && maximum == 2 && duration <= 400000000ULL) {
            lastTapValid = false;
            sink(Action{ActionKind::RightDown, targetX, targetY, 0});
            sink(Action{ActionKind::RightUp, targetX, targetY, 0});
        } else if (!moved && maximum == 3 && duration <= 500000000ULL) {
            lastTapValid = false;
            sink(Action{ActionKind::Escape, targetX, targetY, 0});
        } else {
            lastTapValid = false;
        }
        reset();
    }
};
}
