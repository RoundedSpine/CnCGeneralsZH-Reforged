// Pure gesture regression tests: no UIKit, SDL window or game install required.
#include "SdlDevice/GameClient/IOSTouch.h"
#include <cassert>
#include <vector>
using namespace IOSTouch;
struct Fixture {
    State state;
    std::vector<Action> out;
    void event(Kind kind, int id, float x, float y, std::uint64_t time = 0, int device = 1) {
        state.handle(kind, device, id, x, y, time, [this](Action a) { out.push_back(a); });
    }
    int count(ActionKind kind) const { int n = 0; for (auto a : out) if (a.kind == kind) ++n; return n; }
};
int main() {
    { Fixture f; f.event(Kind::Down,1,20,30); f.event(Kind::Up,1,20,30,100000000);
      assert(f.count(ActionKind::LeftDown)==1 && f.count(ActionKind::LeftUp)==1); }
    { Fixture f; f.event(Kind::Down,1,20,30); f.event(Kind::Up,1,20,30,600000000);
      assert(f.count(ActionKind::RightDown)==1 && f.count(ActionKind::LeftDown)==0); }
    { Fixture f; f.event(Kind::Down,1,20,30); f.event(Kind::Move,1,70,80);
      f.event(Kind::Up,1,70,80);
      assert(f.count(ActionKind::LeftDown)==1 && f.count(ActionKind::LeftUp)==1);
      assert(f.out[1].x==20 && f.out[1].y==30); }
    { Fixture f; f.event(Kind::Down,1,20,30); f.event(Kind::Down,2,80,30);
      f.event(Kind::Up,1,20,30); f.event(Kind::Up,2,80,30,100000000);
      assert(f.count(ActionKind::RightDown)==1 && f.count(ActionKind::LeftDown)==0);
      assert(f.out.back().x==20 && f.out.back().y==30); }
    { Fixture f; f.event(Kind::Down,1,20,30); f.event(Kind::Down,2,80,30);
      f.event(Kind::Move,2,110,30); f.event(Kind::Up,2,110,30); f.event(Kind::Up,1,20,30);
      assert(f.count(ActionKind::Wheel)==1 && f.count(ActionKind::RightDown)==0);
      assert(f.out.back().wheel>0); }
    { Fixture f; f.event(Kind::Down,1,20,30); f.event(Kind::Down,2,80,30);
      f.event(Kind::Up,2,80,30); f.event(Kind::Move,1,70,30); f.event(Kind::Up,1,70,30);
      assert(f.count(ActionKind::LeftDown)==0 && f.count(ActionKind::RightDown)==0); }
    { Fixture f; for(int i=1;i<=3;++i) f.event(Kind::Down,i,float(i*10),30);
      for(int i=1;i<=3;++i) f.event(Kind::Up,i,float(i*10),30,100000000);
      assert(f.count(ActionKind::Escape)==1 && f.count(ActionKind::LeftDown)==0); }
    { Fixture f; f.event(Kind::Down,1,20,30); f.event(Kind::Move,1,70,30);
      f.event(Kind::Cancel,1,70,30); f.event(Kind::Up,1,70,30);
      assert(f.count(ActionKind::LeftDown)==1 && f.count(ActionKind::LeftUp)==1);
      f.event(Kind::Down,1,20,30); f.event(Kind::Up,1,20,30);
      assert(f.count(ActionKind::LeftDown)==2 && f.count(ActionKind::LeftUp)==2); }
    { Fixture f; f.event(Kind::Down,1,20,30); f.event(Kind::Down,1,20,30);
      f.event(Kind::Up,1,20,30); assert(f.count(ActionKind::LeftDown)==1); }
    { Fixture f; f.event(Kind::Down,1,20,30); f.event(Kind::Down,1,80,30,0,2);
      f.event(Kind::Up,1,20,30); f.event(Kind::Up,1,80,30,0,2);
      assert(f.count(ActionKind::RightDown)==1); }

    // The hold arms while the finger is still down. The lift after that is not a second click.
    { Fixture f; f.event(Kind::Down,1,20,30,0);
      f.state.poll(400000000, [&](Action a){ f.out.push_back(a); });
      assert(f.count(ActionKind::HoldArmed)==0 && f.count(ActionKind::RightDown)==0);
      f.state.poll(550000000, [&](Action a){ f.out.push_back(a); });
      assert(f.count(ActionKind::HoldArmed)==1 && f.count(ActionKind::RightDown)==1);
      f.event(Kind::Up,1,20,30,700000000);
      assert(f.count(ActionKind::RightDown)==1 && f.count(ActionKind::LeftDown)==0); }
    // Movement before the threshold cancels the hold, and a later poll must not fire it.
    { Fixture f; f.event(Kind::Down,1,20,30,0);
      f.event(Kind::Move,1,40,30,200000000);
      f.state.poll(800000000, [&](Action a){ f.out.push_back(a); });
      assert(f.count(ActionKind::HoldArmed)==0 && f.count(ActionKind::RightDown)==0);
      assert(f.count(ActionKind::LeftDown)==1); }
    // A second finger before the threshold is a chord, not a long-press.
    { Fixture f; f.event(Kind::Down,1,20,30,0);
      f.state.poll(200000000, [&](Action a){ f.out.push_back(a); });
      f.event(Kind::Down,2,80,30,250000000);
      f.state.poll(800000000, [&](Action a){ f.out.push_back(a); });
      f.event(Kind::Up,1,20,30,850000000); f.event(Kind::Up,2,80,30,860000000);
      assert(f.count(ActionKind::HoldArmed)==0 && f.count(ActionKind::RightDown)==0); }
    // A fast second tap is a double click. A slow one, a moved one and a far one are not.
    { Fixture f; f.event(Kind::Down,1,20,30,0); f.event(Kind::Up,1,20,30,50000000);
      f.event(Kind::Down,1,25,34,200000000); f.event(Kind::Up,1,25,34,240000000);
      assert(f.count(ActionKind::LeftDown)==2);
      assert(f.out.back().clicks==2 && f.out[f.out.size()-2].clicks==2); }
    { Fixture f; f.event(Kind::Down,1,20,30,0); f.event(Kind::Up,1,20,30,50000000);
      f.event(Kind::Down,1,22,32,500000000); f.event(Kind::Up,1,22,32,540000000);
      assert(f.out.back().clicks==1); }
    { Fixture f; f.event(Kind::Down,1,20,30,0); f.event(Kind::Up,1,20,30,50000000);
      f.event(Kind::Down,1,80,30,120000000); f.event(Kind::Up,1,80,30,150000000);
      assert(f.out.back().clicks==1); }
    { Fixture f; f.event(Kind::Down,1,20,30,0); f.event(Kind::Up,1,20,30,50000000);
      f.event(Kind::Down,1,22,32,100000000); f.event(Kind::Move,1,80,32,130000000);
      f.event(Kind::Up,1,80,32,160000000);
      assert(f.count(ActionKind::LeftDown)==2);
      bool doubled = false; for (auto a : f.out) if (a.clicks==2) doubled = true;
      assert(!doubled); }
    // Sticky modifiers stay down. A command is a press and a release. Hidden eats both.
    { Controls controls; std::vector<std::pair<Key,bool>> keys;
      auto sink = [&](Key key, bool down){ keys.push_back({key, down}); };
      controls.press(ZH_IOS_CONTROL_SHIFT, sink);
      controls.press(ZH_IOS_CONTROL_STOP, sink);
      assert(controls.shiftHeld());
      assert(keys.size()==3 && keys[0].first==Key::LShift && keys[0].second);
      assert(keys[1].first==Key::S && keys[1].second && keys[2].first==Key::S && !keys[2].second);
      controls.press(ZH_IOS_CONTROL_SHIFT, sink);
      assert(!controls.shiftHeld() && keys.back().first==Key::LShift && !keys.back().second);
      controls.setHidden(true);
      controls.press(ZH_IOS_CONTROL_CTRL, sink);
      assert(!controls.ctrlHeld()); }
}
