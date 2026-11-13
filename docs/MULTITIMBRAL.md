# 16-part instrument

New instances route MIDI channel 1-16 to parts 1-16. One active FluidSynth engine
loads one shared SF2/SF3 bank; the bank is not loaded sixteen times. All parts and
the reverb/chorus returns mix into the existing stereo output (or its mono host
mixdown). Loading a different SoundFont replaces the shared bank for all parts.

## Playing and mixing

Select a Part above the browser to edit that part's bank, preset, envelope and
filter. The on-screen keyboard sends to the selected part. Changing parts releases
notes held by that keyboard on the previous part, without stopping host MIDI on
other parts. The current preset name appears alongside the selector.

Mixer switches the browser area to a scrollable 16-row mixer. Each row controls
Volume (CC7), Pan (CC10) and Expression (CC11). Clicking the part number selects
that part for editing. Volume/expression scale the part through MIDI controllers;
note velocities are not rewritten. Output remains a global, smoothed level.
Watch the host meter when layering many parts: the -12 dB default is not a limiter.

Modulation (CC1), sustain pedal (CC64), channel pressure, pitch bend and bend range
also have independent host parameters. Ordinary MIDI CCs are forwarded on their
original channel. Note-off, key pressure, all-notes-off and all-sound-off are
channel-local. Supported global SysEx resets affect all parts.

Part 10 starts at SF2 bank 128/program 0 with FluidSynth's drum-channel type. It
requires an actual percussion preset in the loaded SoundFont. A missing bank-128
kit is left unassigned rather than silently replaced by a melodic piano. Select a
suitable kit from the SoundFont; loading a bank does not provide absent samples.
GUI bank numbers are SF2 bank numbers 0-128 and preset numbers are 0-127.
Host program-list calls retain their historical part-1 meaning.

## REAPER setup

Use one instrument track containing Fluid Juicy SF2. Send MIDI from each source
track to that instrument, assigning a distinct destination channel 1-16 for each
part. A MIDI item can also carry multiple channels on one source track. Enable
monitoring as appropriate for live input. Choose the matching part's preset in
the plugin.

Use the plugin Mixer, MIDI CC7/CC10/CC11 events, or those plugin parameters in host
automation for per-part mixing. The plugin currently returns one shared stereo
mix; separate audio-output buses and per-part audio effects are future work.
Save the host project after changing the shared bank or part settings.

## State and migration

The first nine host parameter indices and historical IDs remain unchanged. New
part parameters are appended. Part 1 retains bank/preset/envelope/filter IDs;
other IDs use partN.name (for example part2.volume). New states carry partFormat=1,
a routing mode, selected UI part, the shared SoundFont reference and per-part
parameters/controller data. SoundFont samples and active voices are not embedded.

Saved settings include each part's bank/program, volume, pan, expression,
modulation, pedal, envelope/filter controls, pitch, pressure and bend sensitivity.
Ordinary extra CC values are retained. Channel-mode commands, incomplete RPN/NRPN
transactions and custom tuning are not serialized as controller replay. Pitch
bend sensitivity is stored as semitones. Numeric CC replay excludes data-entry
and RPN/NRPN selectors/increments. Polyphonic key pressure remains live MIDI,
not saved per-note state. A host reset releases hold pedals and active voices.

States without the new routing field load in Legacy omni mode: all incoming MIDI
channels address part 1, preserving the previous single-part behavior. New parts
receive defaults instead of inheriting unrelated live settings. Select Legacy
omni to enable 16-part MIDI explicitly; mode changes stop existing held voices.
The part selector/mixer are disabled while legacy routing is active. Saving the
converted project retains the new mode. Loading these new projects in an older
plugin version is not a supported reverse migration.

## Verification and limits

Synthetic-bank regressions check distinct simultaneous bank/program frequencies,
all sixteen channel numbers, note-off isolation, CC7/CC10/CC11, pressure, pitch,
sustain, percussion and channel-local all-sound-off. State tests cover all-part
mix/selection recall, effect-send CC, RPN bend range, rate rebuild, bank handover,
legacy import/conversion, stale-controller rejection and mixer parameter binding.
They also render a simultaneous 16-part ensemble and retain existing loader,
reset, timing, provenance and lifetime regressions.

This is 16-part operation, not a full GM2/GS conformance claim. SoundFont coverage,
unsupported tuning/NRPN transactions and the existing FluidSynth 64-sample event
quantization remain separate limits. Real GM/GS SF2/SF3 banks, long-session CPU/
memory/headroom measurements, manual REAPER 16-track routing, offline renders and
installer/clean-machine acceptance remain release gates.
