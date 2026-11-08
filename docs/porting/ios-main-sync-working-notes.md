# iOS main sync working notes — 2026-11-08

Claude-update start `003b6fe261ccbcc79ced6f04672bf67f25555de8`; Claude-working `d4682cd9ef25ae637a1565c9a5859f6f63ebafca` (update ahead 1, behind 0). Prior upstream base `5d6a328764a7bef8f977387077776ae6835750a7`. RoundedSpine main `6a651eb67c596d091fce2db0e9de76d296ef73ae`; authoritative upstream tip `287ef500994a07fd5535d198bef1b3c57750d681`, 104 upstream commits.

## iOS invariants
Preserve simulation behavior except upstream-authored gameplay/data; foreground GPU submission/startup/exit/touch/audio/sandbox rules; HLSL→SPIR-V→MSL and native-pass constant restoration; depth-capture submission ordering and sparse MSAA; native lighting/particles and bounded GPU overflow; memory-pressure policy; iOS shadow fallback/aircraft/tree silhouettes; LAN entitlement opt-in; unsigned ARM64 IPA workflow/tests. Platform differences stay behind named seams.

## Commit groups
(a) gameplay/data/logic: Turkiye, target/aircraft/vehicle/blast/fog/path/no-superweapon changes, mod removal. Take as upstream and revalidate E1.
(b) renderer/platform/UI: water reflection/refraction/FFT/shore reuse, shared hazard volume, heat/tracer/laser, side HUD/tooltips/tab/observer, frame cap, file I/O, AppleClang fix, shadow skinned-mesh guard. Reviewed for iOS/Metal.
(c) online/control: relay, mbedTLS, -online adapters/screens, OnlineBridge/TransportRoute/lobby, -relay, -control and LAN/online hardening. Apple applicability reviewed separately.
(d) tests/tooling/CI/docs: build/version, fixtures/pathlab, POSIX/GCC portability and integration commits.

## Tree audit
Recursive Git trees show 180 Claude-update changes and 430 main changes. Divergent/shared overlap is 33 paths; identical shared edits are accepted directly. The renderer-heavy overlaps include PosixDevice9, SdlGpuFrame, water, hazard, shadows/model/tree/particles, dx11runtime_posix, engineshader, test_gameengine and vendor.sh. The seven diagnostic workflows and helicopter-shadow.patch are excluded; the patch is evidence only.
