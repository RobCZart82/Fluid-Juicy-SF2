# Reliability bug ledger

Audit baseline: `02a3bad63d70e32249a992cb29332c86c00366d3` (2026-11-10).
See [the complete audit](AUDIT-2026-11-10.md). Status records implementation and
validation separately; CI is not manual REAPER acceptance.

Current follow-up: [audit remediation from baseline 8f33ca5](AUDIT_REMEDIATION.md).

| ID | Priority | Issue | Implementation | Validation |
| --- | --- | --- | --- | --- |
| F01 | P1 | Empty-bank left/right navigation divides by zero | Implemented in this branch | Passed at `4da9ca7` on macOS arm64/Intel and Windows x64; see evidence below |
| F02 | P1 | Parameter callbacks directly mutate GUI; host program change takes message lock | Implemented for parameter-driven updates; loading callbacks remain F05 | Passed at `4da9ca7` on all three CI targets; manual REAPER acceptance open |
| F03 | P2 | MIDI Program Change can undo preceding Bank Select | Implemented in bank/program recovery branch | Two-bank 440/880 Hz, same-PC and recall regressions passed at `3741a6a` |
| F04 | P2 | Failed replacement discards working bank | Implemented in bank/program recovery branch | Missing/corrupt files, failed recall and successful replacement regressions passed at `3741a6a` |
| F05 | P2 | Loading is synchronous and shared engine state is not handed over safely | Background GUI loader, prepared-engine handover and UI snapshots implemented; see [architecture](LOADER_ARCHITECTURE.md) | Concurrent regressions and sanitizer/platform evidence on implementing PR; manual REAPER acceptance open |
| F06 | P2 | Host is told the instrument has no release/effect tail | Unbounded tail reporting implemented in audit remediation | Long-release regression passed locally; manual REAPER render acceptance remains open |
| F07 | P2 | Packages do not bind plugin source revision to binary build | Implemented in package provenance PR | Local rejection checks and subsequent three-platform packaging CI passed; see audit remediation for the separate source-archive rebuild issue. |
| F08 | P2 | Current privacy notice describes obsolete JUCE 5/ROLI behavior | Privacy notice updated | Reviewed current CMake configuration, SoundFont loading and project-state serialization |
| F09 | P3 | Decay/sustain long display descriptions swapped | Corrected in automation-label PR | Parameter IDs and short labels checked locally. Three-platform CI passed at `359ab1e`. |
| F10 | P2 | Editor focusInitialized flag read before initialization | Initialized to false | Editor construction exercised in successful three-platform regressions at `4da9ca7` |

## Sequence

1. F01/F02: protect the first-use GUI path and isolate parameter-driven GUI updates.
2. F03/F04: correct bank/program state and preserve a working bank on failure.
3. F05/F06: loader/real-time design and render tails.
4. F07/F08/F09: exact-source package evidence, current privacy text and naming.

No SoundFont bank is bundled. Original synthetic test fixtures are test data,
not a user sound library. Preserve the familiar interface and plugin identity.

## First implementation batch

F01: no-op navigation for empty lists and deterministic fallback when the current
bank is absent. F02: bank/preset widgets poll atomic parameter values on a 30 Hz
message-thread timer; MIDI and host parameter callbacks no longer invoke those
widgets directly. Host program selection no longer takes MessageManagerLock.
JUCE's existing slider attachments remain responsible for their own UI delivery.
The synthetic fixture now has two presets to exercise a real selection change.
F10: initialize the editor focus flag, preserving the existing keyboard behavior.

This batch does not claim asynchronous loading or full thread safety for bank
replacement/state recall (F05). F03-F09 remain open. No manual REAPER acceptance
or plugin-folder installation has been performed.

### Initial validation attempt

At code revision `1c27e3a`, native ARM macOS compiled VST3, standalone and the
regression executable. Empty navigation, worker-side isolation, state recall,
rendering and rate-change assertions passed. The timer-delivery assertion failed
because the console harness used a single fixed delay without an event loop.
The revised harness repeatedly pumps JUCE timer callbacks with a 1.5-second
bound and retains both the no-worker-mutation and eventual-GUI-update assertions.
CTest has a 30-second timeout so a deadlock cannot stall an entire CI job.
Validation of the revised code revision completed successfully; see below.

## Verified first-batch evidence

Implementation/test revision: `4da9ca7b34c404d4df6e41549f01c08eea31228d`.
Date: 2026-11-10.

- [Native Apple Silicon run 42763363885](https://github.com/RobCZart82/Fluid-Juicy-SF2/actions/runs/42763363885): build, regressions, architecture/package checks and artifact upload **passed**.
- [Desktop run 42763363890](https://github.com/RobCZart82/Fluid-Juicy-SF2/actions/runs/42763363890): macOS Intel and Windows x64 builds, regressions, package checks and uploads **passed**.
- New regressions cover empty-bank editor navigation, two-preset rendering,
  worker-thread MIDI isolation and eventual message-thread selection refresh.
  Existing state, timing and sample-rate assertions remain enabled.
- This documentation follow-up does not change code or tests. Its own exact-head
  CI status is available in [PR #6](https://github.com/RobCZart82/Fluid-Juicy-SF2/pull/6);
  the links above certify the specified implementation revision, not a future head.
- No manual REAPER acceptance, live performance stress test, notarization,
  merge or release is implied. F03-F09 remain open.

Next bounded implementation unit: F03/F04 bank/program state and transactional
replacement, followed by the F05 loader architecture. Keep these separate from
UI layout changes and preserve the no-bundled-bank distribution policy.

## Bank/program recovery batch

Base: merged PR #6, `48062aa1730eac9716db8019859df19641f1c8d5`.

- F03: publish the engine's selected bank and program together after MIDI Program
  Change, with same-thread callback feedback suppressed only for that model.
  UI/host parameter requests on other threads are not globally suppressed.
- F04: explicitly request a path/bookmark pair; try loading/selecting the candidate
  before removing the old font. Failed replacement preserves the old bank,
  program, saved path and bookmark, and exposes a transient user-readable error.
  A fresh missing-media project still retains its requested path for relocation.
- File-picker updates and error notices are delivered asynchronously on the UI
  thread. No browser/editor layout change or automatic directory scan is added.
- macOS bookmark scopes of replaced fonts are retained until engine rebuild or
  destruction, because FluidSynth can retain old voices. This needs subsequent
  F05 lifetime/stress coverage; it is not a claim of a finished real-time loader.
- The generated bank now has two presets in bank 0 at 440 Hz and two in bank 8
  at 880 Hz. Tests verify actual rendered pitch, same-PC bank switches, project
  recall, failed replacement recovery and commit of a successful replacement.

Loading remains synchronous. F05-F09 and manual REAPER acceptance remain open.
At `3741a6a9ff5bd184482bcc5a18f6fa0ede27eb07`, builds, regressions and
package verification passed on [Apple Silicon](https://github.com/RobCZart82/Fluid-Juicy-SF2/actions/runs/42770114929)
and [macOS Intel / Windows x64](https://github.com/RobCZart82/Fluid-Juicy-SF2/actions/runs/42770114899).

## Package provenance (F07)

A successful clean build records the plugin commit and SHA-256 manifests for the
VST3 and standalone products. Starting a new build invalidates the prior
record. Packaging checks the current source revision and both products, checks
the staged copies before signing, and archives the recorded Git revisions.
Older build records need a rebuild before packaging. Local builds with tracked
source edits can still run the tests.

## Envelope automation labels (F09)

Decay now reports decay time and Sustain reports sustain attenuation in host
automation. Parameter IDs, short labels, ranges, defaults and MIDI mappings
retain their existing definitions.

PR #9 merged at `7052564`. At `359ab1e`, builds, regressions and package checks passed on [Apple Silicon](https://github.com/RobCZart82/Fluid-Juicy-SF2/actions/runs/42778332810) and [macOS Intel / Windows x64](https://github.com/RobCZart82/Fluid-Juicy-SF2/actions/runs/42778332804).

## Privacy notice (F08)

The current notice documents local SoundFont loading and the path, settings,
editor size and optional macOS file-access bookmark stored in plugin state.
The source review covered the file picker, loader, state serialization and
build configuration. This was a source review without a network traffic capture.
