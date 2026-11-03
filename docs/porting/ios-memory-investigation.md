# iOS memory investigation

The supplied stderr identifies source `33b4919853c2`, running at 2868×1320 with
Classic off, Reforged lighting, AO, atmosphere, shadows and volumetric smoke on.
It predates the HDR/MSAA port. The user reports that the app closed itself.

## Recorded evidence

- The last footprint sample is **230.3 MiB at startup**, with 5913.7 MiB reported
  available. There are no gameplay footprint samples. This is not the run's peak.
- After about 606 seconds, three low-memory warnings arrive within 0.184 seconds.
  Each is followed by cache release and immediate AO/atmosphere reconstruction.
- The log stops there. It contains no normal exit or crash stack. This is
  consistent with possible iOS memory termination but does not establish it;
  a matching JetsamEvent/device report would be needed.
- Both increased-memory-limit and extended-virtual-addressing keys are requested
  and verified in the packaged IPA signature. Re-signing/profile grant and the
  device's actual allowance are separate from the build's requested keys.

## Source findings

1. **Effect cache cleanup was immediately undone.** The warning set a pending
   flag, and Present flushed/evicted optional resources. No pressure state stopped
   the next scene from rebuilding them. Repeated warnings repeated that cycle.
2. **Batch clearing retained peak allocations.** CPU stream/upload/constant/draw
   arenas used `clear()`, preserving capacity. The GPU upload/stream buffers also
   retained their largest size. Previous pressure cleanup did not release either.
3. **Texture fallback can be much larger than the source DDS.** If the GPU cannot
   sample BC formats, DXT textures expand into BGRA8 while source images remain.
   DXT1 expands by approximately 8×; DXT3/5 by 4×. This is a supported fallback,
   not proof of a leak. The old log does not identify its aggregate cost.
4. Resource mirrors remove entries when the corresponding engine resource dies.
   Mid-frame submissions default to three in flight. The shader description
   lookup has an entry cap. Distinct shader/pipeline variants remain cached for
   the device session; the supplied log does not show whether their count grows
   during gameplay. None of these observations establishes a steady leak.

At this resolution, nominal image sizes help put those findings in context:

| Allocation | Nominal MiB, excluding driver overhead |
| --- | ---: |
| One full-size BGRA8 or R32F image | 14.44 |
| AO depth plus two half-resolution RG32F images | 28.88 |
| Shared atmosphere/Firelight depth | 14.44 |
| 4096² D32F sun shadow | 64.00 |
| One 1024² DXT1 texture with all mips | ~0.67 compressed; ~5.33 expanded |

AO and atmosphere together therefore account for about 43.3 MiB of nominal
textures, not gigabytes. Their repeated reconstruction can worsen pressure at
the limit, but cannot by itself explain a multi-gigabyte asset footprint. These
are source-derived estimates, not measured allocations from the failing run.

## Changes

Commits `c5ed55d7` and `ba3ffa14` keep a lower optional-cache budget after an OS
warning until the renderer device is recreated. AO, atmosphere, per-pixel lights
and FFT resources stay paused; active MSAA switches off. Vertex lights/analytic
water retain their fallbacks, and saved preferences are retained. Mandatory scene
copies can return once; subsequent warnings do not evict them again. Failed
cleanup remains eligible for retry. CPU staging capacity and idle GPU staging
buffers are released after flushing and waiting for GPU completion.

The next device log records:

- `[IOS_MEMORY]`: footprint, available memory and sampled peak every 30 seconds,
  on warnings, and before/after cleanup. The sampled peak is not an exact peak.
- `[SDL_MEMORY_CACHE]`: mirrored texture/source-image/buffer nominal bytes,
  expanded texture count/bytes, pending retirements, CPU staging capacity,
  shader/pipeline counts and active HDR/MSAA/pressure state.
- `[SDL_TEXTURE]`: native block-compression capability and BGRA8 fallback.
- `[SDL_MEMORY]`: released staging sizes and the constrained-session decision.

Mirror totals exclude driver padding/cycling, native effect/shadow allocations,
assets without GPU mirrors, audio and other engine allocations. They must not be
treated as the process footprint or added to it. Comparing these readings over
time will establish whether textures, staging, shader variants or another
subsystem dominate. A device run is still needed to measure the savings and
determine whether the unexpected close is resolved.
