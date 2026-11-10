# Development gates

Current audit and implementation status: [bug ledger](BUG_LEDGER.md) · [2026-11-10 audit](AUDIT-2026-11-10.md).

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

## Planned SoundFont library discovery

Status: future development, not an existing automatic scanning feature. Keep
manual file selection and drag-and-drop available from any accessible location.

### Default folders

On macOS, automatically check both standard bank folders for `.sf2` and `.sf3`
files (case-insensitive extensions):

- `~/Library/Audio/Sounds/Banks/` â€” current user's banks.
- `/Library/Audio/Sounds/Banks/` â€” banks shared by all users.

These are bank folders, separate from the VST3 plug-in installation folders.
Apple documents these locations for its DLSMusicDevice in the
[Core Audio system audio units reference](https://developer.apple.com/library/archive/documentation/MusicAudio/Conceptual/CoreAudioOverview/SystemAudioUnits/SystemAudioUnits.html).

On Windows, provide the same drop-folder discovery workflow. Verify the platform
conventions before choosing and documenting the exact default locations. Do not
claim a universal Windows SF2/SF3 folder without evidence. For example,
[FluidSynth's `synth.default-soundfont`](https://www.fluidsynth.org/api/settings_synth.html)
documents `C:\soundfonts\default.sf2` for its executable; this is a FluidSynth
default, not evidence of a Windows-wide SoundFont directory or this plug-in's
current behavior. If no suitable OS convention is established, define clearly
named application-specific user/shared folders and document them as such.

### Discovery behavior

- Let users add their own bank folders and persist these preferences.
- Scan on startup in the background and provide an explicit Refresh action, so
  banks copied into a folder can be discovered without restarting the host.
- Keep directory scanning and SoundFont loading off the real-time audio thread.
  Specify bounded traversal and symlink handling before implementation.
- List discovered banks with their paths, distinguish duplicate names, and let
  the user choose a bank. Discovery must not silently replace the project's bank.
- Treat absent or unreadable folders gracefully. Reading shared folders must not
  require administrator privileges; do not create or modify them automatically.
- Preserve project bank references and provide relocation/recovery when a saved
  bank has moved. Finding a file does not guarantee that it is a valid SoundFont;
  report load failures without discarding the current working bank.

### Acceptance and user manual

Validate both macOS locations and the chosen Windows locations, custom folders,
SF2/SF3 discovery, duplicate names, missing folders, permissions, Refresh behavior,
project recall and moved-bank recovery in REAPER. Confirm that scans do not
interrupt audio playback.

The feature is complete only when the user manual documents the actual macOS and
Windows paths, user/shared folder scope, copying banks into these folders, adding
custom folders, refreshing and selecting banks, permissions and moved-file
recovery. Explain that bank files remain external to saved projects. Publish
these instructions with the implemented feature; do not advertise planned
discovery as already available.
