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
