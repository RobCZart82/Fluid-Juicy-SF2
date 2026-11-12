# Modernization status

Current audit and implementation status: [bug ledger](BUG_LEDGER.md) · [2026-11-10 audit](AUDIT-2026-11-10.md).

Date: 2026-11-10. Upstream baseline: `c95eb5755aab634a22dfa778912facd136590d5c`.

## Intent

Fluid Juicy SF2 updates the existing Juicy SF Plugin for native Apple Silicon. Preserve the existing layout, keyboard, browser, controls, parameter names and XML state shape. This change does not turn the instrument into a 16-channel GM2 module.

## Evidence and outstanding acceptance

- Source pins: see `scripts/build.py` (full SHA for each dependency).
- Native arm64, Intel macOS and Windows x64 source builds and processor regressions have passed in previous PR CI. Packaging on all three targets is a separate development gate; see the current commit checks.
- Native macOS compilation and regression tests: see the exact GitHub Actions run for the development branch. Until a run passes, treat the source as an unverified development preview.
- Processor regressions use an original generated two-preset SF2 and check MIDI offsets, finite output, state round-trip, pitch at 44.1/48/96 kHz, empty-bank editor navigation and worker-MIDI/message-thread GUI delivery. First-batch evidence is pinned to code revision `4da9ca7` in the [bug ledger](BUG_LEDGER.md). SF3 was also exercised locally with an original Vorbis fixture; full real-bank acceptance remains open.
- Manual REAPER scan, editor display, SF2/SF3 loading, saved-project recall, moved/missing files, sample-rate changes, multiple instances, offline rendering and sustained playback remain required.
- Developer ID signing/notarization and clean-machine install acceptance remain open. Do not call an ad-hoc signed CI artifact a notarized release.

## Compatibility

The original CMake manufacturer code `Blbs`, plugin code `Jsfp`, bundle ID and parameter/state identifiers are retained. The displayed product/vendor names are Fluid Juicy SF2 / RobCZart82, with Fluid_Juicy_SF2.vst3 and Fluid_Juicy_SF2.app bundles. REAPER may need a rescan to replace cached names; existing track names and manually renamed FX instances are project data and are not automatically renamed. The historical Projucer project used a differing plugin code (`Jspf`); old release project recall is therefore not proven by retaining the CMake identifiers. Test old sessions in an isolated environment before replacing any installed version. No automatic plugin-folder installation is performed.

## Licensing and provenance

Retain all original author notices and GPLv3 source license. JUCE 9's open-source option is AGPLv3, which adds requirements for combined distribution (GPLv3 section 13 covers combination with AGPLv3). The original files are not silently relicensed. FluidSynth and libsndfile use LGPL terms; Xiph and Signalsmith components carry their own notices. Any binary distribution needs corresponding source, build instructions and all applicable notices. No proprietary SoundFont, Ketron sample or firmware is included.

## Development packages

`package_desktop.py` supports macOS arm64/x86_64 and Windows x86_64 with
`--arch`, `--build-dir` and optional `--source-dir`. `package_macos.py` remains
the existing macOS entry point. Packages include matching source archives and
SHA-256 checksums. Packaging refuses tracked source edits or mismatched pins.
macOS checks architecture and system-only dynamic dependencies, then applies an
ad-hoc signature. Windows checks AMD64 PE32+ binaries; this does not verify
all runtime dependencies or replace clean-machine testing.

See [next development gates](DEVELOPMENT_PLAN.md).

## Bank replacement behavior (historical PR #7 batch)

A rejected replacement leaves an already loaded bank and its saved reference
intact. Errors are transient UI messages and are not serialized as project data.
For a fresh instance whose project bank is missing, the requested reference is
retained for later recovery. A playable candidate is committed only after load
and preset selection succeed. Bank and program selected by MIDI are mirrored to
project parameters without selecting an older UI bank again. See F03/F04 in the
[ledger](BUG_LEDGER.md) for exact validation status; asynchronous loading remains
F05 and is not implemented by this batch.

## Runtime reliability follow-up

See [audit remediation](AUDIT_REMEDIATION.md) for implementation and validation
status. Mono output is mixed from a stereo scratch buffer; reverb/chorus returns
are mixed into the stereo pair. Wet output can be louder than the old dry-only
path, so compare levels before replacing an installed preview. No limiter or
normalization has been silently added.

FluidSynth 2.6.2 applies events at internal 64-sample render boundaries. Host-block
splitting retains offsets but does not remove this quantization: additional
Note On timing error is 0..63 samples (up to 1.43 ms at 44.1 kHz). Regressions use
unaligned offsets and host blocks of 32, 127 and 512 samples. Full sample-accurate
Note On/Off, CC and program dispatch remains an open engine-level requirement.

Tail reporting is unbounded, rather than a false zero or a guessed short finite
time. The JUCE VST3 wrapper reports kInfiniteTail. Choose an explicit end/tail
length for offline renders in hosts which cannot auto-stop an unbounded tail.
Reset stops active voices and releases hold pedals; FluidSynth may emit a brief
mixer residual before settling. Ordinary controller values, generator offsets
and pitch-bend sensitivity/value survive engine sample-rate reconstruction.
In-progress RPN/NRPN selection and custom tuning/SysEx state are not fully
preserved; they need the future channel-state model. This is not full GM2 recall.

## Rebuild an exported source bundle

Extract the complete corresponding-source.tar.gz, keeping fluid-juicy-sf2/,
dependencies/ and source-manifest.json together. From fluid-juicy-sf2/ run:

```sh
python3 scripts/build.py --verify-sources-only
python3 scripts/build.py --arch arm64
```

Use --arch x86_64 for Intel macOS/Windows and a short --build-dir on Windows.
The exporter records file hashes and revisions; the builder verifies those
without requiring .git metadata or downloading the included dependencies.
Compiler, CMake, Python and platform SDKs remain external prerequisites.
Packaging a new binary distribution still requires actual clean Git checkouts.
CI verifies the exported entry point outside the checkout; full archive rebuild
acceptance is recorded separately from manifest verification.

## Background loader follow-up

GUI browse/drop now prepares an independent synth on a serial background worker.
GUI-ready engines are adopted at block boundaries with a 512-sample crossfade; old
engines and file-access scopes are reclaimed outside rendering. Host state
restoration remains a synchronous non-real-time operation for offline readiness.
UI models, program-name queries and persistence use separate snapshots rather
than shared raw synth/font pointers. See [ownership, ordering and test limits](LOADER_ARCHITECTURE.md).
The 64-sample MIDI timing limitation and full GM2/GS acceptance remain open.

## Output level and recall compatibility

Output is an appended, automatable parameter (-60 to +6 dB), with a 20 ms linear
gain ramp. New instances start at -12 dB. State files from earlier previews which
contain parameters but no outputGain field restore at 0 dB to retain their mix
balance. Existing parameter IDs and indices remain unchanged.

FluidSynth engine gain remains 2.0. The output control adds headroom after the
stereo render; it is not a limiter or a guarantee against every bank/polyphony
combination exceeding 0 dBFS. Monitor the host meter and lower Output as needed.
The original audit's DSoundFontV4 eight-note piano chord exceeded unity by
0.245 dB; the new default attenuates the same render by 12 dB.

## Source archive extraction

The verifier supports Python 3.9+ without relying on newer tar extraction
filters. It rejects absolute/parent paths, duplicate files and special devices,
writes regular files before links, and accepts only in-root links to regular
files. Directory links and link chains are rejected. Legitimate FluidSynth
Unicode filename aliases and executable mode bits are retained.

Source bundles exported after the 2026-11-12 corrections include content hashes
for file aliases. The Windows verification/extraction tool copies these aliases
without requiring symlink privileges and validates their bytes. Older exported
manifests still require their original native links. See [D02](REVIEW_FIXES_2026-11-12.md).
