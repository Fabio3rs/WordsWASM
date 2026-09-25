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
[Versioning]: https://github.com/Fabio3rs/WordsWASM/blob/master/docs/versioning.md
[Licensing and notices]: https://github.com/Fabio3rs/WordsWASM/blob/master/THIRD_PARTY_NOTICES.md

## Install

The current release line is a prerelease. Install its moving prerelease tag:

```sh
npm install wordswasm@next
```

When stable 1.0 is available, `npm install wordswasm` selects `latest`.
To make a deployment reproducible, replace `@next` with an exact version.
`next` remains the most recent prerelease after a stable publication.

## Quick start: browser, Worker, bundler, or Node.js

```js
import {createBundledWordsAnalysisEngine} from "wordswasm";

const engine = await createBundledWordsAnalysisEngine();

console.log(engine.analyze("mālum"));
engine.dispose();
```

`createBundledWordsAnalysisEngine()` loads the full database and its matching
manifest. In Node.js it reads package files; in browsers, Workers, and bundler
output it fetches package URLs. It therefore avoids the `file:`-URL difference
between Node's `fetch` and browser `fetch`.

Pass `database: "search"` for the smaller no-meanings database, then use
`search()` or `searchLine()`:

```js
const engine = await createBundledWordsAnalysisEngine({database: "search"});
console.log(engine.search("amo"));
engine.dispose();
```

## Optional result filters

All four query methods accept client-side filters:

```js
engine.analyze("rēs", {
  filters: {excludeWhitakerTrimReasons: ["deponent-active-form"]}
});
```

The list uses the exported `WhitakerTrimReason` type. Omit `filters`, use `{}`,
or pass an empty list to keep all results. Any selected reason removes the
matching analysis, including analyses with notices; this is a presentation
choice, not a judgment that a form is impossible. The engine and database are
unchanged, and filtering does not persist between calls.

The filter also covers tokens and suggestions. Status retains the engine's
value; a newly emptied main or token list receives `all-analyses-filtered` in
`diagnostics`. Result schemas are unchanged. See the
[morphological assessment documentation](https://github.com/Fabio3rs/WordsWASM/blob/master/docs/morphological-assessment.md#optional-client-filters)
for the six reason names and full behavior.

## Advanced loading and assets

Use `createWordsAnalysisEngine()` when a host stores the database elsewhere,
already has its bytes, or needs to control the Emscripten module factory. Pass
**exactly one** of `databaseUrl` and `databaseBytes`.

```js
import {createWordsAnalysisEngine} from "wordswasm";

const engine = await createWordsAnalysisEngine({
  databaseUrl: new URL("/data/words-full.wwdb", location.origin),
  datasetId: "sha256:...", // Copy this from the matching manifest.
});
```

`assets` contains URLs resolved from this installed package. It is useful when
you need to serve, cache, or preload data yourself:

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

The six exact asset subpaths below are public; no other `dist` path is part of
the package API. They can be used by tools that accept package asset imports:

```text
wordswasm/assets/manifest.json
wordswasm/assets/dataset-manifest.json
wordswasm/assets/words-full.wwdb
wordswasm/assets/words-search.wwdb
wordswasm/assets/words_wasm.mjs
wordswasm/assets/words_wasm.wasm
```

The stable 1.0 browser contract is schema 6. It is independent of the npm version,
WWDB format, and `datasetId`; see [Versioning] before mixing downloaded data
with a package from another release.

## ESM, bundlers, and Workers

This is an ESM-only package. Use `import`, not `require`. The quick-start API
works in modern bundlers because it resolves every URL from the installed
package with `import.meta.url`; do not copy the files to another directory
unless you use the advanced API and provide their new URL or bytes.

Workers use the same ESM import and quick-start API. Ensure the Worker is
served as a module and that its server permits fetching the emitted `.wasm`,
`.wwdb`, and `.json` assets. The package does not use DOM APIs.

For Node.js, version 20 or newer is supported. The quick start reads packaged
files directly. If an application has already read the database, avoid a
second read with the advanced API:

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
