export * from "./words-engine.mjs";

/** URLs for files shipped with this package. */
export const assets = Object.freeze({
  manifest: new URL("./manifest.json", import.meta.url),
  datasetManifest: new URL("./dataset-manifest.json", import.meta.url),
  fullDatabase: new URL("./words-full.wwdb", import.meta.url),
  searchDatabase: new URL("./words-search.wwdb", import.meta.url),
  module: new URL("./words_wasm.mjs", import.meta.url),
  wasm: new URL("./words_wasm.wasm", import.meta.url),
});
