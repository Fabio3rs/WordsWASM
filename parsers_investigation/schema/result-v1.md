# Result schema v1 — frozen historical contract

Version 1 was emitted by the first parser PoC. It is frozen and is not emitted
by the current executable. Existing `REPORT.md` revisions generated from it
remain historical evidence only.

The following names were found to overstate or combine measurements:

| v1 field | Actual v1 meaning | v2 replacement |
|---|---|---|
| `goldRank`, `goldSurvives` | Preferred lemma sequence only | `gold.preferredLemmaSequence` |
| `completeParses` | Valid assignments, projected graphs, or estimated finite anchors depending on strategy | `acceptance.morphAssignments`; derivation count is `null` |
| `statesCreated` | Propagation iterations + enumeration nodes + parser units | Separate `propagation`, `enumeration`, and `parser` counters |
| `statesReused` | Duplicate Earley deductions or duplicate explicit stacks | `parser.duplicateDeductions` with an explicit parser kind |
| `packedNodes` | Chart items, explicit stacks, or dependency relations | Separate parser/dependency metrics; SPPF metrics are `null` |

Strategy names are likewise frozen as historical aliases:

| v1 | Canonical v2 name |
|---|---|
| `cartesian` | `cartesian-leaf-check` |
| `propagation` | `worklist-prefilter` |
| `dependency` | `dependency-projection` |
| `earley` | `earley-fixed-point-recognizer` |
| `glr` | `gslr-stackset-recognizer` |

The CLI accepts the old strategy spellings for reproduction, but always emits
the canonical v2 name.
