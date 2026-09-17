# RocketVolley gameplay audit

Audit baseline: repository version 0.12, 2026-09-17. Findings below come from source inspection, not visual play-testing. This machine has Python but no discoverable C++ compiler/CMake and no installed WSL distribution. No Git commands or local MSVC builds are used.

## Existing systems and scope

The C++20/raylib/Jolt architecture is retained. `Game.cpp` owns the 120 Hz loop, car handling, team AI, court rendering, procedural audio, menus, split-screen, replays, power-ups, Academy/ghosts, target challenge, Cup and career persistence. `PhysicsWorld.cpp` wraps Jolt; `GameplayLogic.cpp` supplies prediction/dodge/target helpers; `MultiplayerProtocol.cpp` validates v4 packets (no transport). Assets are procedural except the three README screenshots. Settings use the existing version-3 key/value file and ghosts use RVGHOST 1. Windows packaging fetches pinned dependency releases, uses VS 2022, CPack and SHA-256 checks.

## Prioritized confirmed findings

| Priority | Finding and player impact | Evidence at audit baseline | Resolution |
| --- | --- | --- | --- |
| P0 | Ball can escape the 6.2 m walls and competitive play never resolves the lost ball. | `createArena`, `fixedUpdate`: practice has bounds handling; competition only tests floor height. | In progress |
| P0 | Scoring uses a 12 cm early height threshold; fast floor bounces can also pass through that threshold between observations. Live rallies are cut off immediately at clock expiry. | `fixedUpdate`, `academyFixedUpdate` | In progress |
| P1 | Jump/dodge/power button edges disappear if a rendered frame produces no fixed step. Resetting the accumulator inside a step can leave it negative. | `update`, `launchServe`, `resetRound` | In progress |
| P1 | Nearby cars receive credit for wall/floor impacts; gentle true contacts can be missed. Continuous contact can repeatedly increment the four-touch fault counter. | `checkBallImpact`: velocity delta >4.1 and nearest car within 3.55 m | In progress |
| P1 | Bots can launch the ball at up to 3.05 m planar and 3.72 m vertical distance, regardless of physical contact. | `tryAiBallTouch` | In progress |
| P1 | Reverse AI flips steering, but ground handling does not reverse steering; logical airborne heading also diverges from body yaw and snaps on landing. Upside-down cars can remain stuck above the grounded height threshold. | `aiControls`, `driveCar` | In progress |
| P1 | Predictor bounces off infinite-height walls, uses incorrect wall inset, ignores wall energy loss/damping, only notices net center crossings, and predicts beyond first landing. Repeated independently per bot/role/indicator. | `predictBallMotion`, `ballTimeToHeight`, `aiControls`, `drawLandingIndicator` | In progress |
| P1 | Role selection runs independently, human slot 0 can retain priority while unavailable, support abandons defense for boost, and separation only protects P1. | `strikerForTeam`, `supportForTeam`, `aiControls` | In progress |
| P2 | Split-screen camera mode is shared; controller cannot pause or skip replays; free training repeats the full three-second countdown. | `handleGlobalInput`, `updateLocalCoopCameras`, `resetTrainingServe` | In progress |
| P2 | Academy aerial objective accepts any return, including ground touches. Target lesson relies on height threshold instead of contact. | `academyFixedUpdate` | In progress |
| P2 | Existing visual/audio feedback is extensive, but projected landing marker gives no explicit urgency, no off-screen ball pointer, and ball mesh does not use its physical rotation. | `drawLandingIndicator`, `drawBallTransform`, `drawHud` | In progress |
| P2 | CI only gates protocol/helper assertions. Most actual gameplay checks are embedded in a graphical smoke run, unavailable on hosted headless Windows. | `CMakeLists.txt`, `.github/workflows/release.yml`, `Game::Impl::run` | In progress |
| P3 | Settings overwrite directly; ghost replacement removes the previous file before rename; save failures after opening are not surfaced. Replay erases front of a vector; gameplay/UI/tests share a 7,000-line implementation. | `saveSettings`, `saveAcademyGhost`, `captureReplayFrame` | Follow-up |
| P3 | Packet validation does not bound match-state values or reject negative match time. No network transport exists. | `decodeWorldSnapshot`, `docs/RELEASE.md` | Follow-up |

## Implementation and verification log

Work is being completed in passes: rally integrity and input; handling and coordinated AI; readability and practice flow; regression/release gates. Final outcomes, executable verification limits and manual play-test instructions will be recorded here after implementation.
