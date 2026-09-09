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

The default engine mode is `annotate`, which retains these candidates. The
explicit `filter` mode reproduces Whitaker's early removal.

## `audeo` passive

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

References:

- [Bennett, New Latin Grammar](https://www.gutenberg.org/cache/epub/15665/pg15665-images.html)
- [Allen & Greenough, Deponent Verbs](https://dcc.dickinson.edu/grammar/latin/deponent-verbs)
- [UK National Archives, deponent and semi-deponent verbs](https://www.nationalarchives.gov.uk/latin/stage-2-latin/lessons/lesson-22-deponent-and-semi-deponent-verbs/)
- [Lewis & Short, *audeo*](https://atlas.perseus.tufts.edu/dictionaries/entry/urn%3Acite2%3Ascaife-viewer%3Adictionary-entries.atlas_v1%3Alat.ls.perseus-eng2-n4450/)

## Source disagreement

`source-disagreement` is a curated flag, not a conclusion inferred from the
surface alone. It is emitted only for reviewed cases whose supporting sources
are recorded here or in `.study/WHITAKER_MORPHOLOGY_FINDINGS.md`. New cases
must add both a focused test and documentation before receiving the flag.

## Review policy

`manual-review-recommended` means the candidate should remain available to a
contextual or syntactic ranker. Downstream consumers may lower its score, but
should not silently convert the notice into “unknown” or “impossible.” A future
attestation component should add its own evidence instead of overwriting the
Whitaker compatibility assessment.

## Orthographic provenance

Every rewrite step retains its rule ID and its concrete application:

- `category`: `classical`, `medieval`, or `syncope`;
- `scope`, `operation`, and scheduler `stage`;
- logical `position` and `removeCount`;
- the observed fragment and its replacement;
- the final `recognizedForm` in native analysis JSON v2.

This ordered path is the source of truth. Consumers should not reconstruct a
second, lossy list of alternative spellings from rule descriptions.
