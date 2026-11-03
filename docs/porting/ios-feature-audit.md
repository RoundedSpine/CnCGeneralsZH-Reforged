# Reforged iOS feature review

This targeted renderer, audio, input, lifecycle and packaging review does not
establish full Windows feature parity. See the [44-control audit](ios-graphics-options.md)
for activation timing and scene/hardware conditions.

## Implemented fixes

- Sun shadows and volumetric smoke use capability-checked SDL capture and
  receivers. Native AO, haze, atmosphere/sky, hemisphere ambient and team glow
  consume independent scene depth and shared map/light parameters.
- Reforged lighting/smoke preferences resolve consistently before archive mounting
  and option loading. Explicit Classic selections remain authoritative. Menu
  persistence, write failures, active mode and native capabilities are diagnosed.
- iOS startup reselects Documents/UserData, migrates only missing legacy entries,
  and uses UIKit window format selection with a D24S8-first depth probe.
- Ordinary particles now receive depth fades and flame treatment; object shaders
  receive cloak/reveal/EMP/outage state. Snapshots are independent of active
  attachments and captured at actual drawing boundaries.
- The opaque lighting pass accepts shared point-light/headlight arrays; translucent
  geometry retains vertex-light fallback. Camera reconstruction uses the main
  scene camera instead of a camera left by stencil or mesh work.
- Modern water/ocean maps/wakes and original-water foam are compiled and connected
  for iOS. Missing depth retains original-water rendering. Ground marks, weather,
  splashes and client-only building collapse use native capabilities.
- Optional half-float scene targets and snapshots preserve HDR radiance. Tone
  mapping/colour grading finish before HUD/health bars; presentation remains SDR.
- Optional hardware MSAA resolves colour and separately replays alpha-tested
  depth/stencil into the sampleable attachment. The menu offers only supported
  2×/4×/8× counts. This adds memory and depth geometry work.
- Lifecycle watchers cover SDL events delivered outside the polled queue. Memory
  warnings defer disposable cache eviction until a completed frame.
- Audio interruption/session recovery is worker-owned and permission-gated.
  Media-service resets invalidate a generation and rebuild device/context while
  retaining voice/stream/decoder state. Stale callbacks cannot change a new session.

## Evidence and limits

The latest supplied `ce612d51` stderr confirms Reforged lighting/smoke, stencil,
shadows, AO, atmosphere, Firelight and heat resources initialized, then exited
normally. The November 2 audio SIGBUS report and older DebugLogFile describe
different sessions. ModelChecksums is a normal asset manifest. The logs do not
exercise backgrounding, audio-service reset or measured memory pressure.

ARM64 Actions run 41596199431 passed the prior batch's Metal readback, sanitizer
helpers, iOS compilation and unsigned IPA packaging. The newer native-effects
batch found mixed sampled/integer-load Metal bindings; the shader generator now
uses explicit same-slot samplers for integer texel-centre reads. Seventeen
production variants compile to SPIR-V/MSL with unique bindings locally.

Current HDR/MSAA and native-effects GPU checks must pass on GitHub before the
new batch is considered validated. Local GPU checks explicitly skip when no
backend is available. Run 41623320860 ended before any steps: GitHub could not
allocate a hosted ARM64 runner. Continue using macOS 15 ARM64.

## Device validation still needed

Compare individual options on the same map/camera, reopen Options to verify
persistence, restart for startup options, and load a fresh map for asset/caster
changes. Check water/foam, cloak/EMP, smoke intersections, headlights and collapse
with eligible assets. Measure frame time/memory before increasing HDR/MSAA
defaults. Test calls/Siri, denied resume, headphones/Bluetooth, lock/unlock, audio
service reset, background/foreground and actual low-memory pressure.

The IPA requires the user's signing profile. LAN multicast requires a profile
that grants the entitlement and physical-device discovery checks after local
network permission. GPU particle acceleration remains a CPU fallback; controller
input is additional work. Safe areas, software keyboard and external input need
device checks. Empty DX11 entry points alone do not prove missing SDL behavior.
