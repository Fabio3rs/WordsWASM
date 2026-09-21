export * from "./words-engine.mjs";

export interface WordsWasmAssets {
  manifest: URL;
  datasetManifest: URL;
  fullDatabase: URL;
  searchDatabase: URL;
  module: URL;
  wasm: URL;
}

/** URLs for files shipped with this package. */
export const assets: Readonly<WordsWasmAssets>;
