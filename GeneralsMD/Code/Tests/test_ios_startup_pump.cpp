#include "Platform/IOSStartupPump.h"
#include <cassert>
int main() {
    IOSStartupPump pump;
    assert(!pump.begin(0, false)); // A worker cannot change main-thread state.
    assert(pump.begin(0, true));   // Clock zero is a valid first pump.
    assert(!pump.begin(100000000, true)); // Reentrant UIKit callback cannot pump again.
    pump.end();
    assert(!pump.begin(49999999, true));
    assert(pump.begin(50000000, true));
    pump.end();
    assert(!pump.begin(80000000, false));
    assert(pump.begin(100000000, true));
    pump.end();
    pump.finish();
    assert(!pump.isActive());
    assert(!pump.begin(999999999, true)); // No extra pumping after startup.
}
