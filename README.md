# Fluid Juicy SF2

**SoundFont Player · VST3 & Standalone**

An open-source SF2/SF3 instrument built with **JUCE 9** and **FluidSynth 2.6.2**.
A modernization of [Juicy SF Plugin](https://github.com/Birch-san/juicysfplugin),
keeping its familiar interface and bank/preset browser.

**Development preview.** No stable release of this fork is available yet.

## Features

- SF2 and SF3 SoundFont playback.
- Bank and preset selection, on-screen keyboard and MIDI input.
- Filter, attack, decay, sustain and release controls.
- VST3 plug-in and standalone application.
- Build targets: macOS Apple Silicon, macOS Intel and Windows x64.

The current instrument is single-part. Full 16-part GM2/GS support is planned.

## Download

Development packages are available from [GitHub Actions](https://github.com/RobCZart82/Fluid-Juicy-SF2/actions).
Open a successful run for the branch/commit you want and choose your platform:

| Platform | Workflow |
| --- | --- |
| macOS Apple Silicon | Native Apple Silicon |
| macOS Intel / Windows x64 | Intel and Windows validation |

Each artifact contains VST3, standalone, matching pinned sources and SHA-256
checksums. GitHub sign-in may be required. Packages are temporary development
downloads, not stable releases. macOS packages are ad-hoc signed, not notarized;
Windows packages are unsigned. REAPER and minimum-OS acceptance remain open.

## Quick start

1. Load **Fluid Juicy SF2 (RobCZart82)** in a VST3 host such as REAPER, or open the standalone app.
2. Drag an `.sf2` or `.sf3` file into the SoundFont picker.
3. Select a bank and preset, then play using MIDI or the on-screen keyboard.

In standalone mode, select your audio device and MIDI input under
**Options → Audio/MIDI settings**. SoundFonts are not included.

## Build from source

Requires **CMake 3.24+**, Python 3, Git and a C++17 toolchain:
Xcode Command Line Tools on macOS, or Visual Studio 2022 with Desktop C++ tools
on Windows.

```sh
# macOS Apple Silicon
python3 scripts/build.py --arch arm64

# macOS Intel
python3 scripts/build.py --arch x86_64

# Windows x64 — use a short build path
python scripts/build.py --arch x86_64 --build-dir C:/fj-build
```

The script downloads pinned dependencies, builds VST3 and standalone, and runs
processor tests. It does not install into your plug-in folders.
See [compatibility and dependency notes](docs/MODERNIZATION.md).

Repository layout: [active development](docs/REPOSITORY_LAYOUT.md) · [legacy archive](archives/README.md).

## Credits and license

Maintained by [RobCZart82](https://github.com/RobCZart82).
Based on **Juicy SF Plugin by Alex Birch**, with
[JUCE](https://github.com/juce-framework/JUCE) and
[FluidSynth](https://github.com/FluidSynth/fluidsynth).

Original source: [GPLv3](LICENSE.txt). JUCE 9 is used under its AGPLv3 option;
combined distributions must also meet its terms and the other dependency licenses.
Original author notices are retained. SoundFonts have their own licenses.
