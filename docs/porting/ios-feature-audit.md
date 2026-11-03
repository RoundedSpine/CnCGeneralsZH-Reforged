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
  warnings defer disposable cache eviction until a completed frame. The `33b49198`
  device log exposed three warnings within 0.18 seconds, immediate AO/atmosphere
  cache recreation and an unexpected close reported by the user. A real warning
  now keeps AO/atmosphere/per-pixel lights/FFT paused and MSAA off until device
  recreation. Repeat warnings do not re-evict mandatory scene copies. Saved
  choices stay intact; vertex lights and analytic water retain their fallbacks.
  Peak CPU staging arenas and idle GPU stream/upload buffers are also released;
  ordinary batch clearing had retained their allocated capacity. Nominal freed
  staging sizes are logged separately from measured process footprint.
  Runtime footprint/headroom samples are logged every 30 seconds and at warning/
  eviction boundaries. GPU mirror counts/nominal level bytes, compressed-texture
  expansion, staging capacity and shader/pipeline counts distinguish cache trends.
  These estimates exclude driver overhead and other engine allocations.
  No Jetsam report was supplied, so OS memory termination is
  suspected rather than confirmed.
- Audio interruption/session recovery is worker-owned and permission-gated.
  Media-service resets invalidate a generation and rebuild device/context while
  retaining voice/stream/decoder state. Stale callbacks cannot change a new session.
- The saved VSync request now reaches SDL swapchain configuration at device creation
  and successful reset. Off prefers immediate presentation, then mailbox where
  supported; otherwise synchronized presentation remains active. `[SDL_PRESENT]`
  reports the request, actual mode, capability fallback and driver failure. Failed
  changes keep the previous mode; offscreen test pacing is independent.
- Shader-constant cache validity now includes iOS. Previously only Linux checked
  the invalid bits, so an unchanged value could be skipped after a native pass or
  reset invalidated the cache. Vertex/pixel setters now resend invalid ranges and
  still skip redundant valid writes.

## Remaining implementation gaps

- Windows' configurable `bloom,ao,fxaa,sharpen` post chain is refused by
  `Direct3D11_Post_Chain` in `dx11runtime_posix.cpp`. Native tone mapping/colour
  grading, shared bloom and native AO have separate connected paths. FXAA and the
  Windows sharpening stage have no SDL equivalents yet. Their absence can affect
  single-sample edge quality and texture sharpness; supported MSAA is available,
  with a memory/geometry cost. Do not treat the default chain string as evidence
  these stages run on iOS.
- `SdlInput_dispatch` handles keyboard, text/IME, mouse and iOS touch, but there is
  no gamepad event adapter. Controller input requires additional implementation.
- GPU particle acceleration uses the CPU fallback. This is a performance gap,
  not evidence that particle visuals are missing.
- The app currently requests full-screen landscape. No game HUD safe-area inset
  handling was found. Notch/home-indicator overlap, software keyboard placement
  and external-input behavior require device checks before changing layouts.

Source checks alone do not establish additional visible bugs in these areas.
Normal-map removal is an upstream renderer change, not an omitted iOS feature.

## Evidence and limits

The latest supplied `33b49198` stderr confirms Reforged lighting/smoke, stencil,
shadows, AO, atmosphere, Firelight and heat resources initialized. Its highest
recorded startup footprint was 230.3 MiB, with no later gameplay footprint samples.
It then recorded the low-memory warnings described above and ended abruptly; the
user confirmed the app closed itself. The older `ce612d51` run exited normally.
The November 2 audio SIGBUS report and older DebugLogFile describe different
sessions. ModelChecksums is a normal asset manifest. These logs do not establish
background/resume or audio-service-reset correctness.

ARM64 Actions run 41596199431 passed the prior batch's Metal readback, sanitizer
helpers, iOS compilation and unsigned IPA packaging. The newer native-effects
batch found mixed sampled/integer-load Metal bindings; the shader generator now
uses explicit same-slot samplers for integer texel-centre reads. Twenty-three
production variants compile to SPIR-V/MSL with unique bindings locally.

New sanitizer fixtures exercise 28 presentation capability/failure/offscreen cases
and the actual shader-constant setters with Linux disabled and iOS enabled. The
native renderer library compiles locally. Physical-device presentation pacing and
shader output still require the ARM64 workflow and a signed device run.

Current HDR/MSAA and native-effects GPU checks must pass on GitHub before the
new batch is considered validated. Local GPU checks explicitly skip when no
backend is available. Run 41623320860 initially failed to acquire a hosted ARM64 runner, then its rerun
passed native effects, all prior Metal pixel checks, complete POSIX GPU draws and
unsigned IPA packaging at `33b49198`. Continue using macOS 15 ARM64.

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
