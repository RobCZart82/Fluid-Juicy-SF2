# Audit remediation

Baseline: master `8f33ca5c878be9244388d50a94e57c5825454a74`, 2026-11-10.
This follows the newer ten-item audit; A identifiers are separate from the
older F identifiers in [the ledger](BUG_LEDGER.md).

## First implementation batch (merged PR #15)

| ID | Behavior | Status |
| --- | --- | --- |
| A01 | Mono host output | Implemented with fixed stereo scratch storage and 0.5(L+R) downmix; oversized odd-block regression |
| A02 | Host reset and release/prepare | Stops active voices, clears effects through FluidSynth reset, releases hold pedals and drains the bounded mixer pipeline; regression covers held note, restart and new notes |
| A03 | Empty project state | Unloads the font, clears saved path/bookmark, browser model and displayed picker; rejects the old sound |
| A04 | Sample-rate state | Ordinary CC, pitch/value/sensitivity and generator offsets restored; CC7 audio regression; incomplete RPN/tuning state explicitly excluded |
| A05 | MIDI timing | Open: 64-sample FluidSynth quantization documented and bounded by unaligned-offset/multiple-host-block tests; sample accuracy is not claimed |
| A06 | Effect output | Wet and dry returns mixed through FluidSynth's supported buffer aliasing; CC91/93 zero/nonzero tail regression |
| A07 / F06 | Tail reporting | Unbounded tail instead of zero; long-release audio regression, host render acceptance still required |
| A08 | Exported source rebuilding | Export manifest and Git-free verification/build entry point implemented; mutation/missing-file/path rejection tests and exported-bundle CI smoke check |
| A09 / F05 | Loader and engine ownership | Prepared-engine/background loader implemented in the follow-up; see [ownership and acceptance](LOADER_ARCHITECTURE.md) |
| A10 | Keyboard dirty flag | Atomic exchange on the message-thread timer; audio listeners only set the flag |

Validation before review: local macOS arm64 VST3, standalone and both CTest
suites pass. Platform CI and actual exported-source rebuild evidence must be
attached to the PR; do not infer those from this local result. No manual REAPER
acceptance or stable release is implied. Original plugin IDs and XML parameter
identifiers are preserved.

The reset policy does not reset the user's volume/pan/mixer controls. Hold
pedals are released and active notes are stopped. A short residual from
FluidSynth's mixer reset may settle over the next block; it must not become a
sustained voice. Blank bank state is always silent. Sample-rate changes restore
ordinary controls and resolved generator values without replaying RPN/NRPN data
entry in arbitrary controller-number order; in-progress selection is deselected.

Wet output is now audible, so historical dry-only sessions can sound different.
Tail length is unbounded and can require an explicit offline render end in the
host. See [compatibility notes](MODERNIZATION.md) for these intentional changes.

## A09 design gates (implemented by the follow-up)

Do not put a file loader or a long blocking mutex around the existing render
function and call it real-time safe. The replacement design must meet all of
these gates before A09 can be closed:

1. A serial background loader owns bank I/O, bookmark resolution, candidate
   engine creation and immutable preset metadata. Failed or superseded requests
   leave the current committed engine and project reference intact.
2. Publish a complete prepared engine with its sample rate, font, bookmark scope
   and selection through a bounded handover. The audio callback owns the active
   engine; it must neither load files nor delete the retired engine. Reclamation
   happens outside the audio callback after an explicit acknowledgement.
3. GUI and host parameter changes enqueue or atomically publish control intent;
   they must not mutate the active synth concurrently. Host program-name queries
   read an immutable metadata snapshot, never a raw SoundFont pointer that the
   loader can invalidate.
4. Deliver committed bank lists, paths and errors on the message thread. State
   serialization must read a coherent committed snapshot. A host restoring state
   on a worker with the editor open must not call GUI components directly.
5. Specify offline-render readiness, sample-rate changes during loading,
   rapid replacement/cancellation, empty-state unload and destruction with a
   pending load. A late result must not resurrect an unloaded bank or a destroyed
   instance. Keep security scopes alive until the owning engine is reclaimed.
6. Tests must cover slow/injected failed loads during rendering, worker-side
   restore with an open editor, repeated program-name queries, rapid replacement,
   latest-request ordering, destruction and multiple instances. Run thread and
   address sanitizers, then sustained and offline REAPER acceptance.

PR #15 intentionally left this architectural batch open. Its follow-up now
implements prepared-engine handover, background GUI loading, off-render
reclamation and message-thread UI snapshots; see [the implementation contract](LOADER_ARCHITECTURE.md).
Non-real-time host state restoration remains synchronous for offline readiness.
Manual REAPER acceptance remains open. The future 16-part model must build on
this ownership contract rather than multiply the former single-part state.
