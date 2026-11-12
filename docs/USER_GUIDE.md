# Using Fluid Juicy SF2

Fluid Juicy SF2 is a development-preview SF2/SF3 instrument for VST3 hosts and
standalone use. No SoundFont is included. The current instrument is single-part;
16-part GM2/GS operation is a future feature.

## Load and play

Load **Fluid Juicy SF2 (RobCZart82)** in REAPER or another VST3 host, or open the
standalone application. Use the file picker or drag an SF2/SF3 file onto it.
The current bank keeps playing during background preparation; a failed change
keeps a working bank. Select the bank and preset, then send MIDI notes.
In standalone, choose devices under **Options → Audio/MIDI settings**.

**Output** controls the final level, with a -12 dB default for new instances.
Watch your host's meters and reduce it if needed. It is not a limiter.
Projects saved by earlier previews retain their previous unity output level.

## Find your SoundFonts

**Banks** scans the immediate contents of these folders and remembered custom
folders. It does not load every listed bank into memory.

| Location | macOS | Windows |
| --- | --- | --- |
| User | `~/Library/Audio/Sounds/Banks/` | `<Documents>\SoundFonts\` |
| Shared | `/Library/Audio/Sounds/Banks/` | `%ProgramData%\Fluid Juicy SF2\SoundFonts\` |

Windows Documents uses the system's actual location, including redirection to
OneDrive. The shared Windows folder is specific to this plugin, not a universal
Windows SoundFont folder. These folders are not created automatically.

Use **Add folder...** for another location, **Remove folder** to forget it, and
**Refresh** after copying files. Removing a location does not delete any files.
Scanning is nonrecursive and bounded; Browse and drag-and-drop remain available
for other banks. Symbolic links are skipped. If settings are busy, retry.
Closing the editor or refreshing cancels delivery of an obsolete scan without
waiting for it. A blocked filesystem call can delay the next scan; removing the
entire plugin still waits for its worker to finish.
On macOS the native chooser saves a read-only access bookmark. If access expires,
select the file or folder again. Cloud-only files may need downloading first.

## Save, reopen and relocate

Save the host project after selecting your SoundFont and preset. Projects store
its path and, on macOS, an access bookmark; they do not embed SoundFont samples.
Keep the external bank available when moving projects between machines.
After a successful bank-reference change or clearing the bank, the plugin sends
its host a state-changed notification, even with the editor closed. Compatible
hosts can mark the project as modified; this does not save the project for you.
Project recall, unchanged references and failed replacements of a working bank
do not generate this notification.

If a project opens without a playable bank, the picker shows **SoundFont
unavailable** and **Locate...**. The full error is available in the tooltip.

1. Click **Locate...** and select the original SF2/SF3 at its new location.
2. Wait for background loading. Cancelling the chooser changes nothing. An invalid
   replacement keeps the original missing reference and settings for another try.
3. Confirm the bank/preset and sound, then save the host project to retain the new
   path. The status row disappears after a successful load.

Browse, drag-and-drop and Banks can also supply the replacement. Recovery retains
parameters, including Output, envelope and the selected bank/program. If the new
file lacks that preset, the plugin chooses an available preset in the same bank,
or the first playable preset overall. File identity is not checked automatically:
choose the same SoundFont to recover the intended instruments.

A failed replacement while a working bank is already loaded keeps that bank and
reports the error. It does not silently swap to a similarly named file. Native
chooser permissions and project recall should be checked in your particular host;
automated builds do not replace manual REAPER acceptance.
