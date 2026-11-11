# iOS touch controls

Branch base: `particle-perf-test` at `e64db807cdd7621f62afe9d71fe747cedffa6919`
(the head the handoff was read from). Work is on `ios-touch-controls`.

Host checks, from that tree, with the same flags as `.github/workflows/build-ios.yml`:

```
c++ -std=c++17 -Wall -Wextra -fsanitize=address,undefined \
  -IGeneralsMD/Code/GameEngineDevice/Include \
  GeneralsMD/Code/Tests/test_ios_touch.cpp -o /tmp/test-ios-touch
c++ -std=c++17 -Wall -Wextra -fsanitize=address,undefined \
  -IGeneralsMD/Code/GameEngineDevice/Include -IGeneralsMD/Code/GameEngine/Include \
  GeneralsMD/Code/Tests/test_ios_camera_pan.cpp -o /tmp/test-ios-camera-pan
```

Both exited 0 before any change, and again after the recognizer, momentum and
control-strip changes. There is no iOS device and no macOS toolchain here, so
nothing below that needs UIKit, SDL or the game binary was executed.

## Orders (open question 1)

`GlobalData` starts at `MOUSE_STYLE_CLASSIC` (`GlobalData.cpp`). `-mouse` is the
only switch, and nothing in `Platform/iOS` or `PosixMain.cpp`'s iOS path adds
it. `m_useAlternateMouse` defaults off and is read from Options.ini
`UseAlternateMouse`. A fresh install therefore has `leftButtonOrders() == true`:
a tap on the ground is a move or attack, and the long-press is the Classic
right click (deselect, and the retail right-drag scroll if the down and up are
split across frames). This change does not flip the mouse style. A player who
turns Alternate Mouse on in options, or who passes `-mouse reforged`, still
orders with the right button, which on touch is the long-press.

## Hold, double-tap, pan and pinch

The hold used to fire on release after 550 ms, which a stationary finger could
never reach from the SDL stream alone because a still finger produces no
event. `State::poll` runs from `SdlInput_flushIOSTouch` on `SDL_GetTicksNS`.
At the delay it emits `HoldArmed`, then the right click, and the later lift is
ignored. 550 ms is unchanged. `ZH_IOS_HOLD_MS` (200–2000) overrides it.
`HoldArmed` is what the UIKit side uses for a medium impact and a 36 pt ring.
That ring needs device validation.

A second tap within 300 ms and 30 pt sets `Action::clicks` to 2.
`SdlMouse::translateEvent` already turns `clicks >= 2` into `MBS_DoubleClick`.
On the Classic mouse, `CommandXlat` handles `MSG_MOUSE_LEFT_DOUBLE_CLICK` as
attack-move when `m_doubleClickAttackMove` is on, and falls through to the
normal left click otherwise. Select-all-of-type is the engine's existing
double-click path, not a new message.

The two-finger mode is no longer locked. The first motion still has to win the
old threshold (pan if translation beats radial, pinch otherwise). A later
motion can add the other axis: 8 pt of translation, or 6 pt of separation
change. The camera test that required the lock was updated on purpose; a pure
translation still does not zoom. The wheel is reported at the midpoint.
`test_ios_camera_pan.cpp` still requires the batched-motion order independence
and the grabbed-point projection.

## Momentum

`IOSTouch::State` keeps up to four midpoint samples. A `PanFling` is emitted
only when the second finger lifts (or the only finger, in one-finger pan),
the newest sample is under 80 ms old, and the speed over the samples inside
100 ms is at least 200 pt/s. The first lift ends the pan with `PanEnd` and
does not glide while a finger remains. A move of that finger, a third finger,
or a cancel drops the fling. `ZH_IOS_MOMENTUM=off` emits none. `short` uses a
0.18 s time constant and `normal` (the default) 0.30 s. Speed is capped at
3000 pt/s, `dt` at 50 ms, and the glide stops under 30 pt/s. The step distance
is the integral of the exponential; the host test matches `tau * (v0 - 30)`.

`LookAtTranslator::scrollView` steps it with `CLOCK_MONOTONIC`, so a second
call in the same instant adds no extra distance. The segment is the release
point to that point plus `v·dt`, through the existing `panByScreenPoints`.
It stops on a new finger (`SdlInput_dispatchIOSTouch`), `cancelTouchPan`,
`resetModes`, a shell, disabled input, frozen time, selecting, a scripted
camera (`View::isCameraMoving`), a follow that is still held after the pan's
first segment has cleared one, and any other scroll, rotate or pitch. The map
edge compares the look point after `applyCameraConstraint` with the step that
asked for the move; under 20% of that request kills the velocity. The
constraint runs on the draw after `scrollView`, so the comparison is one
picture late and was not executed here.

A tap that stops a glide is still a click. The finger-down hook runs before
the recognizer.

## On-screen controls

The left-edge strip (sticky Shift and Ctrl, attack-move, stop, guard, groups)
was removed. It covered the map and was not needed next to the game's own
command card. Orders, modifiers and control groups are the command card, a
hardware keyboard, or a hardware mouse. `ZH_IOS_COMMAND_BAR` is no longer read.
The long-press haptic and ring stay.

## What was not changed, and why

- Camera yaw is `View::setAngle` / `getAngle`. Middle-drag uses it. No twist
  gesture was added; the work plan does not ask for one.
- Edge scroll still follows the cursor. Touch posts a move on finger-down, and
  `MSG_RAW_MOUSE_POSITION` arms `SCROLL_SCREENEDGE` when that point is in the
  band (`LookAtXlat.cpp`). A finger held in the last pixels can scroll the
  map. A two-finger pan stops that scroll while it is applied. Not exercised
  on a device.
- System edge gestures: no override of
  `preferredScreenEdgesDeferringSystemGestures` exists, and SDL owns the view
  controller. It was not swizzled.
- Command-card hit targets are the window rectangles in
  `GameWindowManager::getWindowUnderCursor`. Widening them changes mouse
  picking on every platform. The retail command card was left alone.
- `ZH_IOS_ONE_FINGER_PAN=1` makes one finger pan instead of drag-select. The
  default is off, so the existing drag-box tests stay valid.

## Device checklist

Needs device validation, on an iPhone and an iPad:

- Single tap orders, long-press deselects with a ring and a haptic, double-tap,
  drag-box, tap on a small command-card button.
- Two-finger pan slow and fast, pinch, pan then pinch without lifting,
  two-finger tap, three-finger tap, four fingers.
- Momentum off, short and normal: release at different speeds, touch to stop
  (the touch should still click), glide into a map edge, glide while the
  camera is locked to a unit, during a cinematic, in a replay.
- Swipe up from the bottom edge near the command card.
- Incoming call, notification shade, background and resume mid-gesture.
- Hardware keyboard and mouse, and the software keyboard in chat.
