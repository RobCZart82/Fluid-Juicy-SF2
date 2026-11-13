# Real-bank acceptance and output headroom

The regression executable can run an opt-in bank audit through the actual plugin
processor. No user bank is bundled, uploaded or modified. Ordinary CI uses only
the original synthetic test-parts.sf2 fixture in a fifth CTest smoke suite.

After building with JUICY_BUILD_TESTS=ON, run:

```sh
build/arm64/plugin/JuicyRegressionTests_artefacts/Release/JuicyRegressionTests \
  --bank-audit "/absolute/path/your-bank.sf2" "/absolute/path/report.json"
```

Use the corresponding x86_64 build path and .exe suffix on Windows. The executable
also accepts SF3 banks. Reports are written locally. Large banks are tested one
at a time, and the original processor is destroyed before creating the recall
instance, avoiding two fully loaded sample banks during the state test.

## Checks and measurements

- Bank load and exact bank-0/program addresses for 128 GM positions.
- Audible output at velocity 100, probing MIDI keys 60, 48 and 72 where needed.
- Audible standard-kit output on channel 10 for keys 35-81.
- A simultaneous 16-part scene: one held note per channel, volume 100, default
  -12 dB Output, at 44.1 kHz with 64-frame blocks.
- Finite audio, peak/RMS and samples above unity. The meter must report the same
  peak and over-full-scale state as the rendered samples.
- ProcessBlock median/p99/maximum timing after 100 warm-up blocks. These are local
  observations, not a real-time deadline guarantee or a whole-host benchmark.
- Exact all-part state round-trip in a fresh processor, with distinct per-part
  volume/pan values, and no engine destruction on the render stack.

--bank-audit returns failure for missing/silent GM test positions, non-finite or
silent ensemble audio, mismatched metering or failed state round-trip.
--bank-smoke uses a deliberately partial synthetic bank, requiring audible
instruments/drums without demanding 128 GM positions.

A sound at a program/key position does not prove its musical identity, full key
range, velocity layers or GM2/GS behavior. Effect tails, host MIDI timing,
long-session stability, SF3 stress, real REAPER routing and clean-machine testing
remain separate release gates.

## Selected primary banks

The user's primary trial set is Tyroland Fixedup10, LiveHQ Natural GM V3.0 and
Timbres of Heaven 4.00(G). In the local acceptance run all three produced audible
output at 128/128 GM positions and 47/47 tested standard-kit keys, and all-part
project state round-tripped successfully.

The fixed 16-part scene produced approximately these output peaks:

| Bank | Linear peak | Peak dBFS |
| --- | ---: | ---: |
| Tyroland Fixedup10 | 0.376 | -8.5 |
| LiveHQ Natural GM V3.0 | 0.604 | -4.4 |
| Timbres of Heaven 4.00(G) | 2.882 | +9.2 |

This is one controlled stress scene, not a loudness/quality ranking or a maximum
for every performance. Reports with exact source paths, measurements and test
revision are retained outside the repository on the implementing PR's local
workspace. These banks remain external user assets.

## Output meter

The bar below Output shows post-gain peak level in dBFS. OVER latches when an
output sample exceeds unity (0 dBFS), including while the editor is closed.
Click the meter to clear the indication. A new exceedance will latch it again.
Meter state is transient and not saved or reported as a project edit.

The meter observes the existing floating-point output; it does not clip, limit,
compress, normalize or automatically lower it. Exceeding unity inside a floating
host is not itself proof of audible distortion, but downstream conversion or
processing can run out of headroom. Lower Output or part levels while watching
the host meter. The Timbres scene above needs roughly 10.2 dB additional reduction
to leave 1 dB peak headroom; other passages/polyphony may require a different level.

## GUI bank recall correction

A regression reproduces MIDI channel 2 selecting bank 8/program 1, followed by a
GUI selection of bank 0/program 0. After saving/reopening, a Program Change without
another Bank Select previously returned to bank 8. Historical CC0/CC32 replay had
changed FluidSynth's pending bank while the prepared-engine preset cache still
reported the saved selection as already applied.

Replayed bank controllers and channel-state transfer now invalidate that cache,
so the saved/UI bank-program intent is resolved again. A separate audio assertion
preserves ordinary live Bank Select followed by Program Change in a later block.
