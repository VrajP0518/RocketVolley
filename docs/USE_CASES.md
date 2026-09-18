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
