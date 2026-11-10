# Reliability bug ledger

Audit baseline: `02a3bad63d70e32249a992cb29332c86c00366d3` (2026-11-10).
See [the complete audit](AUDIT-2026-11-10.md). Status records implementation and
validation separately; CI is not manual REAPER acceptance.

| ID | Priority | Issue | Implementation | Validation |
| --- | --- | --- | --- | --- |
| F01 | P1 | Empty-bank left/right navigation divides by zero | Open | Regression needed |
| F02 | P1 | Parameter callbacks directly mutate GUI; host program change takes message lock | Open | Editor-open worker-thread regression needed |
| F03 | P2 | MIDI Program Change can undo preceding Bank Select | Open | Two-bank regression needed |
| F04 | P2 | Failed replacement discards working bank | Open | Missing/corrupt-bank recovery regression needed |
| F05 | P2 | Loading is synchronous and shared engine state is not handed over safely | Open | Loader design and real-time stress tests required |
| F06 | P2 | Host is told the instrument has no release/effect tail | Open | Tail-render test and reporting policy required |
| F07 | P2 | Packages do not bind plugin source revision to binary build | Open | Stale-build/source mismatch regressions needed |
| F08 | P2 | Current privacy notice describes obsolete JUCE 5/ROLI behavior | Open | Review current source and replace current notice |
| F09 | P3 | Decay/sustain long display descriptions swapped | Open | Preserve parameter IDs; verify corrected display names |

## Sequence

1. F01/F02: protect the first-use GUI path and isolate parameter-driven GUI updates.
2. F03/F04: correct bank/program state and preserve a working bank on failure.
3. F05/F06: loader/real-time design and render tails.
4. F07/F08/F09: exact-source package evidence, current privacy text and naming.

No SoundFont bank is bundled. Original synthetic test fixtures are test data,
not a user sound library. Preserve the familiar interface and plugin identity.
