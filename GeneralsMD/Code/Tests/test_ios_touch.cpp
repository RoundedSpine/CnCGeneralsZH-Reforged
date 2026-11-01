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
}
