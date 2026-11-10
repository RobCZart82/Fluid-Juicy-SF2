# SoundFont loader and engine ownership

This implementation follows the A09/F05 design gates from the audit of
`8f33ca5`. It replaces shared mutation of one synth with prepared-engine
handover. It does not implement 16-part routing or sample-accurate FluidSynth
event timing.

## User behavior

Browse and drag-and-drop enqueue background loading and leave the current bank
playing while the candidate is prepared. A failed load preserves the working
bank, selection and saved reference. Rapid requests are coalesced: only the
latest request can commit. Clearing the bank is also versioned, so an older
request cannot restore a bank after an unload.

A successful replacement fades the old engine out and the new one in over 512
samples. Existing voices belong to the old engine and stop after that fade;
they are not transferred to the new bank. Ordinary controller/pitch/generator
state is transferred. A bank/program absent from the new bank resolves to its
first preset in that bank, or the first playable preset overall; it must not
leave a successfully loaded instrument with a null preset. Complete RPN transactions, custom tuning and full GM2
state are still outside the current channel-state model.

Project-state restoration stays synchronous on the host's non-real-time control
thread: the bank is prepared before the call returns, so a subsequent offline
render need not race a disk loader. This operation and sample-rate preparation
can wait for bank I/O; they are not real-time APIs. A reentrant bank load from
our own render stack is rejected. Host lifecycle calls which replace or reset
the active engine must be serialized with rendering, as required by that
lifecycle contract. The GUI loading path does not wait for these operations.

## Ownership

- One serial loader per instance performs file I/O, decoding, bookmark resolution,
  candidate construction and stale-candidate destruction. Underlying FluidSynth
  decoding cannot be interrupted midway; cancellation discards its result.
- The audio callback owns the active engine and, briefly, one fading engine.
  A lock-free atomic pointer transfers a prepared pending engine at a block
  boundary. A single retired slot transfers cleanup back to the loader.
- If a fade or previous retirement is outstanding, the callback keeps rendering
  rather than waiting or freeing an engine. The loader polls retirement at
  20 ms intervals; this can defer adoption of another prepared bank. A stopped
  host may drain these slots directly outside rendering.
- The slot protocol bounds engine ownership to active, fading/retired, pending
  and one being prepared. It does not retain an unbounded history of engines or
  macOS bookmark scopes. A scope belongs to its engine and outlives its synth.
- Parameter callbacks publish atomic control intent. They do not call into the
  active synth. MIDI rendering and serialized lifecycle code are its only users.
  Public host program-name queries copy immutable metadata under a short state
  lock, never raw SoundFont pointers.

The plugin's processBlock does not acquire the loader/state mutexes, perform bank
I/O, construct engines or delete engines. It retains calls into JUCE and
FluidSynth; this is not a proof of worst-case real-time scheduling for all banks.
The control transfer itself uses fixed storage and supported 64-bit lock-free
atomics, with no host-block-sized allocation.

## State and UI delivery

The committed path/bookmark/error and immutable preset metadata are copied under
a control-side state mutex. Persistence reads this snapshot, including one
packed bank/program word. Editor dimensions use a separate packed atomic value.
No worker writes the GUI's ValueTree or resizes a component.

The editor's message-thread timer consumes a coalesced dirty flag, builds the GUI
bank model and publishes file status. An editor synchronizes this mirror on
opening. The model posts no UI callbacks which could outlive it. Parameter
widgets and restored editor dimensions also use message-thread timers. Normal host automation can still change independent
parameters during serialization; no whole-project sample-atomic snapshot is
claimed.

Request generations govern publication. An unload epoch also suppresses an
obsolete active/fading engine if an empty candidate is superseded before audio
can adopt it. This prevents old held notes from fading back on a later reload.
The host must destroy the editor before its processor, as required by JUCE; that
stops the UI timer. During processor shutdown, queued requests are cancelled,
the loader is joined, then engines are destroyed off the render stack. No model
UI callback remains queued. Shutdown can wait for an already-running decoder
call to finish.

## Automated acceptance

Processor regressions include a paused-loader gate rather than relying on disk
speed: 100 audio blocks keep the old voice while candidate preparation is
blocked. They also cover successful handover/fade, failed replacement, rapid
replacement/unload ordering, unload followed by reload before adoption, worker
state restore with an open editor, concurrent audio/program-name/persistence
queries, sample-rate change around loading, missing-file rate-reload recovery,
and destruction with an in-flight paused request.

Internal test counters reject engine destruction on the render stack and UI
publication off the message thread. JUICY_BUILD_TESTS enables these gate/counter
hooks; no host parameter or UI exposes them. The prior pitch, mono, tails,
state-format and exported-source tests remain enabled.

Native builds, sanitizer results and platform CI evidence must be recorded on
the implementing PR. Address/ThreadSanitizer builds instrument the project and
JUCE; reused FluidSynth/codec libraries are not instrumented. These tests do not
replace large real SF2/SF3 banks, sustained REAPER use, host-specific state/offline
render acceptance, clean-machine tests or a formal real-time budget measurement.
