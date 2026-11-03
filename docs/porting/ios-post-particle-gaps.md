# iOS FXAA, sharpening and GPU particle investigation

Scope: source at `316988fb`, plus local shader-translation probes. Controller
input is outside this work; touch remains the input priority. This investigation
does not enable either feature or establish Metal runtime/performance correctness.

## FXAA and sharpening

`dx11runtime_posix.cpp` refuses every `Direct3D11_Post_Chain` request. The native
`SdlGpuFramePost.cpp` pass currently implements tone mapping and colour grading.
Shared bloom and native AO run through separate paths; their presence does not
mean the Windows chain's FXAA/sharpen stages run.

The existing Windows shaders in `dx11post.cpp` are reusable in principle:

- FXAA samples the centre and four diagonal neighbours, then filters along the
  detected edge. It needs a bilinear, clamped sampler. The native tone/grade pass's
  point sampler is insufficient for FXAA's samples between texels.
- The single-sample sharpening amount is `0.35`, clamped to the neighbourhood's
  colour range to avoid overshoot. With MSAA, Windows skips FXAA and uses a
  **negative** sharpening amount (`-0.25`), which slightly softens texture grain.
  Applying positive sharpening in both cases would change the intended picture.
- They operate on the finished world after tone mapping and before health bars,
  names and HUD. `W3DView::draw` already calls the native finish hook at that point.
- Windows FXAA returns alpha one. A native port must preserve destination alpha,
  as the existing native post pass does with RGB-only writes. It must also respect
  the active viewport and avoid sampling adjacent view content at its borders.
- Configuration should report requested/active passes, distinguish disabled and
  unsupported requests, and avoid duplicating the existing bloom/AO passes.

A local probe extracted the actual shader bodies, supplied SDL fragment resource
sets and translated them through the production glslang/SDL_shadercross path:

| Shader | SPIR-V and MSL translation | Reflected bindings |
| --- | --- | --- |
| FXAA | Passed | One sampler, one uniform buffer |
| Sharpen `0.35` | Passed | One sampler, one uniform buffer |
| MSAA soften `-0.25` | Passed | One sampler, one uniform buffer |

No GPU backend is available in this environment, so this is translation evidence,
not Metal compilation, pixel-readback or a performance measurement.

Each full-resolution image at the supplied log's 2868×1320 resolution is nominally
14.44 MiB in RGBA8 or 28.88 MiB in RGBA16F. The existing heat/post snapshot can be
reused where lifetime/format permit; two effects need not mean two additional
persistent images. Extra copies and full-screen passes still increase bandwidth.
Optional filtering should stop under the existing memory-pressure latch while
required HDR tone mapping remains active.

## GPU particles: what is actually missing

CMake excludes `DX11GpuParticles.cpp` on SDL and uses
`GpuParticlesUnsupported.cpp`, whose `Supported()` returns false. The manager
returns before allocating its GPU pool. These absent allocations therefore do
**not** explain the earlier iOS memory measurements or missing shell glows.

The desktop path is an **overflow particle pool**. `ParticleSystem::createParticle`
only sends eligible spawns there after the CPU particle cap has been exceeded.
Ordinary particles below that cap remain on the CPU. Enabling the desktop design
would allow more particles; it would not automatically reduce normal CPU update
work. A true CPU-offload design needs separate admission/budget policy.

Eligible systems are ordinary, non-ground-aligned ALPHA/ADDITIVE particles without
wind motion, attached child systems, fire look, flipbooks or up-toward-emitter
behavior. Infinite-lifetime spawns are refused. Ground collision is approximated
by clamping against uploaded terrain heights; the ground texture is refreshed at
load/reset, not every terrain deformation.

### Confirmed data-layout blocker

The compute shader translated after assigning SDL compute resource sets, but its
spawn layout did not match the CPU upload:

| Record | CPU stride | Original translated array stride | Probe correction |
| --- | --- | --- | --- |
| `ParticleGpuSpawn` / HLSL `Spawn` | 80 bytes | **96 bytes** | 80 bytes |

The probe inspected SPIR-V `ArrayStride` and generated MSL. Although member offsets
matched the CPU, the translated array advanced 96 bytes between records. MSL
explicitly emitted `spvPaddedArrayElement<Spawn_1, 96>`. Uploading the current packed
CPU array would make the second and subsequent records read incorrectly.

Replacing HLSL `uint2 AlphaKeys` with scalar `uint AlphaKey0, AlphaKey1` and updating
the two references restored stride 80, with scalar offsets 68 and 72 and pad offset
76. This was a probe-only correction; the shipping Windows shader is unchanged.
The corrected shader also translated to MSL. Runtime tests must compare multiple
consecutive records, not just record zero.

Reflected compute requirements were one sampled texture/sampler, three read-only
storage buffers, one read-write storage buffer, one uniform buffer and 64×1×1
threads. SDL and bundled SDL_shadercross expose the necessary compute APIs, but
the game's `sdl3shadercompile` wrapper only handles vertex/fragment stages. Its
graphics helper also supplies no storage-buffer counts. A native implementation
needs stage-aware compilation, storage reflection/binding, compute-to-draw ordering,
texture-array uploads/mips and matching draw pipelines; changing a capability flag
cannot provide these.

### Memory and visual limits

Nominal desktop allocations, using 80-byte spawns, 32-byte evaluated particles,
16,384 system/fade slots, and the fixed 256-layer 128×128 RGBA8 atlas with all mips:

| Preset | Spawn capacity | GPU resources | CPU pending records, up to two rings |
| --- | ---: | ---: | ---: |
| Low | 131,072 | 39.15 MiB | 20 MiB |
| Medium | 262,144 | 53.15 MiB | 40 MiB |
| High | 524,288 | 81.15 MiB | 80 MiB |
| Ultra / initial default | 1,048,576 | 137.15 MiB | 160 MiB |

These estimates exclude terrain, driver/allocation overhead, transfer buffers,
source textures retained by the atlas, CPU system entries/samples and allocator
growth peaks. CPU death tracking adds four bytes per capacity slot plus a small
death-count ring. Pending-vector capacity can remain allocated after clearing.
The fixed atlas alone is 21.33 MiB, so reducing only ring capacity is insufficient.

The desktop renderer draws additive overflow first, then alpha in reverse birth
order, separately from CPU-sorted particles. This does not provide a combined
depth sort. Dense overlapping smoke/fire needs visual comparison. Fire-look and
flipbook systems remain CPU-rendered even with a backend.

The GPU entry/draw data also lacks the iOS weapon-trail contact-fade exemption.
An eligible shell glow moved into an unmodified GPU path could again fade against
its own projectile depth. Keep those trails on the CPU initially, or explicitly
carry the eligibility flag through the GPU pipeline and test it.

Failure/pressure handling must stop accepting new GPU spawns, release resources
after readers finish and retain normal CPU rendering. Merely making `Supported()`
false after startup is insufficient: existing systems cache eligibility, and
`spawnGpuParticle` currently checks refusal/Classic state rather than the cached
support flag. This needs coordinated manager/backend state, diagnostics for any
evicted optional overflow, and no immediate recreation after a memory warning.

## Recommended implementation order

1. Port FXAA/sharpen through the existing world-finish hook, with reusable
   snapshots, bilinear sampling, RGB-only output, explicit active-pass diagnostics
   and the existing memory-pressure latch. Retain the Windows MSAA policy.
2. Validate on macOS 15 ARM64 with Metal pixel tests: flat fields unchanged, edge
   smoothing, bounded sharpening, alpha preservation, viewport edges, HDR/MSAA
   combinations, resize/pressure and untouched UI drawn afterward. Then compare
   frame time and footprint on the same device/map/camera.
3. Build an opt-in native particle backend with explicit small ring/atlas budgets,
   corrected/reflected layouts and CPU fallback. Do not inherit desktop preset
   capacities. Initially retain weapon trails and unsupported effects on the CPU.
4. Validate consecutive-record compute readback against the CPU closed form,
   ring wrap/death, terrain clamp, visibility fades, additive zero-alpha sprites,
   ordering, reset and failure/pressure admission. Measure CPU/GPU time, live versus
   drawn range, atlas occupancy, upload volume and actual footprint before enabling
   it by default or changing admission to offload ordinary CPU particles.

Primary sources relative to `GeneralsMD/Code`: `Libraries/Source/WWVegas/WW3D2/`
(`dx11post.cpp`, `dx11runtime_posix.cpp`, `sdl3shadercompile.cpp`),
`GameEngineDevice/Source/PosixDevice/Render/` (`SdlGpuFramePost.cpp`,
`SdlGpuFrameHeat.cpp`, `PosixDevice9SceneEffects.cpp`),
`GameEngineDevice/Source/W3DDevice/GameClient/` (`DX11GpuParticles.cpp`,
`GpuParticlesUnsupported.cpp`, `W3DParticleSys.cpp`, `W3DView.cpp`),
`GameEngine/Source/GameClient/System/ParticleSys.cpp`,
`GameEngine/Include/GameClient/ParticleClosedForm.h` and
`GameEngine/Source/Common/GameLOD.cpp`.
