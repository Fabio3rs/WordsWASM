# wordswasm

WebAssembly distribution of the WordsWASM Latin morphological analysis engine.

```js
import {assets, createWordsAnalysisEngine} from "wordswasm";

const engine = await createWordsAnalysisEngine({
  databaseUrl: assets.fullDatabase,
});
console.log(engine.analyze("mālum"));
engine.dispose();
```

In Node.js, pass `databaseBytes: await readFile(assets.fullDatabase)` rather
than using `databaseUrl`; Node's `fetch` does not load `file:` URLs.
