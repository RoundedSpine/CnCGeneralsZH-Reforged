// Pure timing/lifecycle policy for the startup-only UIKit pump.
#pragma once
#include <cstdint>
class IOSStartupPump {
    bool active = true, inFlight = false, started = false;
    std::uint64_t last = 0;
public:
    bool isActive() const { return active; }
    bool begin(std::uint64_t now, bool mainThread) {
        if (!mainThread || !active || inFlight) return false;
        if (started && now >= last && now - last < 50000000ULL) return false;
        started = inFlight = true;
        last = now;
        return true;
    }
    void end() { inFlight = false; }
    void finish() { active = false; }
};
