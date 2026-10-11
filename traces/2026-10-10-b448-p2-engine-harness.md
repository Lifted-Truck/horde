# b448-p2-engine-harness — the engine harness, the reference patches and the enrolment gate

- **Queue item:** B448 (Blind-Spot Armor), phase 2 Wave 1, package P2, ruled in ADR-209. Proposal: `docs/strategy/blind-spot-armor-phase2.md` section 2 rule 4 and section 3 row P2.
- **Why:** P3 (fuzz), P4 (invariance matrix) and P6 (soak) are each meant to be a thin loop over one harness, and rule 4 says an engine that has not joined it must be red. Before this there was no interface that drives an engine without knowing which one, no fixed patch set, and nothing that noticed a new directory under `h2/`.
- **What changed:**
  - `h2/harness/harness.h`: the `Adapter` interface (prepare, set, setString, snap, noteOn, noteOff, panic, render, preLimiter), the job language (the scenario language's own words) and `runJob`, an output digest, and three control adapters: `control/silent`, `control/tone` (a level known in closed form, right channel exactly half the left) and `control/block` (writes each render call's length into its frames, so its output must change with the host block size).
  - `h2/harness/adapter_composed.h`: the composed engine through its public interface only. `preLimiter(out)` is `atanh(out)`.
  - `h2/harness/adapters.h`, `h2/harness/render_cli.cpp` (`h2_harness_render`: job file in, chunked sample file out; judges nothing).
  - `h2/harness/harness.mjs`: what tools call (`enrolled`, `patchesFor`, `render`, `run`, `jobFor`, `patchCmds`, `features`). `features` is `tools/patchspace/metrics.mjs`'s `analyse` and nothing more.
  - `h2/harness/gen_patches.mjs` and the generated `h2/harness/patches.json`: ten patches, seven ledger presets and three corners, each recording its source.
  - `h2/harness/selftest.mjs`: 30 rows, including the must-read-loud control.
  - `docs/armor/engines.json`: four units. `h2/engine` is enrolled as `composed`; `h2/cores/scalpel` and `h2/cores/swarm` are entered as test references and `h2/harness` as the harness, each with its reason.
  - `tools/engine_enrolment_check.py`: static; finds the code directories under `h2/` itself.
  - `verify`: one line, `python3 tools/engine_enrolment_check.py || ok=1`, after the `h2_rules_check` line. The lead's amendment to the brief allowed exactly this edit.
- **The brief changed twice while this was built, and one thing is NOT done:**
  1. The brief said to mark the check as not wired in its header. Measured: `tools/weakening_check.py` counts that marker and read red on the new file (`unwired 0 -> 1`). The lead amended the brief: wire the check in `fast` with one line, and have it compile what it needs itself.
  2. Measured: a compile line in a `tools/` script is a `direct_compiles` entry of `tools/build_flags_check.py`. With the self-compile in place that check read `direct_compiles: 'tools/engine_enrolment_check.py' pinned '<absent>'`, which only `--approve` clears, and the amendment forbids running any approval. So the self-compile was removed again.
  3. **Result: the static half is wired and gating. The behavioural half (`selftest.mjs`, where "an adapter that returns silence fails must-read-loud" lives) is green by hand and is NOT run by `./verify`.** `h2/harness/README.md`, "Wiring the selftest", gives the two ways to close it (a CMake target in `full`, or an approved self-compile in `fast`). The enrolment check prints "STATIC ONLY" in its green line on every run so the gap cannot read as covered.
- **Evidence (all measured 2026-10-10, darwin-arm64, Apple clang, Node 24; `<s>` is this session's scratch directory):**
  - `python3 tools/engine_enrolment_check.py`: GREEN, 4 units, 33 must-fail controls fired (16 plants on a synthetic tree, the same 16 on the real tree, and the discovery control in a scratch repository), 0.15 s.
  - A real un-enrolled directory, `h2/zz_probe_engine/probe.h`, created in the working tree: the check exited 1 with `UNENROLLED: h2/zz_probe_engine holds code and has no entry in docs/armor/engines.json`. Removed; green again.
  - `node h2/harness/selftest.mjs --bin <s>/hr_O2`: GREEN, 30 rows, 0.6 s. Loud patches read -12.1 to -28.0 dBFS RMS in its window (one note); the floor is -60.
  - Live mutation: `ComposedAdapter::render` changed to zero its output, a separate binary built from it, selftest run: FAILED, 12 of 29 rows (it had 29 then; the second block row came after), all nine loud patch rows reading `silent by metrics.mjs's rule`. The file was restored from a copy and its SHA-1 matched the one taken before.
  - The pre-limiter recovery against the engine's own stage hook (a scratch program built with `H2_ENGINE_STAGES`, `stageOff = kStageOut`, the defaults patch, 8 voices, 1 s; nothing committed): gain 0.35, true peak 3.02, worst difference 5.3e-15; gain 1, peak 8.63, worst 3.9e-10; gain 8 (outside the declared range), peak 69.0, 12504 of 96000 samples on the rail and read as infinity.
  - Observed, not judged (P4 owns it): all ten reference patches, 8 voices, 0.5 s with a release at 0.3 s, gave one digest at host blocks 128, 1, 7, 64, 512 and varying 1..512, and the same digests from the `-O2` and `-O3` builds. Sixty jobs, 16 s. `control/block` shows in the selftest that the block size does reach the adapter.
- **Evidence consulted:** the proposal sections 2, 3, 5 and 7; `h2/README.md`; `h2/engine/engine.h` (public interface, the last line of `renderCallK`, `renderCalls`' float path, the `H2_ENGINE_STAGES` hook), `blade.h`, `js.h`; `tools/measure_h2_engine.cpp` and `docs/port/cpu-ledger.md` (the seven names, the cell); `tools/h2_scenarios.mjs` (`presetCmds`), `tools/h2_engine_stream.h`; `tools/patchspace/{metrics.mjs,space.mjs,dependency_tree.json,README.md}`; `tools/auhost/analyse.mjs`; `tools/labharness/sandboxed_node.mjs`; `tools/{test_table_check,weakening_check,tolerance_registry_check,build_flags_check,h2_rules_check,include_check,banned_api_check,rtsan_check}.py`; LIBRARY index entries L0016, L0032, L0053, L0056, L0073.
- **Alternatives rejected:**
  - A new engine accessor for the pre-limiter bus: forbidden by the brief, and not needed below the rail (`atanh` is exact enough there, measured above).
  - Reading the bus through `H2_ENGINE_STAGES` in the harness build: the hook is documented as the measure tool's only, it also skips the DC blocker, and it would make the harness build differ from the shipped one.
  - Importing `tools/h2_scenarios.mjs` for `presetCmds`: it evaluates lab text under the lab sandbox, which refuses an entry script with no profile row and forbids child processes. The one rule needed (gain 0.35 after the params) is restated with its source.
  - Deriving the corners by running the lab's table (`space.mjs`): same sandbox. `dependency_tree.json` already carries every declared range as data.
  - Content-hash pins of the patch sources inside `patches.json`: a comment edit in the ledger tool would have turned the staleness row red for nothing (L0073). `--check` regenerates and compares the whole text instead.
  - Corners chosen per parameter ("feedback at maximum"): a judgement. The three corners are whole-table rules.
  - A JSON reader in C++: the render tool takes the line-oriented job language instead, and `harness.mjs` turns a patch into it.
  - Counting non-finite samples in the C++ tool: `std::isfinite` under `h2/` is a `dsp_guard` marker that needs approval. `metrics.mjs` counts them on the JS side.
  - An input path for effect modules in `Adapter`: no module exists under `h2/` to design it against.
  - Naming the selftest so the wiring rule cannot see it, or building flag strings so the flag pin cannot: both would be dodging a gate. The selftest is left visibly not run instead.
- **Verify:** `./verify fast` exit 0, `.harness/last-verify.json` `{"target":"fast","exit":0,"git":"27047c2","ts":"2026-10-11T02:05:50Z"}` (git is the base commit; the change was staged, not committed, when it ran, and this trace was not yet in it). The run on the commit itself is quoted in the PR. `./verify full` was NOT run: nothing this package adds is built or run by `full` yet.
- **Open questions:**
  1. Wiring the selftest (above). Until then S5's "an adapter that returns silence is red" is true only by hand.
  2. Are `h2/cores/scalpel` and `h2/cores/swarm` rightly exempt as test references? `h2/README.md` calls them that; the check holds them to it (nothing enrolled may include them). If the human wants them enrolled, each needs an adapter and patches.
  3. `docs/armor/engines.json` is an approval ledger in effect (an exemption is one line). It is not in P1's code-owner list.
  4. The selftest's thresholds sit outside `docs/armor/tolerances.json`'s scan (`.mjs`).
  5. `metrics.mjs` lacks spectral centroid, an envelope measure, an onset measure, a pitch estimate in Hz or cents, and K-weighted loudness away from 48 kHz. P4's rate axis needs them; P9 was to add them, and P4 comes first in the wave order.
  6. P6 needs a streaming reader in `harness.mjs` (the file format allows it) and, if it wants the voice cap, an adapter key for it.
  7. `h2/README.md` and `docs/armor/README.md` do not mention the harness or the registry; both were outside this package's files.
