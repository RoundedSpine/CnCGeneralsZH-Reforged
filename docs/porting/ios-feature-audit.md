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
- Native FXAA/sharpen filters now run after tone/grade and before health bars/HUD,
  using a reusable snapshot, bilinear sampling and RGB-only writes. Neighbour reads
  clamp to the active viewport. Hardware MSAA skips FXAA and uses the desktop
  sharpening pass's `-0.25` softening instead of its single-sample `0.35` amount.
- Native GPU particle overflow has an opt-in backend (`-gpuparticles <slots>` or
  `ZH_GPU_PARTICLES=1`), capped at 8,192 records, 1,024 system slots and 32 atlas
  layers. The shared compute shader uses scalar alpha-key words to retain its
  80-byte array stride. Initial native eligibility is non-emissive additive
  particles; alpha systems, weapon trails and advanced CPU effects retain their
  existing path. This adds optional overflow rather than offloading the normal
  CPU particle population. Pressure/refusal closes admission for cached eligible
  systems and keeps CPU fallback; pools are released after GPU work is submitted.

## Remaining implementation gaps

- Native post-chain requests select FXAA/sharpen in order; bloom/AO remain in
  their independent scene phases and are reported as separately routed. This is
  not an arbitrary reordering of the complete Windows post-processing graph.
- GPU particle overflow remains opt-in pending device measurements. Most particles
  still use the CPU. Moving the ordinary CPU population or alpha particles into
  a GPU budget requires further admission, ordering and visual validation work.
- The app currently requests full-screen landscape. No game HUD safe-area inset
  handling was found. Notch/home-indicator overlap, software keyboard placement
  and external-input behavior require device checks before changing layouts.

Source checks alone do not establish additional visible bugs in these areas.
Normal-map removal is an upstream renderer change, not an omitted iOS feature.
Controller input is outside the current work; touch controls are the priority.
See the [FXAA/sharpen and GPU particle investigation](ios-post-particle-gaps.md)
for the original evidence and the implementation/validation status.

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

### Uploaded Actions log `logs_113067769101.zip`

Run 41724204987 at `c1ed173c` used macOS 15 ARM64 and Xcode 16.4. The merged
engine suite and AO/atmosphere/firelight/heat Metal checks passed. Native-effects
compilation passed all 23 SPIR-V/MSL variants, but both additive soft-particle
checks produced black RGB (ground and sky), while their non-fading counterparts
were visible. The workflow stopped before HDR/MSAA/post/particle GPU checks and
device compilation; this run did not produce a new IPA.

Depth capture now submits its resolve before recording consumers, with queue
ordering rather than a GPU-idle wait. This addresses a suspected capture/store
dependency; Metal confirmation is still required. Readbacks performed after the
particle draw print both captured R32 depth and independently resolved live depth
to distinguish capture failure from fade arithmetic. Alpha and additive checks
now require the expected partial fade, so total erasure cannot count as success.
The shared fade also bounds its width divisor away from zero.

A prebuilt Mesa software Vulkan driver made local GPU execution possible. Before
these changes, native effects, HDR, AO, heat, FXAA/sharpen and GPU particle checks
passed there, while MSAA incorrectly selected 1x. The capability query had stopped
at unsupported 2x, hiding available 4x. Queries and attachment selection now check
each count independently against both colour and depth formats. The Options
list, saved enum values and quality presets map sparse choices consistently.
Sanitizer fixtures cover all colour/depth capability intersections and both
native and Windows menu policies. Software Vulkan results do not establish
Metal or physical-device correctness.

After the changes, all seven focused Vulkan suites (native effects, AO, heat,
HDR, MSAA, post and particles) passed, as did the complete POSIX device draw
self-check (746 draws). The shader suite passed 32 tests / 392,152 checks,
including SPIR-V validation and MSL translation. Five graphics-preference and two
memory-pressure sanitizer fixtures passed; the changed menu, scene bridge and
no-renderer stub passed iOS-defined syntax checks.

The larger frame/water self-check remains unresolved on this software driver:
FFT mip assertions fail and the run later crashes during tiled foam-history
checks. A separate unit binary using the original capture method reproduced the
same 1,270 failed assertions and crash. Its scene-depth snapshot checks passed
with both implementations. This comparison separates that failure from the new
capture ordering; it does not establish its cause or an iOS rendering defect.

## Device validation still needed

Compare individual options on the same map/camera, reopen Options to verify
persistence, restart for startup options, and load a fresh map for asset/caster
changes. Check water/foam, cloak/EMP, smoke intersections, headlights and collapse
with eligible assets. Measure frame time/memory before increasing HDR/MSAA
defaults. Test calls/Siri, denied resume, headphones/Bluetooth, lock/unlock, audio
service reset, background/foreground and actual low-memory pressure.

The IPA requires the user's signing profile. LAN multicast requires a profile
that grants the entitlement and physical-device discovery checks after local
network permission. GPU particle overflow remains opt-in; controller input is
outside this work. Safe areas, software keyboard and external input need
device checks. Empty DX11 entry points alone do not prove missing SDL behavior.
