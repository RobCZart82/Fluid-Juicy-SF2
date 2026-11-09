# Development gates

1. **Desktop preview packages:** VST3/standalone and corresponding pinned source
   for macOS arm64, macOS Intel and Windows x64; architecture checks and checksums.
   Validate each platform's actual CI package before calling it available.
2. **REAPER acceptance:** scan/name/vendor, SF2/SF3 loading, missing/moved bank
   handling, saved-project recall, sample-rate changes and multiple instances.
   Record OS, host version, architecture and exact commit for each result.
3. **Audio reliability:** measure headroom at high polyphony, inspect UI locks
   and parameter notifications on audio paths, and stress bank changes/offline render.
4. **Distribution:** Windows 10 and minimum macOS clean-machine tests, runtime
   dependencies, signing/notarization and installation/upgrade behavior.
5. **Multitimbral design:** specify 16-part routing, per-part bank/program/state
   and GM2/GS behavior after the single-part acceptance gates pass.

CI success covers its automated checks, not every DAW or SoundFont. No stable
release is implied by this plan. Original plugin IDs/state remain preserved.
