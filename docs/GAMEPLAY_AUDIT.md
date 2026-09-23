# RocketVolley gameplay audit

Audited repository version 0.12 on September 17-23, 2026. Source inspection, executable headless tests, local hidden-window graphical smoke runs, and inspection of captured screenshots were used. This was **not hands-on keyboard/controller play-testing or a subjective audio review**. No Git commands were run. The September 23 fair-play iteration below supersedes the earlier bot shot assistance.

## Scope and existing systems

- [Game.cpp](../src/Game.cpp): 120 Hz gameplay loop, handling, AI, rendering, procedural music/effects, menus, settings, 2v2, 3v3, split-screen, optional Power Volley, replay, Arcade Cup, Academy, PB ghosts, Target Challenge, and Pilot Record. Existing modes and progression were retained.
- [PhysicsWorld.cpp](../src/PhysicsWorld.cpp): Jolt rigid bodies, collision layers, gravity, damping, restitution, continuous ball collision, and now per-step contact reporting and continuous car collision.
- [GameplayLogic.cpp](../src/GameplayLogic.cpp): prediction, dodge direction, target scoring, and now shared team planning, input-edge retention, deadzones, bounds and match-outcome helpers.
- [MultiplayerProtocol.cpp](../src/MultiplayerProtocol.cpp): explicit little-endian, finite-value-checked input/snapshot serialization. This is still a local protocol boundary; no online transport was added.
- [CMakeLists.txt](../CMakeLists.txt), [Windows release workflow](../.github/workflows/release.yml), and [release documentation](RELEASE.md): dependency versions, test discovery, CPack ZIP/checksum, and publishing flow reviewed. Dependency pins, VS 2022 runner, release version/tag behavior, and packaging remain intact. CI now runs five headless test targets on Windows, including save-failure and package-verifier coverage.
- Assets: cars, arena props, effects and sounds are procedural; the three checked-in PNGs are README illustrations. No new asset dependency or art-generation pipeline was introduced.
- Persistence: version-3 settings keys, RVGHOST 1, protocol v4 and existing records are retained. Tests suppress profile reads/writes. No migration or record reset was introduced.

## Findings ranked by player impact

References below name the actual functions in the linked source files. "Fixed" means implemented and exercised where described under verification; it does not imply every human-play scenario has been tested.

| Priority | Confirmed issue and consequence | Evidence | Outcome |
| --- | --- | --- | --- |
| P0 | A competitive ball could escape over the 6.2 m cage and leave play unresolved. | `createArena`, `fixedUpdate` in Game.cpp; only practice previously checked bounds. | **Fixed:** `resolveCompetitiveBall` awards an escaped-ball fault against the last toucher, or server if untouched. Invalid/lost ball coordinates also resolve. |
| P0 | Scoring used a 12 cm early height threshold instead of floor contact; the clock could abruptly end a live rally. | `fixedUpdate`, `academyFixedUpdate`. | **Fixed:** Jolt floor contact resolves the point; duplicate scoring is guarded; at 0:00 the live rally finishes, an equalizer starts overtime, and a decisive point ends the match. |
| P1 | Jump/dodge/power presses vanished on render frames with no simulation step; resetting the accumulator inside a step could make it negative. | Previous `update` loop and `launchServe`. | **Fixed:** retained input edges are consumed exactly once by `advanceSimulation`; elapsed time is deducted before state-changing updates. |
| P1 | Left steering disagreed with directional dodging, reverse steering was inconsistent, airborne heading drifted from body yaw, and inverted cars could get stuck. | `keyboardControls`, `directionalDodge`, `driveCar`, `aiControls`. | **Fixed:** coherent left/right and reverse steering, physical airborne yaw, brief protected dodge spin, upright recovery, and continuous analog deadzone. Boost without forward input now accelerates appropriately. |
| P1 | Nearby wall/floor bounces could count as car touches; gentle contacts were missed; rolling contact could create four-touch faults. | Previous `checkBallImpact` required a velocity delta >4.1 and a nearby car within 3.55 m. | **Fixed:** thread-safe Jolt contact pairs drive attribution independently of sound strength. A car must separate for 75 ms before earning another touch. |
| P1 | Bots returned balls beyond their physical reach, then retained aimed velocity replacement after the first fix. | Removed `tryAiBallTouch`; `checkBallImpact`, `PhysicsWorld::contactImpact`. | **Fixed:** bots use actual driving/jumping contacts and the same momentum-based contact response as humans. Bot-only steering and impact advantages are removed. |
| P1 | Prediction used infinite-height walls, incorrect inset, no wall restitution/damping, and only net-center crossings; markers could describe a later bounce. | `predictBallMotion`, removed `ballTimeToHeight`. | **Improved:** finite walls, actual court dimensions, damped/restituting ball motion, net face/top sphere contact, Rookie speed limits, and first-landing termination. Shared cached trajectory feeds AI and HUD. |
| P1 | Unavailable P1 could retain striker priority; each bot independently selected roles; support abandoned defense for boost and only avoided P1. | `strikerForTeam`, `supportForTeam`, `aiControls`. | **Improved:** a team snapshot chooses available striker/support with facing, velocity, boost, arrival estimates and hysteresis. Support preserves defensive coverage during threats; all teammates receive separation room. |
| P1 | Retreating defenders steered their nose toward the target while reversing, moving the rear away from the interception lane. | `aiControls` reverse branch, exposed by Pro 3v3 simulation. | **Fixed:** reverse control aims the rear at the target; focused regression added. |
| P1 | Academy's first driving lesson put a gate beyond the net; Pro aerial/placement feeds could not clear the net. | `AcademyGatePositions`, `setupAcademyLesson`. | **Fixed:** driving gates stay on the learner's half; ballistic feeds are calculated for each difficulty and tested to reach the player side. |
| P2 | Bot shot aiming concealed weak physical approaches and serves. | Removed `tryAiBallTouch`; `aiControls`, `resetRound`. | **Reworked September 23:** serve setup moves closer to the net for every player; bots stage behind descending interceptions and commit through contact. Shot direction comes from physical approach, rather than scripted lane targeting. |
| P2 | Split-screen shared one camera toggle; controller lacked pause/replay skip; help could obscure ongoing play. | `handleGlobalInput`, `updateLocalCoopCameras`. | **Fixed:** independent P1 C / P2 Y cameras, Start pause, A replay skip, B menu back, auto-pause on a connected P2 controller disconnect, and pause while help is open. |
| P2 | Free training repeated a long countdown and manual reset randomized the shot. | `resetTrainingServe`. | **Fixed:** subsequent feeds use a one-second countdown and R repeats the same feed. |
| P2 | Academy's aerial objective accepted ground returns; placement judged height rather than landing. | `academyFixedUpdate`. | **Fixed:** aerial lesson requires an airborne player touch plus a legal return; placement uses floor contact. |
| P2 | Court team-color surfaces were below the floor plane; notices obscured the ball; no off-screen pointer or explicit landing urgency; ball rendering ignored physical rotation. | `drawArena`, `drawHud`, `drawLandingIndicator`, `drawBallTransform`; captured screenshots. | **Improved:** visible colored halves, notices outside central play, ball locator per viewport, shrinking/pulsing landing ring, and rotating ball seams. Ball-camera aiming now uses the smoothed camera position. |
| P2 | Gameplay regressions were primarily embedded in a graphical smoke test; hosted release only checked protocol/helpers. | Original CMake/workflow and `Game::Impl::run`. | **Fixed:** dependency-free logic tests plus real Jolt/game integration tests, labeled `headless`. The graphical test remains local/optional; its aerial coverage no longer waits for random AI rally length. |
| P1 | Portable ZIP placed MSVC runtime DLLs in `bin/`, separate from the executable. | `InstallRequiredSystemLibraries` default destination in CMakeLists.txt; confirmed by inspecting the generated archive. | **Fixed:** runtime DLLs install beside `RocketVolley.exe`; archive validation checks executable identity, required files and checksum. |
| P3 | Snapshot decoder accepted unknown match-state values and negative time. | `decodeWorldSnapshot`. | **Fixed:** rejected without changing protocol layout/version. |
| P3 | Settings truncate the destination directly; ghost replacement removes the old file before rename. | `saveSettings`, `saveAcademyGhost`. | **Fixed in follow-up:** both writers now stage, flush and replace through `SaveFile.cpp`, preserving old files on ordinary failure. Historical backups and cross-file transactions remain future work. |
| P3 | Replay drops the front of a vector; gameplay, UI and graphical tests remain tightly coupled. | `captureReplayFrame`, Game.cpp. | **Replay fixed in release-readiness pass:** bounded deque removes repeated front shifting; retention/order/clamping/reset tested. Gradual subsystem extraction remains future work. |

## Implementation iterations

1. **Rally integrity and input:** established the audit, added synchronized contact collection following [Jolt 5.5.0's callback contract](https://github.com/jrouwe/JoltPhysics/blob/v5.5.0/Jolt/Physics/Collision/ContactListener.h), and integrated scoring, contact attribution, escaped-ball recovery and input retention. Contact callbacks record body identifiers and, since September 23, impact normal/closing speed; gameplay changes occur after Jolt finishes updating. Speculative pairs are filtered by separation/closing speed with a small solver tolerance.
2. **Handling and team play:** corrected steering/recovery, replaced repeated per-car prediction with a shared flight and role snapshot, constrained AI assistance to contact, improved defensive behavior and shot selection. Simulation evidence prompted further reverse-path and serve tuning.
3. **Readability and training:** corrected Academy geometry/feeds, faster repeatable practice, separate split cameras/controller flow, visible court colors, landing timing and off-screen indicators. Screenshot review prompted relocating notifications and simplifying ball seams.
4. **Verification and release gate:** added direct tests of the production input loop, Jolt collisions, match transitions, practice, AI roles and sustained matches. Kept the graphical smoke test for rendering/asset/UI coverage; CI selects only headless tests.

## Verification

Initial PATH inspection missed an installed VS 2019 Build Tools compiler and Windows SDK. Those were subsequently located and used successfully; no build was attempted against an unavailable toolchain. The bundled CMake 3.20 was too old, so CMake 3.31.6 and the repository's pinned raylib 6.0/Jolt 5.5.0 sources were downloaded into ignored `.cache` directories. Local configure used those sources without Git or further dependency fetching. This local compiler differs from the preserved VS 2022 CI configuration.

- `RocketVolleyLogicTests`: input-edge preservation/one-shot consumption, analog deadzone, score cap/final rally/overtime outcomes, actual wall dimensions and restitution, high-wall escape, net face/top, first landing, unavailable players and 2v2/3v3 role exclusivity, packet validation, and **500 varied trajectory simulations**.
- `RocketVolley --headless-test`: constructs the production game/Jolt world without initializing a window/audio/model or reading/writing a profile. Covers early/fast floor scoring, duplicate-point prevention, escape attribution, kickoff/overtime/GameOver/progression transitions, wall/net containment and legal clearance, false versus gentle/continuous contacts, removal of bot proximity hits, steering/recovery, production input loop at substep frame times, predictor versus Jolt comparison, exact training retries, practice scoring isolation, Academy feeds, and four **60-second** AI simulations across 2v2/3v3 and Rookie/Pro.
- `RocketVolley --protocol-test`: retained existing compatibility gate.
- `RocketVolley --smoke-test`: ran locally with a hidden window on Intel UHD OpenGL 3.3; renders real scenes and exercises existing Cup, career, customization, ghost codec/playback, training/challenge, power-up, boost-pad, recovery, split-screen and camera checks. Captures are under ignored `build-audit/`. Gameplay, split-screen and training screenshots were inspected. This is automated visual coverage, not manual driving or controller testing.
- Full Release build succeeded with MSVC 19.29 and no project-code warnings. **Initial audit: all four CTest targets passed** (23.07 seconds total): logic, protocol, real simulation and local graphical smoke. The graphical run recorded a 9.69 m aerial peak, two jumps, and a completed sprint lesson.
- Seeded 60-second AI runs completed without non-finite state or resource-bound failures. Metrics below count completed rallies; touches in an unfinished rally are excluded. These are regression diagnostics, not human balance ratings.

| AI scenario | Completed touches | Points | Longest completed rally |
| --- | ---: | ---: | ---: |
| Rookie 2v2 | 14 | 1 | 14 |
| Rookie 3v3 | 16 | 2 | 15 |
| Pro 2v2 | 19 | 6 | 6 |
| Pro 3v3 | 25 | 2 | 21 |

The initial dependency-free `ROCKET_VOLLEY_LOGIC_ONLY=ON` configure/build/test also passed (1/1). The Windows ZIP was rebuilt with adjacent runtime DLLs; its SHA-256, packaged executable identity, documentation and image contents were verified. No test executable or cache files are packaged.

Reproduce with an available C++20 toolchain and CMake >=3.24:

```sh
cmake -S . -B build-release -DBUILD_TESTING=ON
cmake --build build-release --config Release --parallel 4
ctest --test-dir build-release -C Release -L headless --output-on-failure
# Only on a machine with working graphics/audio:
ctest --test-dir build-release -C Release -R rocket_volley_smoke --output-on-failure
```

For helpers alone, without raylib/Jolt downloads:

```sh
cmake -S . -B build-logic -DROCKET_VOLLEY_LOGIC_ONLY=ON
cmake --build build-logic --config Release
ctest --test-dir build-logic -C Release --output-on-failure
```

## Limits and recommended next work

- Human play feel, controller hardware variations and subjective audio balance need manual evaluation. A passing simulation demonstrates behavior/stability, not that every skill level finds the tuning enjoyable.
- Prediction is an approximation, not a second Jolt world: spin/friction, wall lips, moving cars and future power-ups can change the landing. Do not treat the landing ring as a guarantee.
- AI coordinates interception/coverage and avoids teammates, but does not yet execute deliberate multi-car set/spike combinations. Bot-only aimed velocity replacement has been removed. Both human and AI cars receive the same bounded arcade lift/carry on physical contact; compare handling and difficulty through human play before further tuning.
- Seeded AI-versus-AI metrics are diagnostics, not a human win-rate or difficulty certification. Pro exchanges remain shorter than Rookie exchanges; planned placement and aerial timing remain the highest-value AI follow-ups. No artificial rally timer was added.
- Ground handling remains an arcade velocity controller with simple height/orientation grounding, not a suspension model. Multi-body piles, wall lips and airborne recoveries deserve targeted hands-on testing.
- Split-screen still shares match-wide HUD elements, uses one gamepad plus keyboard and a fixed 1280x720 layout. General controller rebinding, accessibility scaling and broader resolution options remain opportunities. Independent audio and camera-shake settings are now implemented.
- No networking transport, replay file format, new mode, major dependency, or save schema was added. Cross-file save transactions, historical backups and smaller gameplay modules remain technical follow-ups.
- The hosted GitHub Actions workflow has not been executed from this workspace; no release was published. Existing checked-in release ZIPs are not rewritten.

## Manual play-test checklist

1. On keyboard and gamepad, turn both ways forward/reverse; feather the stick near center; tap jump at high refresh rates; double-jump, dodge left/right/back, boost without throttle, and recover from an inverted landing. Check for unwanted snaps or missed input.
2. Try soft roof/nose contacts, rolling contact, wall/net ricochets near a car, low saves, net-top clips and balls over the cage. Check point ownership, four-touch faults and serve recovery.
3. Let the clock reach 0:00 mid-rally; tie on that rally and win the next one. Exercise replay skip, restart, Cup advancement and return to menu.
4. Compare Rookie/Pro 2v2 and 3v3. Deliberately leave your half, get demolished, retreat backward and run low on boost. Check that the AI takes over, covers, rotates and does not return balls without touching them.
5. Use P1 C and P2 Y independently; track a high overhead ball and a ball behind the car; try Start pause, A skip, F1 help and unplug/reconnect P2. Look for HUD obstruction or misleading direction pointers near walls.
6. Complete all four Academy lessons on both difficulties, then load an existing PB ghost. In free training, retry an identical feed with R; in Target Challenge, complete ten shots and inspect saved score/combo. Check an existing Pilot Record and Cup record after restarting.
7. Listen to hit/jump/boost/score cues during long rallies. Check the new contact-driven soft-hit feedback and overall music/effect balance; audio initialization alone does not verify the mix.

## Focused engineering follow-up - September 18

[Player use cases and acceptance criteria](USE_CASES.md) define practice-feed, ten-shot challenge and persistence scenarios with explicit automated/manual boundaries. All six real-physics practice feeds passed before further changes, so their tuning was retained. Added full challenge progression, duplicate landing and combo-reset checks.

Fixed the confirmed save-replacement defect using a small shared writer with exclusive staging, flush/close checks, atomic per-file replacement and failure cleanup. A new dependency-free `rocket_volley_saves` CTest target covers exact bytes, real replacement failures, locked-file recovery on Windows and staging cleanup. Tests use isolated scratch files; existing profiles are untouched. The headless CI label automatically includes this new target. POSIX persistence is implemented but untested locally; multi-file consistency and power-loss recovery are not claimed.

Follow-up verification: Release build passed; **all five CTest targets passed** in 23.44 seconds (four headless targets plus the local graphical smoke test). The dependency-free configuration passed **2/2** tests. Rebuilt the portable ZIP and verified its checksum, executable identity, adjacent runtimes and current documentation. No hosted workflow run or publication was performed.

## Release-readiness follow-up - September 18

- Reproduced interruption defects before fixing them: loss of focus left a rally running; simultaneous P2 disconnect/pause could cancel auto-pause; co-op could resume with P2 still missing. The focused test run had five failed assertions (including downstream resume checks). All interruption cases passed after the fixes.
- Centralized pause/help transitions; focus loss and controller disconnection pause play, co-op waits for a connected controller, and returning focus/reconnecting never automatically resumes. Help is modal, freezes gameplay inputs and cannot leave a kickoff running underneath. Hidden graphical smoke tests supply availability explicitly; they do not pretend to exercise real controller hardware.
- Replaced replay front-erasure with a bounded deque. Tests cover 650 captures, the latest 300 retained frames, chronological playback, index clamping and kickoff reset. No frame-rate improvement is claimed without profiling.
- Added `tools/VerifyPackage.ps1` and negative ZIP fixture tests. The existing Windows publishing workflow now verifies runtime placement, required docs/images and exact tested-executable identity as well as checksum before upload. Dependency versions, compiler runner and publishing semantics remain unchanged.
- Manual release acceptance still requires Alt-Tab/reconnect on real hardware, a clean-machine ZIP launch, an existing-profile restart and subjective play/audio evaluation. Local automated success alone is not a production certification.

Verification: Release build succeeded. Logic, saves, graphical smoke, protocol and Jolt simulation passed in the full run. The new package-verifier test exposed a Windows PowerShell assembly-loading issue; after adding the explicit compression assembly load, its focused rerun passed. All six targets have passed on the final code. The ZIP was rebuilt and accepted by the same package verifier used by CI. No hosted workflow execution or publication was performed.

## Push-readiness follow-up - September 22, 2026

Confirmed and fixed optional shortcut conflicts with custom bindings: driving could also change difficulty, ball elasticity, training feeds or ghost visibility. All six affected keys now defer to assigned actions. Existing valid custom controls remain compatible. Profile validation now rejects F1/Enter just like the Controls menu; invalid layouts fall back to defaults through the existing loader. Key capture receives reserved keys before global help processing, and rejection feedback appears inside the capture dialog.

Fixed test discovery when the explicit test-only option was combined with `BUILD_TESTING=OFF`. The local packaging target now passes its build configuration to CPack; workflow artifacts use exact versioned filenames instead of globs that could include old checked-in releases. No Git commands, publishing actions or schema migrations were performed. See UC-09 through UC-11 in [USE_CASES.md](USE_CASES.md).

September 22 verification: Release build passed; full suite **6/6 passed** in 26.51 seconds, including graphical smoke. Fresh test-only configure with `BUILD_TESTING=OFF` passed **3/3** in 2.80 seconds. Remaining hardware/clean-machine acceptance limitations still apply.

The Release ZIP passed the CI package verifier. Its executable was extracted to a fresh local directory and both `--protocol-test` and `--headless-test` exited successfully. These test modes bypass profile access. This verifies the packaged build on this development machine, not on a clean Windows installation.

## Practice and comfort iteration - September 22, 2026

Implemented three player-facing additions within the existing architecture:

1. **Repeatable training drills:** L/D-pad Right locks the current shot for automatic repeat after both success and failure. Tab/D-pad Up selects a new feed while retaining the lock; R/D-pad Down retries immediately. New-session reset clears the lock and statistics. Existing Challenge/Academy rules are isolated from these commands. Fixed the old free-training accuracy denominator, which counted resets and in-flight shots: the HUD now uses completed attempts and adds current/best return streaks.
2. **Audio and camera preferences:** music, effects and impact shake have independent 0-100% settings. Dynamic ball-hit volume respects the effects setting; muted music does not keep restarting. A keyboard/gamepad menu is accessible from the main screen and while paused (F2/Y), and returning keeps the match paused. This is an impact-shake setting, not a general reduced-motion mode; camera tracking, FOV changes and particles remain.
3. **Optional point replays:** disabling replays retains the scoring celebration, then advances directly to the next serve or final results. Default replay behavior remains enabled. Replay selection does not change scoring, serving ownership or progression.

Added optional keys to settings version 3 without changing existing records, ghost or network formats. Extracted production settings stream parsing/encoding for headless round-trip tests, including bindings and earned records. Fixed parsing of malformed numeric prefixes (for example `70garbage`), which are now ignored. Old profiles without preference keys retain the original mix, full shake and replays. Preference files still use the previously verified atomic writer.

Tests exercise automatic locked retries, miss/duplicate landing/streak behavior, mode isolation, menu changes without an audio device, zero-shake camera movement, production-profile round-trip, legacy defaults, malformed values, and both replay flows through final results. Graphical smoke additionally exercises music mute/unmute and captures the new options screen. Screenshot inspection covers layout/readability; subjective audio balance and physical controller input still require manual acceptance.

Final verification on September 23: Release build succeeded and **6/6 CTest targets passed** in 24.45 seconds. The dependency-free configuration passed **3/3**. New training, main-menu and audio/comfort screenshots were inspected; an options screenshot initially exposed stale capture timing in the smoke harness, which was corrected, and an unrelated old match notice on the options screen was cleared. Music mute/unmute was checked through audio playback state, not subjective listening.

## Fair-play and presentation iteration - September 23, 2026

Confirmed findings and completed fixes, ordered by player impact:

| Priority | Confirmed problem and source | Implemented behavior |
| --- | --- | --- |
| P1 | `tryAiBallTouch` overwrote ball velocity toward a chosen destination and gave the bot another impulse; `driveCar` gave bots faster steering. | Removed that method and all bot-only physical advantages. `PhysicsWorld::contactImpact` records actual normals/closing speeds. `volleyVelocityChange` supplies bounded arcade lift and momentum carry equally to every car, after real contact and touch rearming. No destination or team enters the response. |
| P1 | Removing the old assist exposed repeated short serves. Initial tests also mislabeled same-side touches as multi-return rallies. | `resetRound` moves ball/server depth from 15.8/19.1 m to 11.0/14.3 m for humans and bots. Tests now count changes of touching team. `planTeam` uses descending interceptions and approach space; `aiControls` maintains commitment through contact instead of switching back to staging mid-approach. |
| P1 | A low-boost defender could select an enemy pad, then have its destination clamped to its own half forever; inverted cars could win an otherwise equal interception. | `nearestAvailableBoostPad` only selects the team's half and returns no target if all are unavailable. Team planning accounts for recovery delay. Existing support/third-car spacing and role ownership remain. |
| P1 | `thirdTouchBoostTimer` expired in render `update`, even though it changes Rookie speed limits; headless `fixedUpdate` never expired it. | Timer now advances in the fixed simulation and pauses with play. Cosmetics use a separate RNG; particle count and camera updates cannot alter subsequent serve/training randomness. |
| P2 | Ball cameras and split cameras could sit below the cage top outside the court; split FOV treated vertical and horizontal coverage alike. | Shared `arenaCameraPosition` lifts near-wall orbits above the cage without collapsing follow distance. `ballCameraFraming` uses the viewport aspect and actual smoothed camera position, with a safe direction for opposite rays. Extreme views still have an 82-degree cap and may need the existing off-screen locator. |
| P2 | Replays snapped between recorded frames and rendered car availability from current play; ghost normalized linear interpolation could collapse at equivalent opposite-sign quaternions. | `sampledReplayFrame` interpolates positions/shortest-path rotations, retains recorded visibility and cuts recovery teleports. Replay cameras sample the same trajectory; live particles are excluded. PB ghost rotation uses shortest-path interpolation. Its existing codec now rejects negative or beyond-finish timestamps. |
| P2 | Car impacts could start the hit sound twice; landing had no dedicated cue. | One contact cue, strength-scaled team particles/shake, plus a short synthesized landing sound and small dust burst once on a hard landing. Effects respect existing volume/shake preferences. |

The first pass without aimed returns failed Pro rally tests; physical approach, shared contact response and serve changes were developed against those failures. The final simulation scenarios recorded Rookie 2v2/3v3 with 6/7 exchanges and Pro 2v2/3v3 with 5/6 exchanges over each 60-second scenario. Best consecutive exchanges were 2 for Rookie and 1 for Pro. These are four reproducible diagnostics, **not** evidence of human difficulty balance or long Pro rallies.

Acceptance cases UC-15 through UC-19 are in [USE_CASES.md](USE_CASES.md). Production contact parity, bounded momentum carry, team recovery/boost selection, camera math, replay interpolation/visibility, real ghost codec/playback, fixed-step power duration, cosmetic RNG isolation and one-shot landing feedback are covered headlessly. The Release build and full suite passed **6/6 in 24.37 seconds**; the dependency-free build passed **3/3 in 2.71 seconds**. A replay capture initially showed the previous scoring screen; the local smoke harness now waits for rendered replay frames before capture.

Remaining priorities: manual human + AI 2v2 acceptance, more deliberate shot placement and longer Pro exchanges, controller handling across hardware, subjective sound/camera comfort, and clean-machine package launch. No major dependency, mode, persistence migration, online feature or publishing action was introduced.

Final stabilization found two harness issues: the closer serve overlapped the graphical aerial launch fixture, and that script scheduled controls on render time. The fixture now parks the held ball away from the jump lane; scripted controls and measurements run on physics ticks. The original height threshold remains, with two headless render-cadence cases added. The full suite then passed **6/6 in 23.99 seconds**, including a 9.60 m scripted aerial peak. Updated gameplay, split-screen and actual replay captures were inspected locally. This is still scripted rendering, not hands-on play.

Windows intermittently locked package fixture files during overwrite/cleanup. Fixtures now use separate archives, write each checksum once and retry only transient I/O cleanup failures within a bounded interval; verifier assertions are unchanged. The package-verifier test subsequently passed three consecutive runs and its test-only configuration rerun. Same-half contact parity was explicitly checked so the former bot-only assist would have activated in the comparison fixture.
