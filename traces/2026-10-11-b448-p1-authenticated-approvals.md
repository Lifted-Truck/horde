# b448-p1-authenticated-approvals — repo side of code-owner approval for the ratchet ledgers

- **Queue item:** B448 phase 2 Wave 1, package P1 (ADR-209 items 2 and 3).
- **Why:** an agent could write "approved" into a ratchet ledger itself. The human ruled that only they
  approve; the mechanism is GitHub code-owner review. This change is the repo half: a CODEOWNERS file
  naming the human for each ledger, a home for accepted limits and hole expiries, a check that the owner
  record and the ledger list cannot drift, and plain-words text on each approve path. The ruleset toggle
  and the live drill are the human's and are not done here.
- **Evidence consulted:** `docs/strategy/blind-spot-armor-phase2.md` §3 row P1; ADR-209 in `DECISIONS.md`;
  `tools/{weakening,tolerance_registry,param_id_lock,build_flags,license_audit}_check.py` (their pin paths
  and `main()` approve paths); `tools/license_allowlist.json` and `docs/armor/tolerances.json` (house style);
  `docs/armor/catalogue.json` (row ids); `tools/test_table_check.py` (UNWIRED header rule).
- **Alternatives rejected:** a catch-all `*` rule (brief forbids; the check is red on one); owning
  CODEOWNERS itself, `tools/ledger_owner_check.py` and the ratchet tools' sources (outside the brief's list:
  raised as an open question); adding `docs/port/divergences.json`, `h2/cores/swarm/lift-ledger.json`
  (pinned by checks but not in the brief's list: shown every run as PINNED, NOT A LEDGER YET for the human to
  rule); a printed approval on `license_audit_check` (it has no approve path: its exceptions are hand-edited).
- **Verify:** first run `./verify fast` exit 1: the brief's `UNWIRED:` header counted as a new `unwired`
  weakening marker (0 -> 1), which only the human may approve. The lead amended the brief: wire the check
  instead (one added line in `verify` after `weakening_check`, header flipped to `WIRED: ./verify fast`).
  Second run `./verify fast` exit 0 (`.harness/last-verify.json`, base git 7f5116b plus the uncommitted wiring);
  `weakening: 80 markers across 6 categories, 0 increases`; `docs/armor/weakening-baseline.json` unchanged
  against origin/main; `ledger_owner_check: GREEN (... 15 controls fired as expected)`.
- **Open questions:** whether the human approves the `unwired` marker, or the lead wires the check in this PR
  instead; whether CODEOWNERS, the ratchet tools' sources and `tools/ledger_owner_check.py` should be owned
  too (an agent can otherwise edit the owner record or the check); the two unlisted pinned files.
