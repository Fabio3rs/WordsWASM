# Versioning and compatibility

This policy takes effect with the stable `v1.0.0` release. Earlier tags,
including `v1.0.0-rc*`, were development checkpoints. They document how the
project evolved, but do not establish compatibility commitments for the 1.x
release line.

WordsWASM has several independent version and identity systems. Do not infer
the meaning of one from the value of another.

| Identifier | What it identifies | Stable 1.0 baseline |
| --- | --- | --- |
| GitHub release and npm package version | A distributable release train. | `1.0.0` |
| Native CLI JSON schema | A selected JSON output contract. | `analysis-v3`, `search-v3`, `analysis-v4`, `search-v4` |
| Browser schema | Documents returned by the high-level WebAssembly API. | 6 |
| WWDB format | The binary database wire format. | 1.11 |
| `datasetId` | The logical data snapshot and its ID space. | A `sha256:` identity in the release manifest |

## Package versions

WordsWASM uses [Semantic Versioning](https://semver.org/) from stable 1.0
onward. One release tag `vX.Y.Z[-prerelease]` builds a release train: the
`wordswasm` library, `wordswasm-cli` wrapper, and all six native platform
packages receive exactly `X.Y.Z[-prerelease]`. The wrapper pins each platform
package to that exact version, so it always selects a binary and bundled
database from the same release train.

| Change | Package version |
| --- | --- |
| Documentation, build, security, or defect correction that leaves public APIs, schemas, and `datasetId` unchanged | Patch |
| Backward-compatible public capability, or a data update that changes `datasetId` | Minor |
| Removed or incompatible public API; changed stable CLI default; incompatible browser schema; or incompatible WWDB format | Major |

The contents of an exact npm version are immutable. Applications should pin an
exact version whenever reproducibility matters.

## npm channels and GitHub Releases

`latest` is the stable channel. `next` is the moving prerelease channel:

| GitHub Release | npm dist-tag | Installation |
| --- | --- | --- |
| Stable `vX.Y.Z` | `latest` | `npm install wordswasm` |
| Prerelease `vX.Y.Z-...` | `next` | `npm install wordswasm@next` |

The **Set as a pre-release** checkbox in the GitHub Release UI must agree with
the SemVer tag: a tag containing a prerelease suffix must be marked as a
prerelease, and a stable tag must not be. The build rejects a mismatch.

Publishing a stable release moves `latest`; it does not move `next`. Until a
new prerelease is published, `next` continues to name the most recently
published prerelease. Prerelease versions are not stable compatibility
commitments and should be pinned exactly when used outside evaluation.

## Stable API and data contracts

### Native C++ API stability

The native C++ interfaces exposed by `words_core`, including its headers and
libraries, are experimental. WordsWASM 1.x does not guarantee source or binary
compatibility for them; C++ interfaces may change between minor releases.

The stable 1.x compatibility commitments in this document apply to the
documented CLI contracts, versioned JSON schemas, JavaScript/WebAssembly API,
WWDB format, and release/package interfaces. A stable C++ source API may be
defined separately in a future release. Binary ABI compatibility is not
currently guaranteed.

### Native CLI JSON

The stable JSON contracts are selected explicitly:

| Selector | `schema` | `schemaVersion` | Database |
| --- | --- | ---: | --- |
| `analysis-v3` | `whitakers-words.analysis` | 3 | Full WWDB |
| `search-v3` | `whitakers-words.search` | 3 | Full or search-only WWDB |
| `analysis-v4` | `whitakers-words.analysis` | 4 | Full WWDB |
| `search-v4` | `whitakers-words.search` | 4 | Full or search-only WWDB |

`wordswasm-cli` supplies its bundled full database and selects `human`
when `--format` is omitted. `--pretty` without an explicit format selects
`analysis-v4`. Scripts needing JSON must select an explicit versioned format;
use `analysis-v4` or `search-v4` for suffix quantity evidence. The standalone
native binary requires an explicit `--database` and `--format`.

The binary currently also accepts the unversioned `analysis` and `search`
selectors (schema 1) and the v2 selectors. They are retained as an unsupported
migration path, are not part of the stable 1.x API, and may be removed in a
minor release. New integrations should select v4 when they need suffix
quantity evidence; v3 keeps its existing document shape. The v1 and v2
implementation and schema records, plus historical examples, remain in the
repository as development records rather than a promise to preserve all
prerelease interfaces.

`human` and `human --human-style compact` are presentation formats, not stable
API or TSV schemas. Their wording, ordering, and compact column layout may
change in minor releases. Scripts that need a stable contract should select
an explicit versioned format.

### Browser and WebAssembly

`createWordsAnalysisEngine()` and its TypeScript declarations return browser
documents with schema 6: `whitakers-words.browser-analysis` for `analyze()`
and `whitakers-words.browser-search` for `search()`. Schema 6 is the browser
contract for 1.x. Earlier browser schemas are historical files; the API has no
runtime switch that emits them.

The current browser schemas reject unknown properties. A changed document
shape therefore requires a new browser schema version and a package major
release. Compatible additions belong in a new API surface rather than silently
changing schema 6.

### WWDB and datasets

The stable 1.0 distribution is planned to publish WWDB 1.11 in `dense` (full)
and `search-only` profiles. Version 1.11 adds sparse addon rule attributes for
quantity, source paradigm, and coexistence policy. It succeeds the 1.10
development format; 1.10 remains a historical format, not the 1.0 baseline.
A future incompatible wire format requires a WordsWASM package major release.

In native v4 and browser v6 projections, `form.display` shows only
database-supported quantity; `form.recognized` retains marks supplied in the
query. Native v3 keeps its prior `display` behavior.

**Breaking change from WWDB 1.10 to 1.11:** the engine's adverbial `-ē`
analysis depends on the source-paradigm and vowel-quantity attributes added in
WWDB 1.11. WWDB 1.10 lacks those attributes. The development loader can read
older images for inspection, but `Engine::create` rejects them with
`unsupported-version`. Distribute the matching engine and WWDB together; do
not replace only one file in an existing deployment. This incompatible change
occurred before stable 1.0, so it does not break a stable 1.x promise.

Use the full and search-only files from one release together. They must have
the same WWDB format and `datasetId`; a full database is required for
`analyze()` and `analysis-v3`/`analysis-v4`, while a search-only database
supports only `search()` and `search-v3`/`search-v4`.

`datasetId` is not a package version and is not the hash of one `.wwdb` file.
The exporter hashes a canonical dataset manifest containing the WWDB format,
canonical source digests, and the packer source. It changes whenever that
logical provenance changes, including a curated data update, a relevant packer
change, or an ID-space change. Physical asset hashes in `manifest.json` verify
individual `.wwdb`, JavaScript, WASM, and compressed files.

Lexeme and rule IDs are local to a `datasetId`. Consumers may join IDs between
the full and search projections only when their `datasetId` strings match.
Omit `datasetId` only for anonymous local or test datasets where that
provenance guard is intentionally not needed.

## Optional presentation filters

The CLI `--filter-trim` option and JS/WASM `AnalyzeOptions.filters` are optional
input capabilities. They preserve default results and the existing output
schemas; the informational `all-analyses-filtered` code uses the existing
open diagnostic vocabulary. No new output field or WWDB format is introduced.
Changing the stable no-filter default is not part of this addition and must
follow the compatibility policy above.

## Historical documents

Research reports and audit notes retain the schema and WWDB values observed at
the time of their sessions. Those references are historical evidence, not the
stable compatibility policy. This document and the public package READMEs are
the source of truth for released interfaces.
