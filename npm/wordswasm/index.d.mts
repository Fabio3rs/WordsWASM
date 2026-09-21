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

export interface CreateBundledWordsAnalysisEngineOptions {
  /** `full` includes meanings and supports analyze(); `search` omits meanings. */
  database?: "full" | "search";
  /** Optional assertion that the package manifest has this dataset identity. */
  datasetId?: string;
  moduleUrl?: string | URL;
  moduleFactory?: (options?: unknown) => Promise<unknown>;
  moduleOptions?: Record<string, unknown>;
  fetchImpl?: typeof fetch;
}

/**
 * Loads a packaged database and manifest. It reads local package files in
 * Node.js and fetches them in browsers, Workers, and bundlers.
 */
export function createBundledWordsAnalysisEngine(
  options?: CreateBundledWordsAnalysisEngineOptions,
): Promise<import("./words-engine.mjs").WordsAnalysisEngine>;
