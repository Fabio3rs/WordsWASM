# Gate D0 schemas

The experiment emits NDJSON records with schema
`words-parser-investigation`, version 3. Fixtures use
`words-parser-fixtures`, version 2.

Version 3 adds the exact `analysisProfile` passed to the core, aggregate
assessment/compound counts, and per-candidate derivation, Whitaker-trim
assessment, notices and multi-token span role. Verbal compounds keep one node
per surface token: a `compound-head` is coupled to exactly one
`compound-auxiliary` by H012, and the auxiliary attaches with `aux`. Artificial
Roman numerals are retained as explicitly non-Whitaker candidates. Version 2
remains frozen in `result-v2.schema.json` for historical artifacts.

The measurement rules established in version 2 remain in force:

- morphology, propagation, enumeration, parser, and forest metrics have
  separate namespaces;
- `acceptance.morphAssignments` counts assignments accepted by the selected
  strategy, not syntactic derivations;
- `forest.derivationCount` stays `null` until the strategy constructs a real
  shared forest;
- `gold.preferredLemmaSequence` is explicitly a compatibility signal;
- morphology and dependency gold are evaluated independently;
- `gold.*.bestScoreTie` distinguishes a gold below the maximum score from one
  displaced only by deterministic tie-breaking;
- survivor sets are compared exactly in self-tests by sorted assignment IDs;
  the FNV-1a digest is only a compact report fingerprint;
- `complete-clause` and `fragment` select distinct start grammars;
- product budgets apply before raw Cartesian enumeration, or after propagation
  for strategies that run the prefilter.

`morphologyNBest` is an optional diagnostic payload emitted only with
`--include-nbest`. It contains every morphology assignment accepted by the
selected strategy, ordered by the current manual score, and marks assignments
matching the structured morphology gold. Dependency strategies also include
the deterministic relations projected for each assignment. `complete` is false
if the experiment budget prevented an exact candidate set. This payload is the
input of the Markov ranking investigation; its `manualScore` remains
uncalibrated.

`treeNBest` is emitted under the same opt-in flag for tree-oracle and decoder
strategies. Unlike `morphologyNBest`, it keeps every complete tree as a
separate candidate: canonical tree ID, morphology-assignment ID, morphology
score, arc score, combined manual score, projectivity, tokens, relations and
independent morphology/dependency-gold markers. A reranker can therefore
reorder parser-produced trees without rebuilding arcs or combining fragments
of different survivors.

Each published analysis token also carries `features`, a compact typed
projection of the morphology. A `null` field is not applicable to that
morphology; the string `unknown` remains an applicable but unknown enum value. The gold
morphology report separates `inLattice` from `survives`, so lexical coverage is
not conflated with survival after constraints.

The decision semantics are deliberately asymmetric. A hard-constraint conflict
can mark an analysis impossible; every surviving analysis is only *possible*,
and soft features may rank it as more or less plausible. Version 2 aggregates
rejection counts by constraint ID, but does not yet expose per-analysis scope
and evidence. `bestScore` and `scoreReasons` are auditable, uncalibrated scores,
not probabilities. A future probability field requires an explicit candidate
universe, normalization/calibration method, and held-out validation. Schema v3
does not make that claim.

`morphology.surfaceTokens` is the immutable token sequence shown by the
source. `morphology.lookupTokens` is the sequence actually sent to the WWDB.
When they differ, `morphology.lookupOverrides` records the token index,
replacement and reason. This permits an explicit orthographic lookup such as
source `intelligentior` → WWDB `intellegentior` without silently rewriting the
fixture or its provenance.

`fixtureAnnotation` is `null` for synthetic or still-unverified material. For
promoted didactic fixtures it records the catalog ID, immutable source commit,
page/unit/block, short source text, evidential claim and review date. The
`sourceAsserts` and `editorialAdds` lists deliberately separate information
printed by the source from the experiment's complete verbal analysis and
dependency representation.

The propagation namespace distinguishes the legacy `fixed-point-scan` from
`gac-agenda` and `gac-agenda-residues`. Agenda runs additionally report
`queuePops` and per-variable `revisions`. `supportChecks` counts semantic
witness searches. Cached runs report `residueHits`, `residueMisses`,
`residueInvalidations`, and the cheaper `residueCandidateChecks` separately,
so revalidation is not silently conflated with a fresh support search.

The `relationCandidates` namespace describes typed edges between concrete
morphological candidates. It reports generation totals by relation kind and by
ternary compatibility, plus the edges selected by the top-1 dependency
projection. H005 uses `preposition-complement`, H006 uses `verb-argument`, and
H007 uses `coordination`. H011 uses `comparison-standard`: without `quam` its
dependent must be ablative; with a selected adverbial/conjunctive `quam`, the
edge stores both the marker and the first comparison term in `contexts`, and
the two terms must have the same case. An incompatible edge is never selected;
`indeterminate` survives, but does not receive the S008 known-government
reward.

The `attachmentSearch` namespace is populated only by
`dependency-attachment-search`. It enumerates the exact H005/H006/H007/H011 choice
set for every accepted morphology assignment. `analysisIds` combine the
morphology assignment with sorted relation-lattice indices; `analysisSetDigest`
is their compact fingerprint. `projectionChecked` and `projectionInSearch`
measure whether the deterministic baseline is a member of that exact set.
`slotsCreated` counts slot instances across morphology assignments, rather than
unique linguistic roles. This namespace does not claim full dependency-tree
enumeration: heads for unconstrained tokens, acyclicity, projectivity, and
connectedness are reported separately by `treeSearch`. The same `maxProduct`
safety limit caps the number of materialized attachment analyses; exceeding it
returns `experiment-budget-exceeded` without publishing a partial analysis
set.

The `treeSearch` namespace is populated by `dependency-tree-oracle`. Arc
domains cover every selected surface token. The exact DFS assigns one head per
token and accepts only structures with one root and no directed cycle; those
conditions imply a connected dependency tree. It separately counts projective
and nonprojective analyses, early cycle/root rejections, canonical tree IDs,
and membership of the deterministic projection. `bestArcScore` is the sum of
the auditable T001 arc heuristics and is also included in `bestScore` with the
morphology score. For this strategy, gold ranks range over complete trees,
whereas projection/attachment ranks range over morphology assignments. The
same `maxProduct` cap prevents an incomplete exact tree set from being
published.

Projectivity is computed with an artificial root immediately to the left of
the sentence. Consequently, an arc spanning the selected root can cross the
artificial root arc even when no two ordinary token-to-token arcs cross. This
is the same convention consumed by the Eisner chart.

The `decoder` namespace is populated by `dependency-eisner` and
`dependency-mst`. Both collapse competing labels for one head-dependent pair
to the highest-scoring arc and enumerate the permitted root token, so every
output still has exactly one root. Eisner reports completed chart/split work;
Chu–Liu/Edmonds reports examined edges and contracted cycles. These values are
not interchangeable units. Each decoder emits one best tree per surviving
morphology assignment. `scoresByAssignment` is checked against, respectively,
`treeSearch.bestProjectiveScores` and `bestUnrestrictedScores`; decoder IDs
must also belong to the exact oracle set.

See `result-v1.md` for the frozen interpretation of historical fields.

`markov-result-v3.schema.json` validates the separate JSON document emitted by
`markov_parser_ranker`. It records `leave-one-fixture-out`, `in-sample` or an
`exposure-curve`, the evaluation tier, state projection, smoothing,
surface/parser-canonical mixture, target exposure, negative-control seed, tie
policy and per-fixture ranks. Its ranking ablation reports manual-only,
Markov-only and Markov-then-manual metrics separately. Training provenance separates verified
didactic gold, attested morphology gold, controlled synthetic gold,
Latin Dependency Treebank morphology gold, preferred-lemma silver and
synthetic relinearizations, each with an explicit weight. It does not add a
probability field to parser result v3. Version 3 also pins the core analysis
profile used to construct the candidate lattice.

The Markov report additionally records the full evaluation denominator and
per-model diagnostics for unknown states and contexts. Its canonical encoding
orders recursively signed subtrees and emits enter/exit events, preventing
token-index dependence and preserving subtree boundaries. Additive smoothing
remains the default baseline; `hierarchical-backoff` is an optional ablation,
not a selected production architecture.

`dependency-markov-result-v2.schema.json` validates the separate structural
experiment emitted by `dependency_markov_ranker`. It records the exact tree
candidate policy, verbal-root convention, leave-one-fixture-out policy and
three factor families: root, labeled local arc, and an optional joint
predicate-dependent profile. Orders 1 and 2 mean local head and
grandparent+incoming-relation+head respectively; they do not refer to surface
token adjacency. The report keeps model-only, model-then-manual and manual-only
ablations, plus root accuracy, UAS, LAS, exact-tree ranks and backoff coverage.
Version 2 pins the same core analysis profile as the parser and sequential
reranker artifacts.
