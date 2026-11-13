# Review corrections, 2026-11-12

Baseline: de15be732ed8764debbaa1bcfb8b14033e2d97db.

## D01: preset preservation during reset

Reset previously applied the new selection to both the active engine and the
fading old engine. If the old bank lacked the new bank number, its fallback
overwrote the shared selection. The baseline probe changed bank 5/program 1
to 0/0 during reset and then to 5/0 at the next audio block.

Reset now retires the fading engine through the existing off-render reclamation
queue and clears the fade counter. Reset restores the active synth preset locally and leaves pending control
intent for the next audio block. The regression covers reset and releaseResources
before engine adoption and during a bank-layout crossfade,
checks the selection before and after the next block, and rejects render-stack
engine reclamation.

## D02: source aliases without Windows link privileges

Extraction retains native links when available. On Windows, a link-permission
error falls back to a regular copy of the validated file target. All link targets must be original regular
archive members inside the extraction root; links to directories, link chains,
duplicates and traversal remain rejected.

New source manifests record the target content SHA-256 alongside each symlink.
The exported-source verifier accepts a materialized alias only when that digest
matches. Older manifests continue to require native links because they lack the
additional digest. New Windows bundles therefore need to be exported by this
version for privilege-free alias verification.

Tests cover forced materialization with link APIs disabled, link-chain rejection,
copied-alias tampering and export/extract/manifest verification together.
All three Python suites passed locally (7, 9 and 4 tests).
The native Windows regression target compiled and all four CTest suites passed.
Platform build/package evidence is recorded on the implementing PR.

A full export of the actual pinned dependency sources also passed Windows
extraction and Git-free manifest verification locally.

The console host-notification harness now pumps JUCE timers until expected
delivery with a 1.5-second bound. Quiet checks pump for 100 ms. Exact event
counts, message-thread delivery and persisted paths remain asserted. This fixes
the fixed-60-ms timing failure observed on a busy Windows CI runner.


## E01: preserve channel pressure across lifecycle operations

Baseline: 89b3aed0d6042e82a6b2b1b279d65ddcfeeacad5. Channel pressure was
forwarded to FluidSynth but omitted from the channel snapshot. Engines now track
this MIDI input and restore it alongside pitch and CC state during Reset,
bank adoption and sample-rate rebuilds. CC121 and handled GM/GS reset messages
clear the tracked value so a later lifecycle operation cannot resurrect pressure.
This is live channel state, not a new project parameter or a poly-pressure model.

The regression uses an original synthetic SF2 and a diagnostic pressure-to-
attenuation modulator: after Reset, notes retain pressure attenuation without a
new MIDI event. Additional checks cover bank adoption, rate rebuild, CC121 and
GM reset. No user SoundFont is bundled or modified.

## R01: cancel editor scans without joining blocked filesystem work

The processor now owns one persistent library worker. Editor destruction and
Refresh invalidate the previous generation, discard its callback/result and
queue at most one newest request. Neither path joins the worker. Completed
results are delivered once, only for the current generation. File access and
folder edits remain on the worker; cancelling cannot undo an edit already saved.

A blocked OS filesystem call cannot be safely interrupted in-process. A new
request waits on the worker until that call returns, but the editor remains
responsive. Full processor destruction still joins the worker, preventing
execution of detached plugin code after module unload. This does not promise a
bounded host teardown when an OS/network operation never returns.

The regression blocks a worker independently of cancellation, closes the editor,
queues a new request before releasing the old worker, and verifies that only the
new callback is delivered. Native Windows regression compilation and all four
CTest suites passed with these changes. Platform CI and manual REAPER acceptance
are separate evidence recorded on the implementing PR.


## Audio regression follow-up, 2026-11-13

The dedicated original `test-pressure.sf2` fixture now carries its own
channel-pressure-to-attenuation modulator. Tests therefore exercise the bank
loader and newly constructed engines, without injecting a modulator through a
raw synth-pointer hook. That hook has been removed.

Reset, sample-rate rebuild and background bank adoption must all retain audible
attenuation without another pressure event. CC121, GM reset and GS reset must
restore an audible unattenuated note after lifecycle reset. The positive sound
checks prevent a silent/unloaded engine from satisfying the attenuation checks.
The fixture is synthetic test data and is not a bundled user SoundFont.

The development plan now matches processor-owned scanning: editor close does
not join, while full processor teardown still does. This follow-up changes test
coverage and documentation, not the product's MIDI behavior.
