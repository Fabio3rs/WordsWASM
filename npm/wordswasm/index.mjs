import {createWordsAnalysisEngine} from "./words-engine.mjs";

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

async function readBundledAsset(assetUrl, fetchImpl) {
  if (assetUrl.protocol === "file:" &&
      typeof process !== "undefined" &&
      typeof process.versions?.node === "string") {
    const {readFile} = await import("node:fs/promises");
    return new Uint8Array(await readFile(assetUrl));
  }
  if (typeof fetchImpl !== "function") {
    throw new TypeError("fetch is unavailable for bundled package assets");
  }
  const response = await fetchImpl(assetUrl);
  if (!response.ok) {
    throw new Error(
      `bundled asset download failed: HTTP ${response.status} ${response.statusText}`,
    );
  }
  return new Uint8Array(await response.arrayBuffer());
}

/**
 * Creates an engine using one of this package's databases.
 *
 * The helper reads package files with Node's file system and downloads them
 * with fetch in browsers, Workers, and bundlers.  Use
 * createWordsAnalysisEngine() directly only when the database lives elsewhere
 * or is already available as bytes.
 */
export async function createBundledWordsAnalysisEngine({
  database = "full",
  datasetId,
  moduleUrl,
  moduleFactory,
  moduleOptions,
  fetchImpl = globalThis.fetch,
} = {}) {
  if (database !== "full" && database !== "search") {
    throw new TypeError('database must be "full" or "search"');
  }
  const databaseAsset = database === "full"
    ? assets.fullDatabase
    : assets.searchDatabase;
  const [databaseBytes, manifestBytes] = await Promise.all([
    readBundledAsset(databaseAsset, fetchImpl),
    readBundledAsset(assets.manifest, fetchImpl),
  ]);
  const manifest = JSON.parse(new TextDecoder().decode(manifestBytes));
  if (datasetId !== undefined && datasetId !== manifest.datasetId) {
    throw new Error("datasetId does not match this package's manifest");
  }
  return createWordsAnalysisEngine({
    datasetId: manifest.datasetId,
    databaseBytes,
    moduleUrl,
    moduleFactory,
    moduleOptions,
    fetchImpl,
  });
}
