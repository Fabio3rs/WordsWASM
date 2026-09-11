# Morphological assessment and provenance

The engine keeps mechanically generated candidates separate from decisions
about whether to display or rank them. This is important for historical Latin:
a form rejected by Whitaker's `Trim_Output` policy is not thereby impossible.

Native `AnalysisIR` carries a `MorphologicalAssessmentIR`. Native JSON exposes
it only in schema v2 (`--format analysis-v2` or `search-v2`). The WebAssembly
API exposes the same information as typed Embind objects in browser schema v4;
JSON parsing and serialization remain outside the WASM binary.
The browser contracts are documented by
`schemas/browser-search-v4.schema.json` and
`schemas/browser-analysis-v4.schema.json`.
The Pages demo maps the typed reason/notice codes to explanatory English,
Brazilian Portuguese, and Latin strings in `web/app.js`; those presentation
strings are deliberately absent from WWDB and the WASM module.

## Whitaker trim compatibility

`whitakerTrim.compatible` means “this candidate survives the historical
`Allowed_Stem` checks when `Trim_Output` is enabled.” It does **not** mean
“valid Latin.” When compatibility is false, `reasons` contains one or more of:

- `unsupported-short-imperative`
- `invalid-imperative-person`
- `impersonal-non-third-person`
- `deponent-active-form`
- `semideponent-passive-present-system`
- `semideponent-active-perfect-system`

Annotation is the only engine policy. Every candidate remains in the native
result and in every JSON/browser projection; consumers receive compatibility
reasons and notices instead of a switch that reproduces Whitaker's removal.
The schema-v2 option field remains `whitakerTrim: "annotate"` for existing
readers, but it is descriptive rather than configurable.

## Reviewed semideponent exceptions

Whitaker generates passive present-system candidates for `audeo` and then
removes them under `Trim_Output` because the lexeme is marked semideponent.
That is useful ranking evidence, but it is too strong as a validity judgment.

Bennett and Allen & Greenough describe the ordinary semideponent paradigm as
active in the present system and passive in form (with active meaning) in the
perfect system. Lewis & Short, however, records genuinely passive uses of
`audeo`, including `audebantur` and `auderi`. Consequently a present-system
passive candidate for the `audeo` lexeme receives these notices:

- `related-passive-usage-attested`
- `source-disagreement`
- `manual-review-recommended`

“Related usage attested” is intentionally narrower than “this exact queried
surface is attested.” Exact-form attestation requires a separate corpus layer.

The same distinction now covers two additional reviewed cases. Livy 24.8
contains the impersonal passive `diffideretur`, so present-system passive
candidates for Whitaker's `diffido` entry receive the same three related-use
notices. Allen & Greenough records the old perfect subjunctive `ausim`, and
Lewis & Short records the exceptional active perfect `ausi`; active
perfect-system candidates for the two `audeo` entries therefore receive
`source-disagreement` and `manual-review-recommended`. These trigger-level
summaries invite analysis-specific review; they do not attest every generated
cell in either subparadigm.

The reviewed mapping is data, not an engine special case. The human-readable
`whitakers-words/MORPHOLOGICAL_NOTICES.LAT` ledger targets the two `audeo`
entries plus the reviewed `diffido` entry, and the WWDB packer emits one
three-byte record per
`(LexemeId, WhitakerTrimReason)`: a 16-bit lexeme ID followed by three trigger
bits, three notice bits, and two reserved bits. No notice name, source URL, or
documentation string is stored in WWDB or WebAssembly. The full database keeps
this sparse section row-major; the search database keeps its large hot tables
columnar but uses the same row-major notice section.

References:

- [Bennett, New Latin Grammar](https://www.gutenberg.org/cache/epub/15665/pg15665-images.html)
- [Allen & Greenough, Deponent Verbs](https://dcc.dickinson.edu/grammar/latin/deponent-verbs)
- [UK National Archives, deponent and semi-deponent verbs](https://www.nationalarchives.gov.uk/latin/stage-2-latin/lessons/lesson-22-deponent-and-semi-deponent-verbs/)
- [Lewis & Short, *audeo*](https://atlas.perseus.tufts.edu/dictionaries/entry/urn%3Acite2%3Ascaife-viewer%3Adictionary-entries.atlas_v1%3Alat.ls.perseus-eng2-n4450/)

## Source disagreement

`source-disagreement` is a curated flag, not a conclusion inferred from the
surface alone. It is emitted only for reviewed cases whose supporting sources
are recorded here or in `docs/whitaker-morphology-findings.md`. New cases must
add both a focused test and documentation before receiving the flag.

Three additional lossless disagreements are currently encoded:

- `C.` retains both Whitaker's lexical abbreviation and the independently
  generated Roman-numeral reading. The numeral is flagged because the original
  `words`/`words_json` specialization suppresses it when the period is present;
  Napoleão Mendes de Almeida, *Gramática Latina: curso único e completo*,
  29th ed., printed page 200, explicitly records `C.` for *Caius*, *Cicero*,
  and *Calendae*.
- a finite plural analysis of a lexeme marked `impersonal`, such as the
  impersonal reading of `licent`, is retained and flagged. Almeida,
  *Gramática Latina*, §342 (printed page 318), describes finite impersonals as
  third singular; the original Whitaker checks person but not singular number.
- a future active participle combined with finite `sum`, such as
  `amaturus est`, retains Whitaker's synthetic passive voice and is flagged.
  Almeida, *Gramática Latina*, §285 (printed page 257), describes future
  participle plus `sum` as the active periphrastic construction.

These notices do not filter or relabel the candidates. They expose the
disagreement while keeping both the original classification and all generated
readings available to downstream consumers.

## Review policy

`manual-review-recommended` means the candidate should remain available to a
contextual or syntactic ranker. Downstream consumers may lower its score, but
should not silently convert the notice into “unknown” or “impossible.” A future
attestation component should add its own evidence instead of overwriting the
Whitaker compatibility assessment.

## Exact-form attestation and contextual ranking

Whitaker does not provide an exact-form concordance. Its lexical `SOURCE`
field identifies a principal dictionary used to derive an entry, `FREQ` is a
coarse lexical/rule frequency, and `DO_EXAMPLES` synthesizes an English
paraphrase from morphology. None of those fields proves that a queried Latin
surface with a particular analysis occurs in a cited passage.

Consequently exact attestation should follow the existing quantity-evidence
pipeline rather than become an `if` in `Engine`:

1. rich source identity, edition, locator, observed form, analysis, and review
   state stay in an editorial manifest or immutable corpus index;
2. a deterministic native compiler rejects conflicts and emits only reviewed
   summaries into a versioned compact WWDB section;
3. the immutable `Database` exposes typed lookup values and `Engine` annotates
   already-generated analyses after morphology, without deleting candidates;
4. CLI JSON and typed Embind project those values; the WASM graph never parses
   or serializes JSON;
5. a later contextual ranker consumes the complete candidate set and may score
   it, but does not rewrite `generatedByWhitaker`, trim compatibility, or
   attestation.

The minimum honest exact-attestation states are `not-checked`,
`exact-attested`, `related-only`, and `source-conflict`. “Not observed” is
valid only when accompanied by an explicit corpus/edition coverage set; it
must never be treated as “morphologically invalid.” An exact-form section will
need the actual normalized Latin surface (a legitimate pooled string), a
lexeme ID, a typed morphology identity, and compact status/source-family
flags. It should not be added until reviewed exact-form records exist.

## Orthographic provenance

Every rewrite step retains its rule ID and its concrete application:

- `category`: `classical`, `medieval`, or `syncope`;
- `scope`, `operation`, and scheduler `stage`;
- logical `position` and `removeCount`;
- the observed fragment and its replacement;
- the final `recognizedForm` in native analysis JSON v2.

This ordered path is the source of truth. Consumers should not reconstruct a
second, lossy list of alternative spellings from rule descriptions.
