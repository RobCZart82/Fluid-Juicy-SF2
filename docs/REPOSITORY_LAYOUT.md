# Repository layout

| Path | Purpose |
| --- | --- |
| `Source/` | Plug-in, interface, MIDI and FluidSynth integration |
| `resources/` | Current application icon |
| `scripts/` | Pinned dependency build and macOS packaging |
| `cmake/` | Helpers used by the modern build |
| `tests/` | Processor regressions and original test SoundFont generator |
| `.github/workflows/` | Native Apple Silicon, Intel macOS and Windows x64 validation |
| `docs/` | Compatibility, dependency and development notes |
| `archives/` | Historical projects, vendored dependencies and retired tooling |
| `build/` | Local generated files and downloaded pinned sources; ignored by Git |

Use `scripts/build.py` for the supported VST3 and standalone build. Dependencies
come from its pinned source revisions, not from the archived modules or libraries.
32-bit Windows and VST2 are historical targets, not part of the current build.

The archive is excluded from CMake targets. Matching-source packages retain it
as repository history and preserve all original notices. Archiving files reduces
root-directory clutter; it does not reduce the Git history or download size.
