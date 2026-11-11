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
    void event(Kind kind,int id,float x,float y,std::uint64_t time = 100000000) {
        state.handle(kind,1,id,x,y,time,[this](Action a){out.push_back(a);});
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
      // The second motion opens the fingers (separation 60 to 110) while the
      // pan continues. The mode used to lock until both fingers lifted, which
      // is the rule the README told players to obey. A pinch may now join a pan.
      assert(f.count(ActionKind::Wheel)==1 && f.count(ActionKind::PanStart)==1);
      f.event(Kind::Cancel,1,40,30);assert(f.count(ActionKind::PanEnd)==1);
      assert(f.count(ActionKind::PanFling)==0);
      f.event(Kind::Down,1,20,30);f.event(Kind::Up,1,20,30);
      assert(f.count(ActionKind::LeftDown)==1 && f.count(ActionKind::LeftUp)==1); }
    // A pure translation after the pan has started does not grow a pinch.
    { Fixture f;f.start();f.event(Kind::Move,1,40,30);f.event(Kind::Move,2,100,30);f.flush();
      f.event(Kind::Move,1,50,40);f.event(Kind::Move,2,110,40);f.flush();
      assert(f.count(ActionKind::Wheel)==0 && f.count(ActionKind::PanMove)==2); }
    // Fling: samples under 80 ms old and faster than 200 pt/s. A pause, a third
    // finger and momentum Off do not emit one. The camera tests above share one
    // timestamp, so their span is zero and they stay fling-free.
    { Fixture f;f.event(Kind::Down,1,0,0,0);f.event(Kind::Down,2,40,0,0);
      f.event(Kind::Move,1,0,80,10000000);f.event(Kind::Move,2,40,80,10000000);f.flush();
      f.event(Kind::Move,1,0,160,60000000);f.event(Kind::Move,2,40,160,60000000);f.flush();
      f.event(Kind::Up,1,0,160,70000000);f.event(Kind::Up,2,40,160,80000000);
      assert(f.count(ActionKind::PanFling)==1);
      int flingAt=-1, endAt=-1;
      for(int i=0;i<(int)f.out.size();++i){
        if(f.out[i].kind==ActionKind::PanEnd && endAt<0) endAt=i;
        if(f.out[i].kind==ActionKind::PanFling) flingAt=i;
      }
      assert(endAt>=0 && flingAt>endAt); // both fingers are up before the glide starts
      Action fling=f.out[flingAt];
      assert(near(fling.vx,0) && fling.vy>200.f); }
    { Fixture f;f.event(Kind::Down,1,0,0,0);f.event(Kind::Down,2,40,0,0);
      f.event(Kind::Move,1,0,80,10000000);f.event(Kind::Move,2,40,80,10000000);f.flush();
      f.event(Kind::Up,1,0,80,200000000);
      assert(f.count(ActionKind::PanFling)==0 && f.count(ActionKind::PanEnd)==1); }
    { Fixture f;f.event(Kind::Down,1,0,0,0);f.event(Kind::Down,2,40,0,0);
      f.event(Kind::Move,1,0,80,10000000);f.event(Kind::Move,2,40,80,10000000);f.flush();
      f.event(Kind::Move,1,0,160,60000000);f.event(Kind::Move,2,40,160,60000000);f.flush();
      f.event(Kind::Down,3,20,20,70000000);
      assert(f.count(ActionKind::PanFling)==0 && f.count(ActionKind::PanEnd)==1); }
    { Fixture f;f.state.setMomentum(Momentum::Off);
      f.event(Kind::Down,1,0,0,0);f.event(Kind::Down,2,40,0,0);
      f.event(Kind::Move,1,0,80,10000000);f.event(Kind::Move,2,40,80,10000000);f.flush();
      f.event(Kind::Move,1,0,160,60000000);f.event(Kind::Move,2,40,160,60000000);f.flush();
      f.event(Kind::Up,1,0,160,70000000);
      assert(f.count(ActionKind::PanFling)==0 && f.count(ActionKind::PanEnd)==1); }
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
    // Momentum: monotonic decay, the 3000 pt/s cap, a clamped hitch, NaN rejected,
    // and the stepped distance matching the integral down to the 30 pt/s stop.
    { const float bad=std::numeric_limits<float>::quiet_NaN();
      ScreenPan::Momentum momentum; assert(!momentum.start({0,0},bad,0,0.30f)); assert(!momentum.active());
      assert(momentum.start({10,20},1000,0,0.30f));
      ScreenPan::Segment segment; assert(!momentum.step(bad,segment)); assert(!momentum.active());
      assert(momentum.start({0,0},10000,0,0.30f));
      assert(momentum.step(1.0f,segment));
      const float capped = std::hypot(segment.to.x-segment.from.x, segment.to.y-segment.from.y);
      const float oneStep = 3000.f*0.30f*(1.f-std::exp(-0.05f/0.30f));
      assert(std::fabs(capped-oneStep)<0.05f);
      assert(momentum.start({50,30},1000,0,0.30f));
      float sum=0, previous=1000;
      for(int i=0;i<10000 && momentum.active();++i){
        if(!momentum.step(1.f/60.f,segment)) continue;
        const float step=std::hypot(segment.to.x-segment.from.x, segment.to.y-segment.from.y);
        assert(step<=previous+0.001f); previous=step; sum+=step;
        assert(near(segment.from.x,50)&&near(segment.from.y,30));
      }
      assert(!momentum.active());
      assert(std::fabs(sum-0.30f*(1000.f-30.f))<0.05f);
      assert(ScreenPan::edgeBlocked(bad,1));
      assert(!ScreenPan::edgeBlocked(10,10) && ScreenPan::edgeBlocked(10,1));
      for(float scale : {1.f,3.f}) for(float zoom : {1.f,4.f}) for(bool rotated : {false,true}) {
        auto project=[=](ScreenPan::Point p){ float x=p.x*zoom,y=p.y*zoom;
          return rotated?ScreenPan::Point{-y*scale,x*scale}:ScreenPan::Point{x*scale,y*scale}; };
        ScreenPan::Momentum glide; assert(glide.start({50,30},800,400,0.30f));
        assert(glide.step(1.f/60.f,segment));
        ScreenPan::Point world;
        assert(ScreenPan::worldDelta(segment,project,world));
        const auto grabbed=project(segment.from), moved=project(segment.to);
        assert(near(moved.x+world.x,grabbed.x)&&near(moved.y+world.y,grabbed.y));
      }
    }
}
