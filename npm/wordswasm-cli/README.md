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

The wrapper uses its bundled full database and compact `analysis-v3` JSON by
default. JSON is the automation interface: results go to standard output and
diagnostics go to standard error.

```sh
wordswasm mālum
wordswasm --pretty "amo puellam"
wordswasm --version
wordswasm --help
```

`--pretty` is still valid JSON, only indented for terminal reading. It must not
be combined with batch mode: JSON Lines requires exactly one complete, compact
JSON value per output line.

### Batch / JSON Lines

Pass one query per input line to keep one database snapshot alive and emit one
result per line. This is suitable for shell pipelines and corpus jobs:

```sh
printf 'amo\npuella\n' | wordswasm --batch-json-lines > analyses.jsonl
# --batch is an alias for --batch-json-lines
```

Use `jq` or another JSONL-aware tool to consume the output. Blank input lines
are skipped. Do not pass a positional query together with batch mode.

### Database and output choices

```sh
wordswasm --db /path/to/words-search.wwdb --format search-v3 mālum
wordswasm -f search-v3 mālum
```

`--db` is an alias for `--database`, and `-f` is an alias for `--format`.
`analysis-v3` requires a full database; `search-v3` accepts a full or
search-only database. They are the stable CLI JSON contracts from 1.0 onward.
The binary may accept older development selectors, but those are unsupported
migration paths; new integrations should use v3. See [Versioning] for the
compatibility policy.

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
