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

## SoundFont drop folders

The first chooser implementation discovers `.sf2` and `.sf3` files, with case-insensitive
extensions. The plugin and installer will ship without SoundFont banks.

| Scope | Windows 10 and 11 | macOS |
| --- | --- | --- |
| Current user | `<Documents>\SoundFonts\` | `~/Library/Audio/Sounds/Banks/` |
| Shared on this machine | `%ProgramData%\Fluid Juicy SF2\SoundFonts\` | `/Library/Audio/Sounds/Banks/` |

On Windows, resolve Documents and ProgramData using the OS known folders.
Documents can be redirected, including to OneDrive. The ProgramData subfolder
is specific to Fluid Juicy SF2. Apple documents the macOS locations for
DLSMusicDevice. Discovery in other software depends on that software.
[Windows known folders](https://learn.microsoft.com/windows/win32/shell/knownfolderid) ·
[Apple DLSMusicDevice](https://developer.apple.com/library/archive/documentation/MusicAudio/Conceptual/CoreAudioOverview/SystemAudioUnits/SystemAudioUnits.html)

### Discovery and loading

- Scan the configured folders in the background when the SoundFont chooser
  opens. Provide a Refresh action for banks copied in while it is open.
- List readable files without loading their sample data. Show the path when
  filenames collide. Load a bank only when selected or when recalling a project.
- Keep Browse and drag-and-drop available for any accessible location. Allow
  users to add and remember their own library folders.
- Handle missing and unreadable folders gracefully. The plugin must be able to
  read accessible shared folders without elevation. Folder creation is explicit.
- Keep scanning and loading off the real-time audio thread. Define traversal
  limits and symlink handling before implementation.
- Give the saved SoundFont reference priority during project recall. If it has
  moved, offer relocation. Report load failures and preserve the working bank.

### Acceptance and user manual

On Windows and macOS, verify both default folders, custom folders, case-insensitive
SF2/SF3 discovery, duplicate filenames, missing folders, permissions, Refresh,
project recall and moved-bank recovery in REAPER. Confirm that discovery does
not interrupt audio playback.

Document where to copy files, how to open and refresh the chooser, and how to
relocate a missing bank. Explain that SoundFonts stay external to projects.
Publish the user instructions when the feature is available.


### Chooser implementation and remaining gates

The Banks button next to the file picker scans the two default folders and
remembered custom folders on a background worker whenever it opens. Refresh rescans them. Selecting an entry
uses the existing background SoundFont loader and failed-load recovery.
The menu includes each file's parent path to distinguish duplicate filenames.
An unopened library never changes the saved bank or triggers a scan on recall.

The chooser scans only the immediate files in the configured folders. It
skips symbolic links and inaccessible files, accepts case-insensitive SF2/SF3
extensions, and stops at 10,000 directory entries or 256 results. It reports
a reached limit and leaves Browse available for other banks. It creates no
folders and bundles no banks. Editor destruction requests cancellation and
joins the scanner before releasing its callback. A filesystem operation already
in progress may delay that join.

Use Add folder to choose up to 16 additional folders. Remove folder forgets a
remembered location and leaves its bank files intact. The folder list is a
per-user preference shared by instances using the same settings location.
Folder changes reread the latest file under an interprocess lock and replace
it through a temporary file. Invalid settings are reported and preserved.

Settings are stored at the OS user application-data location:
- Windows: %APPDATA%/Fluid Juicy SF2/SoundFontFolders.xml
- macOS: ~/Library/Application Support/Fluid Juicy SF2/SoundFontFolders.xml

A sandboxed host can isolate the settings location. On macOS, Add folder saves
a read-only security-scoped bookmark from the native folder chooser. Scans retain
resolved scopes through menu selection so a selected bank can obtain its own
file bookmark. If access expires, select the folder again with Add folder.
Browse is available when folder access cannot be saved.

Native Windows build and both CTest suites passed locally, including persistence,
duplicate-folder updates, concurrent edits, removal without deleting files,
corrupt-settings preservation and invalid bookmark-size rejection.
Windows reparse-tag checks distinguish redirected links from OneDrive placeholders.
macOS bookmark restart/permission acceptance, OneDrive-only files and manual REAPER/large-bank tests
remain separate from compilation and automated directory tests.
