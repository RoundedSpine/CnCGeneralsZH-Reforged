// Gesture-to-camera regression: real recognizer, real queue and projection helper.
#include "SdlDevice/GameClient/IOSTouch.h"
#include "GameClient/ScreenPan.h"
#include <cassert>
#include <cmath>
#include <limits>
#include <vector>
using namespace IOSTouch;
struct Fixture {
    State state;
    std::vector<Action> out;
    void event(Kind kind,int id,float x,float y) {
        state.handle(kind,1,id,x,y,100000000,[this](Action a){out.push_back(a);});
    }
    void flush() { state.flush([this](Action a){out.push_back(a);}); }
    int count(ActionKind kind) { int n=0;for(auto a:out) if(a.kind==kind) ++n;return n; }
    void start() { event(Kind::Down,1,20,30);event(Kind::Down,2,80,30); }
};
bool near(float a,float b) { return std::fabs(a-b)<.001f; }
int main() {
    // Process the two SDL motions as one input batch. Intermediate finger spacing
    // must not turn a translation into a zoom, in either event order.
    for (bool reverse : {false,true}) {
        Fixture f;f.start();
        f.event(Kind::Move,reverse?2:1,reverse?120:60,50);
        assert(f.count(ActionKind::Wheel)==0 && f.count(ActionKind::PanMove)==0);
        f.event(Kind::Move,reverse?1:2,reverse?60:120,50);f.flush();
        assert(f.count(ActionKind::PanStart)==1 && f.count(ActionKind::PanMove)==1);
        assert(f.count(ActionKind::Wheel)==0 && f.count(ActionKind::LeftDown)==0);
        Action pan=f.out.back();assert(near(pan.dx,40)&&near(pan.dy,20));
        f.event(Kind::Up,1,60,50);f.event(Kind::Move,2,150,50);f.flush();f.event(Kind::Up,2,150,50);
        assert(f.count(ActionKind::PanEnd)==1 && f.count(ActionKind::PanMove)==1);
        assert(f.count(ActionKind::RightDown)==0 && f.count(ActionKind::LeftDown)==0);
    }
    { Fixture f;f.start();f.event(Kind::Move,1,0,30);f.event(Kind::Move,2,100,30);f.flush();
      assert(f.count(ActionKind::Wheel)==1 && f.count(ActionKind::PanStart)==0); }
    // A stationary first finger is also a valid pinch; retain the working zoom gesture.
    { Fixture f;f.start();f.event(Kind::Move,2,110,30);f.flush();
      assert(f.count(ActionKind::Wheel)==1 && f.count(ActionKind::PanMove)==0); }
    { Fixture f;f.start();f.event(Kind::Move,1,40,30);f.event(Kind::Move,2,100,30);
      f.event(Kind::Up,1,40,30); // The final motion is flushed even before the frame pump.
      assert(f.count(ActionKind::PanMove)==1 && f.count(ActionKind::PanEnd)==1); }
    { Fixture f;f.start();f.event(Kind::Move,1,40,30);f.event(Kind::Move,2,100,30);f.flush();
      f.event(Kind::Move,2,150,30);f.flush();
      assert(f.count(ActionKind::Wheel)==0); // Intent is locked until both fingers lift.
      f.event(Kind::Cancel,1,40,30);assert(f.count(ActionKind::PanEnd)==1);
      f.event(Kind::Down,1,20,30);f.event(Kind::Up,1,20,30);
      assert(f.count(ActionKind::LeftDown)==1 && f.count(ActionKind::LeftUp)==1); }
    { Fixture f;f.start();f.event(Kind::Move,1,40,30);f.event(Kind::Move,2,100,30);f.flush();
      f.event(Kind::Down,3,60,60);f.event(Kind::Move,1,70,30);f.flush();
      assert(f.count(ActionKind::PanEnd)==1 && f.count(ActionKind::PanMove)==1); }
    { Fixture f;f.start();f.event(Kind::Up,1,20,30);f.event(Kind::Down,3,20,30);
      f.event(Kind::Up,2,80,30);f.event(Kind::Up,3,20,30);
      assert(f.count(ActionKind::RightDown)==0); }
    // Coalescing many motion packets must match a single drag. The same grabbed
    // point stays under the finger at different zooms, rotations and screen scales.
    for(float scale : {1.f,3.f}) for(float zoom : {1.f,4.f}) for(bool rotated : {false,true}) {
        auto project=[=](ScreenPan::Point p) {
            float x=p.x*zoom,y=p.y*zoom;
            return rotated?ScreenPan::Point{-y,x}:ScreenPan::Point{x,y};
        };
        ScreenPan::Pending queue;
        ScreenPan::Point from{50*scale,30*scale},mid{65*scale,40*scale},to{90*scale,50*scale};
        queue.add(from,mid);queue.add(mid,to);
        ScreenPan::Segment batched;assert(queue.take(batched));assert(!queue.take(batched));
        ScreenPan::Point combined,a,b;
        assert(ScreenPan::worldDelta(batched,project,combined));
        assert(ScreenPan::worldDelta({from,mid},project,a));
        assert(ScreenPan::worldDelta({mid,to},project,b));
        assert(near(combined.x,a.x+b.x)&&near(combined.y,a.y+b.y));
        const auto grabbed=project(from), moved=project(to);
        assert(near(moved.x+combined.x,grabbed.x)&&near(moved.y+combined.y,grabbed.y));
        queue.add(from,to);queue.clear();assert(!queue.hasMotion());
    }
    { ScreenPan::Point delta;const float bad=std::numeric_limits<float>::quiet_NaN();
      assert(!ScreenPan::worldDelta({{0,0},{1,1}},[=](ScreenPan::Point){return ScreenPan::Point{bad,0};},delta)); }
}
