# Offline-first iOS integration plan

Working branch: `ios-main-merge` (from `Claude-update`). Upstream: `main`. Do **not** merge directly into `Claude-update` or change `main`.

## Scope and invariants

**Include:** offline engine/gameplay, graphics, UI, asset handling, stability, performance, and relevant cross-platform bug fixes.

**Exclude:** new online-service connectivity, online lobby/account/matchmaking/statistics adapters, and new mbedTLS/onlineconnection targets introduced solely to support that service. Do not remove existing LAN or offline behavior, and do not delete upstream source just because it is excluded from this integration.

**Preserve:** `ZH_PLATFORM_IOS`, iOS deployment target, Objective-C/Objective-C++ support, SDL/Metal backend, touch input, app lifecycle, iOS memory-pressure handling, and unsigned IPA build/packaging. Preserve unrelated macOS/Linux behavior where feasible.

**Merge strategy:** review the three-way diff against merge base `5d6a328764a7bef8f977387077776ae6835750a7`. Upstream changes must be integrated in coherent dependency groups, not isolated lines that introduce unresolved symbols. Never claim the merge is complete until full conflict enumeration and compilation. Avoid introducing the excluded online libraries through a transitive CMake link.

## Ordered goals and gates

- [x] **G1 — Define scope, preserve invariants, and document plan.** Baseline: two W3DTreeBuffer changes already committed on integration branch; prior audit at `docs/porting/ios-main-integration-audit.md`. Verify that `main` and `Claude-update` remain untouched.
- [ ] **G2 — CMake/build platform integration.** Compare `CMakeLists.txt`, vendor scripts, platform definitions and build workflows. Incorporate upstream non-online build improvements, but omit `mbedtls`/`onlineconnection` targets and linkage. Verify CMake configure with actual iOS SDK; static review is not a pass.
- [ ] **G3 — Core engine interfaces and initialization.** Compare `GameEngine.cpp`, `GlobalData.cpp`, options, and dependent headers; preserve iOS lifecycle and logging. Verify compile and relevant unit tests.
- [ ] **G4 — SDL/Metal graphics infrastructure.** Audit renderer, shaders, source registration, and device capabilities; preserve iOS Metal path. Verify shader generation, link and startup tests.
- [ ] **G5 — Water, reflections, terrain, tree shadows.** Integrate `W3DScene`, `W3DWater`, UI visibility and tree-shadow changes as a single compatible feature. Verify symbols, render passes, and device behavior.
- [ ] **G6 — Particles, smoke and hazard clouds.** Integrate policy, shader, backend and resource changes together. Review iOS GPU limits and fallback. Run relevant tests.
- [ ] **G7 — Offline gameplay, UI and options.** Merge options menu/catalog, gameplay and offline fixes; exclude online-only UX. Verify touch and settings persistence.
- [ ] **G8 — Performance, memory and stability.** Review allocators, frame pacing, memory-pressure handling, streaming and shutdown; run regression tests and performance comparisons.
- [ ] **G9 — Assets, manifests and packaging.** Reconcile resource changes, fingerprint manifest, shaders and unsigned IPA workflow. Verify required files exist and are bundled.
- [ ] **G10 — Full merge/conflict enumeration and validation.** Complete a real three-way Git merge or equivalent full tree integration, resolve all conflicts, run tests and build an unsigned iOS device IPA on a macOS/Xcode runner. Record build logs and failures; repeat until green.
- [ ] **G11 — Final review.** Audit diff for unintended online dependencies and iOS regressions; only then propose merging integration branch into `Claude-update`.

## Execution discipline

For each goal: (1) inventory changed files and dependencies, (2) classify iOS impact, (3) implement on integration branch, (4) inspect diff, (5) run available checks, (6) record commit and remaining risks. Do not mark a goal complete on code inspection alone if its gate requires compilation.

## Current known limitations

GitHub's compare endpoint truncated `main` changed-file enumeration at 300 entries; overlap inventory is partial. GitHub file editing is available here but a complete local checkout, macOS SDK, and successful IPA build have **not** been demonstrated. The two earlier W3DTreeBuffer changes are uncompiled. A workflow file existing in the repository does not establish that it has run.

## G2 implementation checkpoint: offline/LAN build boundary

Commit `0aa2ad3` explicitly removes `GameNetwork/Online/OnlineConnection.cpp` from the recursively globbed engine source list. This is a forward-compatible guard: the file is currently absent from the iOS branch, so the current source list is unchanged. The upstream hosted-service `onlineconnection` and `mbedtls` targets remain excluded.

Do **not** remove existing GameSpy-era or LAN code, socket/platform support, or offline skirmish networking. The upstream `SdlHazardCloudShared*` files are graphics features, not online dependencies; they remain pending a coherent source/shader integration in G6.

**Validation pending:** verify the offline source list, check that existing LAN code still compiles and links, and run the iOS IPA workflow. G2 remains unchecked.

### G2 upstream vendor comparison checkpoint

- Ported the independent upstream `expand_source()` archive support for `*.tar.bz2` alongside existing `*.tar.gz` (`cc0f45e`). This does **not** download or enable mbedTLS; `install_mbedtls` remains absent.
- Kept the iOS branch's stronger `curl` retry, timeout, and partial-file cleanup behavior rather than overwriting it with upstream's simpler download call.
- Upstream `SdlHazardCloudShared*` CMake references are deferred to G6 until the corresponding source/shader files are integrated.
- Upstream version bump and fingerprint build command require independent review of manifest and generator dependencies; not blindly copied.
- The added tar extraction branch has been source-reviewed but not executed in a shell test here. G2 still requires build validation.

### G2 version/fingerprint checkpoint

- Integrated upstream generated `BuildVersion.h` values `VERSION_STRING=v3.8.0` and `VERSION_MINOR=8` (`307e7bc`), leaving the iOS build configuration intact. **This is metadata alignment, not evidence of feature parity with upstream v3.8.**
- Retained the iOS-specific fingerprint host-tool implementation (`zh_add_host_executable` and `$<TARGET_FILE:build_fingerprint>`), rather than replacing it with upstream's target-execution form. A device-target binary cannot be executed on the macOS build host.
- Retained iOS's SDL compute GPU-particle source selection rather than adopting upstream's SDL no-backend fallback; the backend choice must be validated during graphics work.
- Existing FFmpeg flags are iOS-aware; do not introduce upstream's macOS-only minimum-version flags into the iOS target.
- Build/version changes are source-reviewed but uncompiled; G2 remains open.

### G2 retrospective correction and manifest analysis

- **Correction:** reverted the premature `v3.8.0` metadata bump in `c969a83`. The integration branch still lacks many upstream features; its generated version remains `v3.6.0` until a release decision is made.
- **Verified:** `Tools/fingerprint/build_fingerprint.cpp` has identical file contents on `main` and `ios-main-merge`. The branch-specific `CMake/HostTools.cmake` exists only on iOS; retain it to compile and execute the fingerprint generator on the macOS host when cross-compiling.
- **Manifest comparison:** upstream has 122 manifest paths absent from the iOS manifest; iOS has 87 absent from upstream, including host-tool, iOS lifecycle, touch, graphics, and test files. Do **not** replace the iOS manifest with upstream's: it would discard iOS-specific fingerprint inputs. Reconcile paths only when corresponding upstream resources are actually integrated.
- **Review limitation:** manifest path differences alone do not establish that the underlying files exist in a branch or are packaged; existence and fingerprint execution require separate verification.
- **Decision:** keep the existing iOS `ZH_PLATFORM_IOS`, host-tool CMake, SDL compute particle backend and FFmpeg toolchain behavior. Upstream features may be adapted to these, but not substituted for them.
