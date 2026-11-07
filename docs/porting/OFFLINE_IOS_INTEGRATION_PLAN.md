# Offline-first iOS integration plan

Working branch: `ios-main-merge` (from `Claude-update`). Upstream: `main`. Do **not** merge directly into `Claude-update` or change `main`.

## Scope and invariants

**Include:** offline engine/gameplay, graphics, UI, asset handling, stability, performance, and relevant cross-platform bug fixes.

**Exclude:** new online-service connectivity, online lobby/account/matchmaking/statistics adapters, and new mbedTLS/onlineconnection targets introduced solely to support that service. Do not remove existing LAN or offline behavior, and do not delete upstream source just because it is excluded from this integration.

**Preserve:** `ZH_PLATFORM_IOS`, iOS deployment target, Objective-C/Objective-C++ support, SDL/Metal backend, touch input, app lifecycle, iOS memory-pressure handling, and unsigned IPA build/packaging. Preserve unrelated macOS/Linux behavior where feasible.

**Merge strategy:** review the three-way diff against merge base `5d6a328764a7bef8f977387077776ae6835750a7`. Upstream changes must be integrated in coherent dependency groups, not isolated lines that introduce unresolved symbols. Never claim the merge is complete until full conflict enumeration and compilation. Avoid introducing the excluded online libraries through a transitive CMake link.

## Revised ordered goals and acceptance gates (2026-11-07)

**Status:** G1 complete; G2–G13 open. A source review or a committed patch is not a successful build. Use the stages below in order; where dependencies require it, prepare a change in its stage and validate it in the next applicable CI gate.

- [x] **G1 — Scope, branch isolation and integration rules.** Keep work on `ios-main-merge`; preserve offline campaign/skirmish, existing LAN, iOS touch, SDL/Metal, lifecycle, memory diagnostics and unsigned IPA. Exclude only *new hosted online-service features* and their mbedTLS/onlineconnection dependencies; do not blanket-remove existing GameSpy/LAN paths.
- [ ] **G2 — Full upstream inventory and dependency classification (active).** The 300-file limit has been bypassed using four consecutive ancestry segments: 424 unique upstream-history touched paths across 110 commits, 180 iOS-side paths, and 34 shared touched paths (see `docs/porting/upstream-segmented-change-inventory.md`). Next classify semantics/dependencies and perform a true three-way merge simulation. **Gate:** reconcile final net diff, renames and actual conflict hunks; segmented touched paths are not a conflict count.
- [ ] **G3 — Build system, host tools and baseline CI.** Audit CMake, vendor scripts, iOS deployment SDK, ObjC/ObjC++, FFmpeg, fingerprint generation and unsigned IPA workflow. Retain `CMake/HostTools.cmake` and host-built fingerprint tool; do not substitute upstream's target-built executable in a cross-compile. Keep existing iOS curl retries and SDL compute particle source selection. Keep `v3.6.0` until release parity is assessed. Integrate safe upstream build improvements without new online targets. **Gate:** actual iOS configure, host fingerprint execution, dependency fetch and baseline build logs.
- [ ] **G4 — Manifest and asset prerequisite audit.** Reconcile upstream-only 122 and iOS-only 87 fingerprint manifest paths *against real files*; preserve iOS-specific entries. Stage only corresponding verified assets and shaders, without wholesale manifest replacement. **Gate:** all manifest paths resolve, generator completes, and required resource packaging is understood.
- [ ] **G5 — Core engine, shared interfaces and initialization.** Three-way merge `GameEngine.cpp`, `GlobalData.cpp`, `OptionsCatalog.cpp` and dependent headers. Preserve iOS lifecycle hooks, diagnostics and platform settings. **Gate:** compile and engine startup tests.
- [ ] **G6 — SDL/Metal renderer and shader infrastructure.** Adapt upstream rendering capabilities to the iOS GPU architecture; retain native SDL compute particles, existing shader/Metal paths and iOS GPU budget/fallback behavior. **Gate:** shader compilation, link and device startup.
- [ ] **G7 — Water, reflections, terrain and tree shadows.** Revisit the *two already committed but uncompiled* `W3DTreeBuffer.cpp` changes. Integrate upstream `W3DScene`, `W3DWater`, world-space UI filtering, breeze cache and shadow policy with dependent headers and iOS rendering passes. **Gate:** compile and visual regression tests.
- [ ] **G8 — Particles, smoke and hazard clouds.** Review upstream `SdlHazardCloudShared.cpp`, `SdlHazardCloudSharedShader.cpp`, model/test additions and source registration as a cohesive feature; do not register files before they exist on the branch. Adapt to iOS SDL/Metal capabilities, not upstream's D3D-only assumptions. **Gate:** cloud/particle tests, shader tests and device rendering.
- [ ] **G9 — Offline gameplay, LAN, UI and settings.** Merge offline gameplay, `ParticleSys.cpp`, options menu and HUD changes; preserve touch, settings persistence and LAN behavior. Do not accidentally import hosted lobby/account/matchmaking/stats UI. **Gate:** campaign/skirmish, touch, options and LAN smoke tests.
- [ ] **G10 — Performance, memory, stability and shutdown.** Check renderer frame pacing, texture/streaming resources, iOS memory-pressure callbacks, allocations, diagnostics, lifecycle and shutdown. **Gate:** repeatable performance/memory baselines and no new crash regressions.
- [ ] **G11 — Full tree integration and conflict resolution.** Complete a real three-way merge (or equivalently verified full-tree integration), retaining necessary upstream fixes and iOS-specific additions. Review every remaining changed file, including docs, tests, shaders, packaging and vendor references. **Gate:** no unresolved conflicts, missing sources or unintended online-service dependencies.
- [ ] **G12 — Full device IPA build and regression suite.** Run macOS/Xcode CI, configure for iOS, build host tools, compile/link, package unsigned IPA, inspect bundle contents, and run automated + device smoke tests. Record workflow run URLs, logs and failing cases. **Gate:** green build and validated offline, LAN, touch, graphics and lifecycle behavior.
- [ ] **G13 — Release/version decision and final merge.** Compare integrated feature set against upstream `v3.8.0` before updating version metadata; audit licensing, resource integrity and diff. Merge `ios-main-merge` into `Claude-update` only with approval after G12. **Gate:** reviewed merge candidate and verified release notes.

### Known decisions and corrections to enforce

| Area | Decision |
| --- | --- |
| Online services | Exclude new hosted service, `mbedtls`, `onlineconnection`; retain existing LAN/GameSpy-era code |
| Fingerprint | Same `build_fingerprint.cpp` upstream and iOS; retain iOS host executable CMake and generator expression |
| Version | `v3.8.0` bump from `307e7bc` was reverted in `c969a83`; current `v3.6.0` is deliberate |
| Manifests | 122 upstream-only and 87 iOS-only paths; verify actual files, reconcile additively |
| Vendor script | `.tar.bz2` support integrated (`cc0f45e`); retain iOS retry/timeout logic |
| GPU particles | Preserve iOS SDL compute backend; validate upstream changes against it |
| Water/trees | Existing include and modern-mirror shadow guard are not compiled yet; review dependent types and render behavior |
| Hazard clouds | Defer shared SDL files and test source registration until all source/shader dependencies exist |
| Build validation | No verified configure, compilation, workflow run or IPA yet; never claim otherwise |

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

### G2 PR inventory checkpoint

- Enumerated **all 423 changed paths** in existing PR #4 using the paginated PR file endpoint (source list saved in `docs/porting/pr4-changed-file-inventory.md`, commit `a5b9700`).
- Provisional filename-only buckets: 152 engine, 130 resources, 70 renderer, 24 build/tools/tests, 9 hosted-online candidates and 38 other. **Do not treat the 9 candidates as all online dependencies or assume every candidate should be removed.**
- The PR file list is complete for the *PR's net diff*, but **not** a complete union of `main`-only and `Claude-update`-only changes since the merge base; it cannot establish actual Git conflict hunks. Goal G2 stays unchecked until both branch-side changes and dependencies are enumerated.

### G2 three-way baseline checkpoint (2026-11-07)

- Merge base: `5d6a328764a7bef8f977387077776ae6835750a7`.
- `main` is 110 commits ahead of base, but the compare endpoint provides **only the first 300 changed paths**.
- `Claude-update` is 47 commits ahead of base, with **180 changed paths** returned by the same endpoint.
- The complete PR #4 net diff includes **423 paths** (`docs/porting/pr4-changed-file-inventory.md`). It is not interchangeable with the upstream-only changes since base.
- Ten confirmed path overlaps in the first 300 upstream paths: `.github/workflows/build-ios.yml`, `CHANGELOG.md`, `GeneralsMD/Code/BuildFingerprint.manifest`, `GeneralsMD/Code/CMakeLists.txt`, `GeneralsMD/Code/Data/Patch.str`, `GameEngine.cpp`, `GlobalData.cpp`, `OptionsCatalog.cpp`, `OptionsMenu.cpp` and `ParticleSys.cpp` (the last five under their respective `GeneralsMD/Code/GameEngine/Source` directories).
- **Ten is a lower bound, not a final conflict count.** A path touched on both branches may auto-merge; a path changed on one branch may still have semantic dependency conflicts. Full upstream enumeration and actual merge simulation remain necessary.

### G2 truncation workaround completed

Four consecutive upstream ancestry comparisons from merge base to `main` returned 245, 139, 119 and 8 changed paths (29 + 25 + 46 + 10 = 110 commits). Their union has **424 unique history-touched paths**, with **34 overlapping** the iOS branch's 180 changed paths. Saved at `docs/porting/upstream-segmented-change-inventory.md` (`16fd684`).

**Interpretation:** this solves the 300-file per-comparison truncation. Segment net diffs can omit edits reverted within a segment; the union can include paths changed and later reverted. Do not label the 34 overlaps as Git conflicts. A true merge simulation and semantic dependency classification are still pending.

### G2 executable three-way audit added (2026-11-07)

- New branch-local workflow: `.github/workflows/ios-merge-audit.yml` (commit `e3973d0`). On push or manual dispatch it uses full Git history, `git merge-tree --write-tree --messages HEAD origin/main`, and `git diff` against the actual merge base.
- Captures `merge-tree.txt`, `merge-tree-exit-status.txt`, upstream and integration net path lists, and overlap path lists as a GitHub Actions artifact.
- **Non-mutating:** does not merge or write to `main`/`Claude-update`; only the workflow file was committed on `ios-main-merge`.
- **Execution not yet verified:** GitHub Actions run ID, artifact, actual merge status and conflict count remain unconfirmed. The container cannot reach github.com directly; use Actions run evidence before claiming simulation results.
- Additional exact three-revision source review: `W3DWaterTracks.h` upstream removes Linux-only guards while iOS broadens guards to `defined(__linux__) || defined(ZH_PLATFORM_IOS)`. Keep iOS guards pending verification against implementation files.
