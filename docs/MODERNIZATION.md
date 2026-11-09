# Modernization status

Date: 2026-11-09. Upstream baseline: `c95eb5755aab634a22dfa778912facd136590d5c`.

## Intent

Fluid Juicy SF2 updates the existing Juicy SF Plugin for native Apple Silicon. Preserve the existing layout, keyboard, browser, controls, parameter names and XML state shape. This change does not turn the instrument into a 16-channel GM2 module.

## Evidence and outstanding acceptance

- Source pins: see `scripts/build.py` (full SHA for each dependency).
- Local Windows dependency compilation was attempted. A long build path prevented completion; this is not a successful plugin build.
- Native macOS compilation and regression tests: see the exact GitHub Actions run for the development branch. Until a run passes, treat the source as an unverified development preview.
- SF2 rendering and MIDI-offset assertions use an original generated sine SoundFont, not private or third-party soundbanks.
- Manual REAPER scan, editor display, SF2/SF3 loading, saved-project recall, moved/missing files, sample-rate changes, multiple instances, offline rendering and sustained playback remain required.
- Developer ID signing/notarization and clean-machine install acceptance remain open. Do not call an ad-hoc signed CI artifact a notarized release.

## Compatibility

The original CMake manufacturer code `Blbs`, plugin code `Jsfp`, bundle ID, binary product name and parameter/state identifiers are retained. The historical Projucer project used a differing plugin code (`Jspf`); old release project recall is therefore not proven by retaining the CMake identifiers. Test old sessions in an isolated environment before replacing any installed version. No automatic plugin-folder installation is performed.

## Licensing and provenance

Retain all original author notices and GPLv3 source license. JUCE 9's open-source option is AGPLv3, which adds requirements for combined distribution (GPLv3 section 13 covers combination with AGPLv3). The original files are not silently relicensed. FluidSynth and libsndfile use LGPL terms; Xiph and Signalsmith components carry their own notices. Any binary distribution needs corresponding source, build instructions and all applicable notices. No proprietary SoundFont, Ketron sample or firmware is included.
