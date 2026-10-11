# B457 steps (a) to (c): kit 2.9.0's closing gate adopted

**Date:** 2026-10-11. **Row:** ROADMAP B457. **Ruling:** ADR-210 item 2. **Author:** the lead, first-hand
(the change is under `.claude/`).

## What changed
- `.kit/` synced from kit 2.7.0 to 2.9.0 by the kit's own `kit_sync.py`: `kit-gates.sh` updated,
  `stop-gate.sh` new. Both are kit-owned and checksummed in `.kit/MANIFEST`.
- `.claude/hooks/stop-gate.sh` is now the kit's shim. horde's own tree-state code (PR #1029) is gone
  from it. One line is horde's: `export KIT_STOP_GATE_MODE=deny`, so the tree test blocks here
  from the start.
- `tools/stop_gate_check.py` is rewritten as horde's contract test: it runs the real shim, the real
  vendored gate and the real `record` in scratch repositories. 20 rows.

## Why
The kit's gate compares a content fingerprint of the tree with the one `./verify` recorded. Against
horde's file-time version it sees a file deleted after verify (our stated limit, now a block row),
and it allows an edit that was put back to the verified bytes and a tree with no local work.

## Evidence
- `stop_gate_check`: GREEN, 20 rows. A hook that never blocks fails all 12 block rows. The shim
  without its deny line fails exactly the 7 tree-state rows, which proves that line is what makes
  them block. Run five times in a row, same verdict.
- `kit_sync.py . --check`: current, kit 2.9.0.
- `./verify fast`: exit 0. The record now carries `tree`.
- The fingerprint takes about 0.2 s on this tree (1134 tracked files), measured once.

## A finding about the vendored fingerprint, reported to the kit
Measured here on 2026-10-11: an edit that keeps a tracked file's size, made in the same second as
the time git last recorded for that file, is not seen when the gate runs in a later second. With
the timing arranged that way it was allowed 60 times in 60. With the index copied so that it keeps
its time (`cp -p`), it was blocked 60 times in 60. The file is kit-owned, so the fix is the kit's;
the report is filed on the thread `stop-gate-tree-state`. This test's edits change the file's size
so that its verdicts do not depend on the clock.

## Not done here
- Step (d), wrapping `./verify`'s gates with the receipt helper: after the Wave 1 wiring.
- Step (e), the pilot report: after (d).
- `project.manifest.json` still says kit 2.5.0. Nothing reads it for the vendored files; left for a
  manifest pass.
