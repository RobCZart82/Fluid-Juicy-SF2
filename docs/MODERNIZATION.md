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

## Bank replacement behavior (development branch)

A rejected replacement leaves an already loaded bank and its saved reference
intact. Errors are transient UI messages and are not serialized as project data.
For a fresh instance whose project bank is missing, the requested reference is
retained for later recovery. A playable candidate is committed only after load
and preset selection succeed. Bank and program selected by MIDI are mirrored to
project parameters without selecting an older UI bank again. See F03/F04 in the
[ledger](BUG_LEDGER.md) for exact validation status; asynchronous loading remains
F05 and is not implemented by this batch.
