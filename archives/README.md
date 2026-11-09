# Archives

Historical upstream build files and dependencies live in `legacy/`. They are
retained for provenance and reference; the modern CMake build does not use them.
Original files, binary contents and license notices are preserved unchanged.

| Archived path | Historical purpose |
| --- | --- |
| `Builds/` | Xcode and Visual Studio projects, bundled libraries and packaging |
| `JuceLibraryCode/`, `modules/` | Projucer-generated wrappers and bundled JUCE 5 modules |
| `include/`, `licenses_of_dependencies/` | Old dependency headers and their license notices |
| `VST2_SDK/` | Historical VST2 SDK placeholder and instructions |
| `win32_cross_compile/`, `win32.Dockerfile`, `.dockerignore` | Windows cross-compilation and Docker tooling, including x86 support |
| `distribute/` | Old release packaging scripts |
| `cmake/Modules/` | Legacy dependency discovery modules |
| `juicysfplugin.jucer` | Historical Projucer project |
| `building.macos.md`, `building.win32.md` | Superseded build instructions |
| `Juicy.png` | Historical upstream artwork |

Paths inside archived files are intentionally unchanged and may refer to their
former locations. These files are not a runnable build tree in this directory.
For the complete pre-archive layout, use commit
`0e36934264c3b5b419f609acff7891c089001d0f` in a separate checkout. That layout
includes the modernization changes; the original upstream baseline is
`c95eb5755aab634a22dfa778912facd136590d5c`.

Current instructions are in the root [README](../README.md), with the supported
build entry point at [`scripts/build.py`](../scripts/build.py).
The maintained JUCE include header is now [`Source/JuceHeader.h`](../Source/JuceHeader.h).
