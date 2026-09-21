# WordsWASM native platform package

This package contains the native `words_cli` executable and the full WordsWASM
database for one operating-system and CPU pair. It is an implementation detail
of [`wordswasm-cli`](https://www.npmjs.com/package/wordswasm-cli) and is
selected automatically through that package's optional dependencies.

Install the wrapper, including the prerelease tag while the 1.0 release line
is in prerelease:

```sh
npm install --global wordswasm-cli@next
wordswasm mālum
```

Do not add this package directly to an application unless you are building a
custom launcher. Its `bin/` directory has the platform executable and `data/`
has `words-full.wwdb`; the public CLI contract, defaults, and supported
options are documented by
[wordswasm-cli](https://www.npmjs.com/package/wordswasm-cli).
The release train, WWDB 1.10 baseline, and npm channel policy are documented
in the [versioning guide](https://github.com/Fabio3rs/WordsWASM/blob/main/docs/versioning.md).

WordsWASM is a C++ implementation whose data is derived from William
Whitaker's WORDS. `LICENSE` covers WordsWASM; `THIRD_PARTY_NOTICES.md` carries
the WORDS attribution and the notices for components included by the native
binary. Project links: [demo](https://fabio3rs.github.io/WordsWASM/),
[releases](https://github.com/Fabio3rs/WordsWASM/releases),
[issues](https://github.com/Fabio3rs/WordsWASM/issues), and
[documentation](https://github.com/Fabio3rs/WordsWASM#documentation).
