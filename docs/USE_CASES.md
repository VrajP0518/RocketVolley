# Player use cases and regression coverage

Follow-up engineering pass, September 18, 2026. Scope deliberately limited to practice reliability and preserving progress. Existing game modes, file formats and controls are retained.

## Acceptance criteria

| ID | Player journey | Given / when / expected result | Automated coverage |
| --- | --- | --- | --- |
| UC-01 | Practice a particular return | Given either difficulty and Lob, Fast or CrossCourt, when a feed launches without interference, it clears the net and lands beyond z=4 on the learner's half within eight seconds. Rookie speed limiting must remain active. | `GameHeadlessTests.inl`: six cases using production `fixedUpdate` and Jolt, fixed random seed. All passed before changes; no feed tuning needed. |
| UC-02 | Finish a target run | Given ten legal bullseye returns, each landing awards once, shots 1-9 advance to countdown, and shot 10 ends the run with score/combo records updated. A subsequent miss breaks the current combo without erasing earned points or best combo. | `GameHeadlessTests.inl`: complete run, duplicate landing callbacks and mixed success/miss scenario. |
| UC-03 | Keep progress across saves | Given a new or existing profile destination, writing a save creates missing directories and replaces the entire file with exact bytes. Ghost payload bytes remain unchanged. | `SaveFileTests.cpp`: creation, replacement and binary preservation. |
| UC-04 | Recover from a save failure | Given a blocked destination, invalid parent or Windows file lock, a save reports failure, preserves the previous destination and cleans up its staging file. Saving succeeds when the temporary lock is removed. | `SaveFileTests.cpp`: real filesystem failures, including a Windows handle denying replacement. No player profile is accessed. |
| UC-05 | Pause and reconnect during co-op | During a live rally, disconnect P2, reconnect and resume; both players retain their camera choices and can continue without lost/unwanted controls. | Manual hardware acceptance remains required. State-transition checks are now automated under UC-06; physical controller reconnection remains manual. |

## Release-readiness cases

| ID | Player/release journey | Acceptance criteria and evidence |
| --- | --- | --- |
| UC-06 | Leave and return safely | Focus loss pauses the rally; a disconnect beats a simultaneous pause press; P2 must reconnect before co-op resumes; reconnection alone never resumes. Help freezes gameplay and only resumes a pause it created. Help carried over from loading stops kickoff. Solo players can resume with keyboard after controller loss. Production transition tests first failed, then passed after the fixes. Hardware event delivery remains manual. |
| UC-07 | Watch the end of a long rally | Capture 650 frames, retain exactly the latest 300 in chronological order, clamp replay access and clear at kickoff. Production replay tests pass; a deque replaces the vector's repeated front erasure. This removes full-buffer shifting without claiming a measured FPS gain. |
| UC-08 | Publish the tested portable game | Verify SHA-256, required documentation/images, runtime DLLs beside the EXE and packaged EXE identity against the tested build. Reject missing DLLs, stale EXEs, path traversal, test executables and checksum mismatches. PowerShell fixture tests exercise valid and invalid ZIPs; GitHub Actions calls the same verifier before upload/publication. |

## Implementation decision

The gameplay cases protect behavior that already worked. The confirmed defect was persistence: settings truncated the live file before completing the write, and ghost saving deleted the old file before replacement. Both now use [SaveFile.cpp](../src/SaveFile.cpp): exclusive staging beside the destination, complete write, flush, close, then replacement. Windows uses `MoveFileExW` with replacement and write-through; POSIX uses same-directory rename after `fsync`. Failed writes do not deliberately remove the committed destination. Existing settings v3 and RVGHOST 1 readers/writers retain their formats.

This protects individual files against ordinary write/replacement failures. It does not provide a transaction across settings and ghost metadata, a historical backup, or a guarantee against storage failure/power loss. Concurrent game instances use separate staging files; the last successful replacement wins. POSIX code is included but has not been executed in this Windows environment.

## Running the checks

```sh
cmake --build build-release --config Release --parallel 4
ctest --test-dir build-release -C Release -L headless --output-on-failure
```

The `headless` label includes gameplay logic, save failures, protocol, Jolt simulation and the Windows package-verifier fixtures; none needs graphics or audio. `ROCKET_VOLLEY_LOGIC_ONLY=ON` builds the two dependency-free logic/save test executables, plus the package-verifier test on Windows when PowerShell is available. The graphical smoke test remains an optional local check. Test scratch files are created in a fresh directory under the build's working directory and removed after the save test.

Manual follow-up: complete an Academy PB, change controls, earn a Target Challenge record, exit and restart to confirm all three persist. On co-op hardware, perform UC-05. Do not mistake synthetic bullseye landings for hands-on validation of target difficulty or controller feel.

Verification result: Windows Release build succeeded; full CTest suite **5/5 passed** (23.44 seconds), including all automated cases above and the existing graphical smoke test. Dependency-free configuration **2/2 passed**. UC-05 remains manual.

Release-readiness result: all six local targets passed, with the package-verifier target rerun after fixing Windows PowerShell assembly loading. The rebuilt ZIP passed the CI package verifier. Hardware, clean-machine launch and subjective play acceptance remain manual.

## Push-readiness cases - September 22, 2026

- **UC-09 - Custom controls:** bind Forward to each of `1`, `2`, `Tab`, `G`, `[` and `]`. The driving binding must suppress the optional difficulty/feed/ghost/bounce action, while unbound shortcuts still respond only to a press. Swapping occupied keys must preserve unique bindings. F1 and Enter must be rejected by both assignment and profile validation. The initial regression run reproduced six shortcut conflicts and two profile-validation failures. The binding capture dialog now receives F1 for validation instead of opening help over the prompt, and displays rejection feedback.
- **UC-10 - Explicit test-only build:** configure a fresh directory with `ROCKET_VOLLEY_LOGIC_ONLY=ON` and `BUILD_TESTING=OFF`. The requested logic/save tests and Windows package-verifier test must still be discovered and executed. No raylib/Jolt download is needed.
- **UC-11 - Release artifact selection:** the workflow uploads only the ZIP/checksum for the CMake project version, excluding old checked-in versions. The `package_windows` target passes its selected build configuration explicitly to CPack. The rebuilt ZIP must pass `VerifyPackage.ps1`, and its extracted executable must pass protocol and headless simulation commands locally.

Manual control check: assign one of these optional shortcut keys to driving, use it in training/Academy and verify only the assigned action occurs; restore defaults to re-enable the shortcut. Input policy has automated coverage, but real keyboard event delivery and subjective control feel still need human acceptance.

September 22 result: all **6/6** full-suite tests and **3/3** fresh test-only tests passed after fixing the reproduced control conflicts and test discovery issue.

## Practice and comfort acceptance cases

| ID | Journey | Automated acceptance |
| --- | --- | --- |
| UC-12 | Practice one repeatable shot | Lock a feed, finish two legal returns and one miss, and receive the same position/velocity/feed each time. Duplicate landing callbacks count once; the final session is 2/3 returns, streak 0, best 2. Manual retries do not inflate completed attempts. Feed changes preserve the lock/statistics; a new session resets them. Commands cannot change scored Challenge feeds. |
| UC-13 | Personalize audio/comfort and keep progress | Music/effects adjust independently and clamp; options never advance the paused match; zero impact shake produces the same camera position as an unshaken frame. The actual profile reader/writer round-trips preferences, custom bindings, Academy PB, Challenge record and career data. Legacy profiles retain original defaults; malformed values are ignored. Graphical smoke checks mute/unmute with an audio device when available. |
| UC-14 | Play faster matches without replays | Off retains the point celebration but skips directly to the winner's kickoff or final results. On retains the original replay path. Score and serving ownership remain correct. The setting survives profile round-trip. |

Manual checks: use a gamepad to lock/change/retry a training shot; mute only music and listen for gameplay cues; mute effects and confirm impacts remain silent; disable shake during hard hits; toggle replays and finish a match; restart the game and verify these preferences and existing records persist. Automated audio state checks do not certify subjective sound quality.

September 23 verification: **6/6 full-suite tests passed** after final polish; **3/3 dependency-free tests passed**. Main menu, training HUD and audio/comfort layout were visually inspected from local smoke captures. Physical controller delivery and subjective audio balance remain manual acceptance items.

## Fair play, camera and replay acceptance cases

| ID | Journey | Automated acceptance |
| --- | --- | --- |
| UC-15 | Hit the same ball as a human or bot | Real Jolt contacts from identical setups produce the same outgoing ball velocity; identical steering inputs produce identical yaw. Nearby non-contact balls are unaffected. Shared lift/carry follows momentum, preserves stationary roof catches and bounds added velocity. |
| UC-16 | Rally with coordinated teams | Four seeded 60-second scenarios cover Rookie/Pro 2v2/3v3, finite physics, bounded boost, match progress, and at least two actual changes of touching team. Same-side touches alone cannot satisfy the exchange assertion. An equally placed upright teammate receives an interception over an overturned car; depleted defenders cannot select enemy-half pads. |
| UC-17 | Track the ball near walls and in split-screen | Both camera modes retain follow distance while clearing the cage. Portrait framing widens for the narrower horizontal view; opposite ball/car rays retain a finite, nonzero direction. Local graphical smoke covers rendering; motion comfort remains manual. |
| UC-18 | Watch replays and PB ghosts | Fractional replay positions interpolate motion; equivalent quaternion signs cannot collapse or spin the rotation. Demolition/respawn teleports are not interpolated, and historical car visibility is retained. Extreme replay indices clamp safely. Production ghost serialization round-trips; negative, reversed or beyond-finish timelines reject without replacing valid decoded data. |
| UC-19 | Pause or change rendering without altering gameplay | Third-touch power expires on physics ticks, including headless matches, and remains frozen while paused. Consuming particles and 60 camera updates does not change a seeded serve or receiving target. Hard-landing feedback occurs once per landing. |

Manual acceptance: compare controlled roof/nose hits and serves on both difficulties; play human + AI against two bots, including retreating, deliberately missing and recovering upside down; follow high balls near all cage edges in each split viewport; listen to landing/hit volume; watch a replay containing a demolition and an older saved Academy ghost.

Final stabilization: full suite **6/6 passed in 23.99 seconds**. The scripted aerial is isolated from the new serve position and uses physics-timed inputs; a headless comparison verifies its peak at two frame cadences without lowering the height requirement. Package fixture cleanup tolerates brief I/O locks, with three consecutive verifier runs passing after the change. Actual replay capture timing was corrected and the resulting image inspected.
