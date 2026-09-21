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
[Versioning]: https://github.com/Fabio3rs/WordsWASM/blob/main/docs/versioning.md
[Licensing and notices]: https://github.com/Fabio3rs/WordsWASM/blob/main/THIRD_PARTY_NOTICES.md

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

## Defaults and options

The command uses its bundled full database and `analysis-v3` output by
default:

```sh
wordswasm "amo puellam"
```

Each query writes JSON to standard output. Select another database or output
format when needed:

```sh
wordswasm --database /path/to/words-search.wwdb --format search-v3 mālum
printf 'amo\npuella\n' | wordswasm --batch-json-lines
```

`analysis-v3` requires a full database. `search-v3` accepts either a full or
search-only database. They are the stable CLI JSON contracts from 1.0 onward.
The binary may accept older development selectors, but those are unsupported
migration paths; new integrations should use v3. Run `wordswasm --help` for
all CLI options and see [Versioning] for the compatibility policy.

## Provenance and license

WordsWASM code is MIT licensed. The bundled database derives from William
Whitaker's WORDS program and lexical data. This package includes
[`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md), with the WORDS attribution
and third-party license texts for the components in the native distribution.
