# Repository review corrections — 2026-11-11

Baseline: `15799d88615970dba6893ab9c4e23913bf8344dc` (merged PR #20).
These corrections address the four reproducible P2 findings and the measured
output-headroom observation from the subsequent full repository review.

| ID | Correction | Regression evidence |
| --- | --- | --- |
| C01 | Synchronous restore supersedes a GUI fade at the next block. Retired engines are reclaimed off the render stack within a four-engine allocation budget. | Sixteen GUI-fade/host-restore cycles adopt the latest request on the next block; no audio-thread engine destruction. |
| C02 | Validate JUCE bookmark length, size, alphabet and payload before decoding; reject invalid project state before changing the bank or parameters. Project bookmarks are limited to 256 KiB; folder bookmarks to 16 KiB. | Oversized, negative, truncated and malformed payloads preserve the entire state; historical empty encodings and valid bytes round-trip. |
| C03 | Move folder edits onto the scanner worker; bound process/interprocess lock acquisition to 250 ms and check cancellation every 5 ms. | A separate process holds the real lock for 1.5 s; reads return busy, cancelled edits preserve bytes, subsequent reads recover. |
| C04 | Extract regular source files before validated file links; reject link pivots, traversal, directory links and duplicate files. | Six archive tests including the reported pivot, Unicode aliases, hardlinks and executable files; existing corresponding-source archive verification. |
| H01 | Append an Output parameter with 20 ms smoothing and a -12 dB new-instance default; legacy parameter states restore at unity. | Audio-energy comparison confirms 12 dB attenuation; parameter order and explicit state recall preserved. |

Local macOS arm64 Release builds include VST3, standalone and regression targets.
All three CTest suites pass normally and with AddressSanitizer and ThreadSanitizer.
The sanitizers instrument project/JUCE code, not the reused static codec/FluidSynth
libraries. CI results for Windows and Intel are recorded on the implementing PR.

No limiter is introduced: arbitrary banks/polyphony can still exceed full scale.
Underlying file I/O and FluidSynth decoding cannot be interrupted midway. Manual
REAPER long-session, offline-render and permission-restart acceptance remain
release gates. See [loader ownership](LOADER_ARCHITECTURE.md) and
[output compatibility](MODERNIZATION.md#output-level-and-recall-compatibility).
