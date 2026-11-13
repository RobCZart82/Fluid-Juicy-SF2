# SoundFont loader and engine ownership

This implementation follows the A09/F05 design gates from the audit of
`8f33ca5`. It replaces shared mutation of one synth with prepared-engine
handover. The 16-part extension retains one shared bank/engine and sixteen
independent channel states; see [multitimbral behavior](MULTITIMBRAL.md).
Sample-accurate FluidSynth event timing remains outside this handover protocol.

## User behavior

Browse and drag-and-drop enqueue background loading and leave the current bank
playing while the candidate is prepared. A failed load preserves the working
bank, selection and saved reference. Rapid requests are coalesced: only the
latest request can commit. Clearing the bank is also versioned, so an older
request cannot restore a bank after an unload.

A successful GUI replacement fades the old engine out and the new one in over 512
samples. A newer replacement can supersede that fade. Existing voices belong to the old engine and stop after that fade;
they are not transferred to the new bank. Ordinary controller/pitch/generator
state is transferred for all sixteen channels on GUI bank handover. Synchronous
project recall starts from a fresh engine and applies the saved intents, preventing
unrelated live controllers from leaking into a restored project. A bank/program absent from the new bank resolves to its
first preset in that bank, or the first playable preset overall; it must not
leave a successfully loaded instrument with a null preset. Complete RPN transactions, custom tuning and full GM2
state are still outside the current channel-state model.

Project-state restoration stays synchronous on the host's non-real-time control
thread: the bank is prepared before the call returns, so a subsequent offline
render need not race a disk loader. Its engine is adopted on the next audio block,
even during an earlier GUI fade; synchronous restoration does not crossfade the
old bank into the restored project. This operation and sample-rate preparation
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
  boundary. An intrusive atomic retirement list transfers cleanup back to non-RT
  producers/reclaimers. Adoption never waits for a prior fade or retirement.
- A superseded fading engine is retired along with any previous active engine
  which will not fade. The loader polls retirement at 20 ms intervals; producers
  also drain the list before preparing another engine. A stopped host may drain
  ownership directly outside rendering.
- A four-engine allocation budget includes active, fading, pending, preparing
  and retired engines, including resources whose destruction is still running.
  Only non-RT preparation waits for budget credit. The render callback neither
  waits nor frees engines. A macOS access scope outlives its synth.
- Parameter callbacks publish atomic control intent. They do not call into the
  active synth. MIDI rendering and serialized lifecycle code are its only users.
  Public host program-name queries copy immutable metadata under a short state
  lock, never raw SoundFont pointers.

The plugin's processBlock does not acquire the loader/state mutexes, perform bank
I/O, construct engines or delete engines. It retains calls into JUCE and
FluidSynth; this is not a proof of worst-case real-time scheduling for all banks.
The control transfer itself uses fixed storage and supported 64-bit lock-free
atomics, with no host-block-sized allocation.

Complete SysEx events are passed synchronously from the existing MidiBuffer
storage to FluidSynth, excluding F0/F7 framing bytes. No MidiMessage copy or
plugin-owned payload allocation is made on this path, and the borrowed pointer
is never retained. An unterminated SysEx event is ignored; fragmented SysEx is
not assembled across events. Short MIDI events retain the existing dispatch.
This removes one known JUCE payload copy; it does not establish that FluidSynth,
parameter notifications or the entire render path are allocation-free.


## State and UI delivery

The committed path/bookmark/error and immutable preset metadata are copied under
a control-side state mutex. Persistence reads this snapshot, including sixteen independent
packed bank/program words. Editor dimensions use a separate packed atomic value.
No worker writes the GUI's ValueTree or resizes a component.

The editor's message-thread timer consumes a coalesced dirty flag, builds the GUI
bank model and publishes file status. An editor synchronizes this mirror on
opening. The model posts no UI callbacks which could outlive it. Parameter
widgets and restored editor dimensions also use message-thread timers. Normal host automation can still change independent
parameters during serialization; no whole-project sample-atomic snapshot is
claimed.

Successful GUI publication compares the committed path/bookmark and raises a
coalesced atomic host-state flag when the reference changes. A processor-owned
30 Hz message-thread timer consumes it and sends JUCE's non-parameter-state
notification, independently of editor lifetime. Neither the loader nor render
callback calls the host for this event, and no model lock is held during host
notification. Successful synchronous recall clears pending GUI notification
intent; rate preparation and failed working-bank replacements do not mark edits.
The notification asks a compatible host to offer saving, rather than saving a
project automatically. Host-specific dirty-project behavior remains a manual
acceptance check.

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
destruction with an in-flight paused request, and repeated synchronous restores
during GUI crossfades without render-stack reclamation.

Internal test counters reject engine destruction on the render stack and UI
publication off the message thread. JUICY_BUILD_TESTS enables these gate/counter
hooks; no host parameter or UI exposes them. The prior pitch, mono, tails,
state-format and exported-source tests remain enabled.

Native builds, sanitizer results and platform CI evidence must be recorded on
the implementing PR. Address/ThreadSanitizer builds instrument the project and
JUCE; reused FluidSynth/codec libraries are not instrumented. These tests do not
replace large real SF2/SF3 banks, sustained REAPER use, host-specific state/offline
render acceptance, clean-machine tests or a formal real-time budget measurement.
