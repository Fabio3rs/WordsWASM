const defaultModuleUrl = new URL("./words_wasm.mjs", import.meta.url);

function requireDatasetId(datasetId) {
  if (typeof datasetId !== "string" ||
      !/^sha256:[0-9a-f]{64}$/.test(datasetId)) {
    throw new TypeError(
      "datasetId must be sha256: followed by 64 lowercase hex digits",
    );
  }
}

function databaseView(bytes) {
  if (bytes instanceof Uint8Array) {
    return bytes;
  }
  if (bytes instanceof ArrayBuffer) {
    return new Uint8Array(bytes);
  }
  throw new TypeError("databaseBytes must be a Uint8Array or ArrayBuffer");
}

async function downloadDatabase(databaseUrl, fetchImpl) {
  if (databaseUrl === undefined) {
    throw new TypeError("databaseUrl or databaseBytes is required");
  }
  if (typeof fetchImpl !== "function") {
    throw new TypeError("fetch is unavailable; pass databaseBytes instead");
  }
  const response = await fetchImpl(databaseUrl);
  if (!response.ok) {
    throw new Error(
      `database download failed: HTTP ${response.status} ${response.statusText}`,
    );
  }
  return new Uint8Array(await response.arrayBuffer());
}

async function resolveModuleFactory(moduleFactory, moduleUrl) {
  if (moduleFactory !== undefined) {
    if (typeof moduleFactory !== "function") {
      throw new TypeError("moduleFactory must be a function");
    }
    return moduleFactory;
  }
  const exports = await import(moduleUrl.href);
  const factory = exports.default ?? exports.createWordsModule;
  if (typeof factory !== "function") {
    throw new TypeError("words_wasm.mjs does not export an Emscripten factory");
  }
  return factory;
}

function parseEngineJson(json, operation) {
  try {
    return JSON.parse(json);
  } catch (error) {
    throw new Error(`analysis engine returned invalid ${operation} JSON`, {
      cause: error,
    });
  }
}

/**
 * Creates one long-lived immutable Words analysis snapshot.
 *
 * Pass either databaseUrl or databaseBytes. moduleFactory and fetchImpl exist
 * primarily so hosts and tests can control loading without global state.
 */
export async function createWordsAnalysisEngine({
  datasetId,
  databaseUrl,
  databaseBytes,
  moduleUrl = defaultModuleUrl,
  moduleFactory,
  moduleOptions = {},
  fetchImpl = globalThis.fetch,
} = {}) {
  requireDatasetId(datasetId);
  const resolvedModuleUrl = new URL(moduleUrl, import.meta.url);
  const factory = await resolveModuleFactory(moduleFactory, resolvedModuleUrl);
  const locateFile = moduleOptions.locateFile ??
    ((path) => new URL(path, resolvedModuleUrl).href);
  const module = await factory({...moduleOptions, locateFile});
  if (typeof module.AnalysisEngine !== "function") {
    throw new TypeError("WebAssembly module has no AnalysisEngine binding");
  }

  const bytes = databaseBytes === undefined
    ? await downloadDatabase(databaseUrl, fetchImpl)
    : databaseView(databaseBytes);
  const native = new module.AnalysisEngine();
  try {
    const loaded = native.loadDatabase(bytes, datasetId);
    if (!loaded.ok) {
      throw new Error(`${loaded.code}: ${loaded.message}`);
    }
    const databaseKind = native.databaseKind();

    let disposed = false;
    const requireLive = () => {
      if (disposed) {
        throw new Error("analysis engine has been disposed");
      }
    };
    const run = (operation, text, options) => {
      requireLive();
      if (typeof text !== "string") {
        throw new TypeError("Latin input must be a string");
      }
      if (operation === "analyze" && databaseKind !== "full") {
        throw new Error("analysis requires words-full.wwdb");
      }
      const twoWords = options?.twoWords === true;
      return parseEngineJson(native[operation](text, twoWords), operation);
    };

    return Object.freeze({
      datasetId: native.datasetId(),
      databaseBytes: native.databaseBytes(),
      databaseKind,
      analyze(text, options) {
        return run("analyze", text, options);
      },
      search(text, options) {
        return run("search", text, options);
      },
      dispose() {
        if (!disposed) {
          disposed = true;
          native.delete();
        }
      },
    });
  } catch (error) {
    native.delete();
    throw error;
  }
}
