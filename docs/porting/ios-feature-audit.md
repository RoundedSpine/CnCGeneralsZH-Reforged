# Reforged iOS feature review

This is a targeted review of renderer, audio, input, lifecycle and packaging seams.
It does not establish full Windows feature parity. The uploaded diagnostic log from
5f1c78fd demonstrates initialization, rendering and normal exit, but no background
or audio-interruption exercise.

## Addressed in this change

- Sun-shadow capture was Linux-only. iOS now uses the same capability-checked SDL
  capture, caster selection, state restoration and receiver/fallback path.
- Ambient Occlusion called an empty POSIX Direct3D 11 function. The opaque-scene
  boundary now calls a native SDL GPU implementation. It uses independent depth,
  half-resolution occlusion, bilateral filtering and RGB-only multiplication.
  Classic graphics, AO preferences and main-view eligibility remain respected.
- Graphics settings and successful resource setup are logged on device. This
  distinguishes a disabled preference from unsupported hardware or shader failure.

The SDL AO algorithm follows the Windows world-space radius and sampling approach.
It applies the Windows cubic shroud visibility attenuation. Pixel tests
use synthetic geometry; actual maps and Metal device performance need validation.

## Confirmed remaining renderer gaps

| Feature | Source evidence | Consequence / next work |
| --- | --- | --- |
| Windows post-process chains | `dx11runtime_posix.cpp`: `Direct3D11_Post_Chain` returns false and finish hooks do nothing | Port desired chain stages to SDL; verify options do not imply unsupported effects are active. |
| Modern atmosphere/ground haze | The POSIX atmosphere, haze-shroud and sky hooks are empty | The dedicated Windows effects are absent; ordinary engine fog is a separate path. |
| Modern per-pixel lights | POSIX headlights/blast-light setters are empty; `Direct3D11_Lights_Per_Pixel` returns false | Audit these lighting options and add SDL bindings/shaders. This does not mean ordinary engine lighting is absent. |
| General soft-particle hooks | POSIX `Set_Soft_Particles`, `Take_Scene_Depth`, `Allow_Soft_Particles` are empty | Port the ordinary particle intersection fade. SDL water/hazard-cloud depth paths are separate and already exist. |
| GPU particle acceleration | `GpuParticlesUnsupported.cpp`: `Supported` returns false; CMake selects it for POSIX | Particles retain the CPU implementation. Check large-battle performance before choosing this as a port priority. |

Do not classify every empty Direct3D 11 hook as a missing feature: native SDL water,
hazard-cloud rendering, capture and ordinary fixed-function lighting have separate
implementations. The new AO scene hook likewise bypasses the old empty DX11 hook.

## Lifecycle, audio and input follow-up

- Low-memory events currently log only (`IOSLifecycle.cpp`). Add deferred eviction
  of disposable GPU/texture caches at a safe engine boundary; never free them
  reentrantly inside the OS callback. Measure with actual memory pressure first.
- Interruption recovery and playback-only audio session configuration are implemented.
  The device logs supplied so far do not exercise recovery. Validate calls/Siri,
  lock/unlock, headphones/Bluetooth and denied resume permission.
- No audio media-services-reset observer is present in the native session bridge or
  pinned miniaudio handler. Test this event and design device/context reconstruction
  if the audio service invalidates existing objects.
- The SDL input adapter handles touch, mouse and keyboard. It has no gamepad event
  adapter; controller support would be additional work rather than a keyboard/mouse fix.
- Touch coordinate scaling and gesture tests do not establish safe-area layout or
  software-keyboard behavior on every iPhone/iPad. Test landscape directions, notches,
  text entry and external input on devices.

## Packaging / network validation

The unsigned IPA still needs the user's signing profile. LAN discovery needs a
profile that actually grants the optional multicast entitlement; requesting it
in packaging does not grant it. Check discovery on physical devices after the
local-network permission is granted. Memory-related entitlement grants and iOS
watchdog/suspension behavior also require device evidence.

## Priorities

1. Validate the new shadows and AO on a real map and measure frame time/memory.
2. Record audio interruption and background/resume tests on device.
3. Handle measured memory-pressure and audio-service reset failures.
4. Make unsupported graphics options accurately report capability, then port the
   remaining effects according to visual value and device cost.

Host shader compilation checks SPIR-V and MSL. The synthetic GPU readback test
checks neutral flat ground, contact darkening near raised geometry, neutral sky
and distant edges, preserved alpha, neutral fully shrouded geometry, odd dimensions and target resizing. A missing
host GPU is reported as a skip; it does not validate those pixel results.
