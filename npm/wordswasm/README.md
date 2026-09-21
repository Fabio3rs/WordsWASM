# wordswasm

`wordswasm` is the WebAssembly distribution of [WordsWASM], a Latin
morphological analysis engine derived from William Whitaker's WORDS source
data. It includes the engine, TypeScript declarations, the full and
search-only WWDB databases, and manifests. It does not install a native CLI.

[Demo] · [Releases] · [Issues] · [Documentation] · [Versioning] · [Licensing and notices]

[WordsWASM]: https://github.com/Fabio3rs/WordsWASM
[Demo]: https://fabio3rs.github.io/WordsWASM/
[Releases]: https://github.com/Fabio3rs/WordsWASM/releases
[Issues]: https://github.com/Fabio3rs/WordsWASM/issues
[Documentation]: https://github.com/Fabio3rs/WordsWASM#documentation
[Versioning]: https://github.com/Fabio3rs/WordsWASM/blob/main/docs/versioning.md
[Licensing and notices]: https://github.com/Fabio3rs/WordsWASM/blob/main/THIRD_PARTY_NOTICES.md

## Install

The current release line is a prerelease. Install its moving prerelease tag:

```sh
npm install wordswasm@next
```

When stable 1.0 is available, `npm install wordswasm` selects `latest`.
To make a deployment reproducible, replace `@next` with an exact version.
`next` remains the most recent prerelease after a stable publication.

## Use in a browser, worker, or bundler

```js
import {assets, createWordsAnalysisEngine} from "wordswasm";

const engine = await createWordsAnalysisEngine({
  databaseUrl: assets.fullDatabase,
});

console.log(engine.analyze("mālum"));
engine.dispose();
```

`assets` contains URLs resolved from this installed package:

| Export | Purpose |
| --- | --- |
| `assets.fullDatabase` | Full analysis database, including meanings. |
| `assets.searchDatabase` | Smaller search database without meanings. |
| `assets.manifest` | Release manifest, including `datasetId`. |
| `assets.datasetManifest` | Dataset-specific manifest. |
| `assets.wasm` | Generated WebAssembly module. |

Use the full database with `analyze()` and `analyzeLine()`. Use the search
database with `search()` and `searchLine()`. Pass the `datasetId` from the
manifest when your application stores or joins IDs across requests.

The stable browser contract is schema 5. It is independent of the npm version,
WWDB format, and `datasetId`; see [Versioning] before mixing downloaded data
with a package from another release.

## Use in Node.js

Node's `fetch` does not load `file:` URLs. Read the packaged database and pass
the bytes instead:

```js
import {readFile} from "node:fs/promises";
import {assets, createWordsAnalysisEngine} from "wordswasm";

const manifest = JSON.parse(await readFile(assets.manifest, "utf8"));
const engine = await createWordsAnalysisEngine({
  databaseBytes: await readFile(assets.fullDatabase),
  datasetId: manifest.datasetId,
});

console.log(engine.analyze("amo"));
engine.dispose();
```

## Provenance and license

WordsWASM code is MIT licensed. The engine and packaged databases derive from
William Whitaker's WORDS program and lexical data; the full attribution and
third-party license texts for WORDS, utf8proc/Unicode data, and
nlohmann/json are in [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md), which
is included in this package.

The default WebAssembly build uses a compact Unicode backend tailored to the
Latin alphabet accepted by WordsWASM. It validates UTF-8 strictly and supports
the package's ASCII, macron/breve, and `æ`/`œ` rules; it is not a general
Unicode normalizer and does not link utf8proc. The native CLI uses the full
utf8proc backend. Both implementations share the public lexer contract and
are differentially tested for the accepted Latin domain and rejection
behavior.
