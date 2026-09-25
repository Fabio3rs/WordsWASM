# wordswasm-cli

`wordswasm-cli` is the native command-line distribution of [WordsWASM], a
Latin morphological analysis engine derived from William Whitaker's WORDS
source data. The wrapper installs the matching native platform package and its
bundled full WWDB database; it does not install the WebAssembly library.

[Demo] · [Releases] · [Issues] · [Documentation] · [Versioning] · [Licensing and notices]

[WordsWASM]: https://github.com/Fabio3rs/WordsWASM
[Demo]: https://fabio3rs.github.io/WordsWASM/
[Releases]: https://github.com/Fabio3rs/WordsWASM/releases
[Issues]: https://github.com/Fabio3rs/WordsWASM/issues
[Documentation]: https://github.com/Fabio3rs/WordsWASM#using-the-command-line-interface
[Versioning]: https://github.com/Fabio3rs/WordsWASM/blob/master/docs/versioning.md
[Licensing and notices]: https://github.com/Fabio3rs/WordsWASM/blob/master/THIRD_PARTY_NOTICES.md

## Install

The current release line is a prerelease:

```sh
npm install --global wordswasm-cli@next
wordswasm mālum
```

When a stable release is available, omit `@next`. The wrapper chooses exactly
one of the following optional platform packages for supported systems:

- Linux x64, ARM64, and ARM hard-float;
- macOS x64 and Apple silicon;
- Windows x64.

If installation reports that no matching optional package was installed,
download the appropriate standalone archive from [Releases] instead.

## Use

The wrapper uses its bundled full database and human-readable output by
default, including when stdout is redirected. Select `--format analysis-v4`
or `--format search-v4` for JSON automation with suffix quantity evidence.
Version 3 remains available for consumers of its existing document shape.
Results go to standard output and CLI errors to standard error.

```sh
wordswasm mālum
wordswasm --pretty "amo puellam"
wordswasm --format analysis-v4 amo
wordswasm --human-style compact amo | rg 'verb'
wordswasm --version
wordswasm --help
```

`--pretty` selects `analysis-v4` when `--format` is absent; it is still valid
JSON, only indented for terminal reading. A query
with multiple independent results is emitted as one JSON array. It is available
only for a single command-line query; stream input always emits compact JSONL.

### Human output and input streams

`--format human` prints one block per reading. Each block shows the dictionary
form, grammatical traits, a short gloss, the input and display forms, and the
quantity evidence for that reading (`none`, `partial`, or `complete`). The
display form may retain a macron or breve supplied by the user where the
database has no evidence; `partial` does not mean that every vowel is marked.
For example, the current bundled data displays `res` as `rēs` with complete
coverage, while different readings of `malum` show `mālum` (partial) and
`mālŭm` (complete). `puella` can display `pŭellă` or `pŭellā`, both partial.
`--detailed` shows the full dictionary meaning, expands editorial notes, and
shows the database-marked form when available. Deponent and semideponent
readings are labeled by verb class rather than the underlying passive-form
flag. A short warning marks readings Whitaker
would omit or that have an editorial note.
For a recognized verbal construction such as `amandus est`, the human view
shows the construction separately from each token's independent readings.
It labels the gerundive plus `sum` as a passive periphrastic construction;
the future active participle plus `sum` is labeled active periphrastic.
With `--two-words=legacy`, a possible split remains explicitly unconfirmed.
When its recorded derivation includes an enclitic, the human output shows the
base and enclitic separately, for example `res + publica + -que` for
`respublicaque`. Compact rows keep the original input and mark `enclitic -que`
in the affected readings' `note` column.

`--human-style compact` writes tab-separated rows with a single header:

```text
result  unit  reading  status  input  display  quantity_coverage  lemma  part  features  meaning  note
```

The actual separators are tabs. There is one row per reading, and one row with
`reading=0` if a result has no visible readings. The `unit` column distinguishes
`main`, `construction`, independent `token:N` readings, and unconfirmed
`suggestion:N` parts. Embedded
tabs, newlines, backslashes, and control characters are escaped within fields.
Both human styles are presentation formats; the TSV columns and their order may
change in minor releases. Use `analysis-v4` or `search-v4` for a stable machine
contract.
Compact output never contains color codes. In normal human output,
`--color=auto` uses color only when stdout is a terminal; `always` and `never`
override that choice. Redirected human output is plain text by default.

With no positional text, stdin is read as one query per line. `--input FILE`
selects a file, and `--input -` selects stdin explicitly:

```sh
printf 'amo\npuella\n' | wordswasm --format analysis-v4 > analyses.jsonl
wordswasm --input corpus.txt --human-style compact > readings.tsv
```

Each non-empty input line is tokenized completely. JSONL emits one compact
value per input line: an object for one result or an array for multiple
results. Human output uses readable blocks or tabular rows
for each result. Blank input lines are skipped.
Do not pass positional text together with `--input`. The legacy
`--batch-json-lines` and `--batch` options remain silent aliases for `--input -`;
the npm wrapper also selects `analysis-v4` for them when `--format` is omitted.

### Database and output choices

```sh
wordswasm --db /path/to/words-search.wwdb --format search-v4 mālum
wordswasm -f search-v4 mālum
```

`--db` is an alias for `--database`, and `-f` is an alias for `--format`.
`analysis-v3`, `analysis-v4`, and `human` require a full database;
`search-v3` and `search-v4` accept a full or search-only database. Only v4 is
part of the stable 1.x native JSON contract. V3 remains a deprecated
compatibility format and may be changed or removed without a major-version
bump. The binary may accept older development selectors, but those are
unsupported migration paths; new integrations should use v4. See [Versioning]
for the compatibility policy.

### Optional result filters

```sh
wordswasm --filter-trim=deponent-active-form rēs
wordswasm --filter-trim=semideponent-passive-present-system,semideponent-active-perfect-system audebantur
wordswasm --filter-trim=none rēs
```

No filtering is enabled by default. `--filter-trim` accepts the six existing
Whitaker trim reasons listed in `--help`, separated by commas, or `none`.
Specify the option once; duplicate reasons are ignored. Active filters require
`analysis-v3`, `search-v3`, `analysis-v4`, `search-v4`, or `human` and work with
JSONL, `--pretty`, and human output.

These filters select presentation results; they do not change generation or
assert that hidden forms are historically invalid. Notices do not override
selected filters. Status remains the engine's status; a newly emptied main
or token list receives an informational `all-analyses-filtered` diagnostic.
JSON output schemas remain unchanged.

### Errors and exit status

`wordswasm --help` works before the optional native package is resolved. If a
platform is unsupported, or installation omitted optional dependencies, the
wrapper explains how to install the matching package or use a standalone
release. For a command that reaches the native CLI, exit status `2` means an
invalid command, `3` means a database or engine failure, and `4` means an
unexpected failure. Status `1` is reserved for wrapper setup failures.


## Provenance and license

WordsWASM code is MIT licensed. The bundled database derives from William
Whitaker's WORDS program and lexical data. This package includes
[`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md), with the WORDS attribution
and third-party license texts for the components in the native distribution.
