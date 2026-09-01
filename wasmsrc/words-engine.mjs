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

function deleteHandle(value) {
  if (!Array.isArray(value) && typeof value?.delete === "function") {
    value.delete();
  }
}

function copyVector(values, map = (value) => value) {
  if (Array.isArray(values)) {
    return values.map(map);
  }
  const output = [];
  for (let index = 0; index < values.size(); ++index) {
    output.push(map(values.get(index)));
  }
  return output;
}

function copyOwnedVector(values, map = (value) => value) {
  try {
    return copyVector(values, map);
  } finally {
    deleteHandle(values);
  }
}

const nullableString = (value) => value === "" ? null : value;
const nullableNumber = (value) => value === 0 ? null : value;

function copyMorphology(raw) {
  const nominal = () => ({
    declension: nullableNumber(raw.declension),
    variant: nullableNumber(raw.variant),
    case: nullableString(raw.case),
    number: nullableString(raw.number),
    gender: nullableString(raw.gender),
  });
  switch (raw.kind) {
    case "noun":
    case "pronoun":
      return {kind: raw.kind, ...nominal()};
    case "adjective":
      return {kind: raw.kind, ...nominal(), degree: nullableString(raw.degree)};
    case "numeral":
      return {
        kind: raw.kind,
        ...nominal(),
        numeralType: nullableString(raw.numeralType),
      };
    case "adverb":
      return {kind: raw.kind, degree: nullableString(raw.degree)};
    case "verb":
      return {
        kind: raw.kind,
        conjugation: nullableNumber(raw.conjugation),
        variant: nullableNumber(raw.variant),
        tense: nullableString(raw.tense),
        voice: nullableString(raw.voice),
        mood: nullableString(raw.mood),
        person: nullableNumber(raw.person),
        number: nullableString(raw.number),
      };
    case "participle":
      return {
        kind: raw.kind,
        conjugation: nullableNumber(raw.conjugation),
        variant: nullableNumber(raw.variant),
        case: nullableString(raw.case),
        number: nullableString(raw.number),
        gender: nullableString(raw.gender),
        tense: nullableString(raw.tense),
        voice: nullableString(raw.voice),
      };
    case "supine":
      return {
        kind: raw.kind,
        conjugation: nullableNumber(raw.conjugation),
        variant: nullableNumber(raw.variant),
        case: nullableString(raw.case),
        number: nullableString(raw.number),
        gender: nullableString(raw.gender),
      };
    case "preposition":
      return {kind: raw.kind, governs: nullableString(raw.governs)};
    case "conjunction":
    case "interjection":
      return {kind: raw.kind};
    default:
      throw new TypeError(`unsupported morphology kind: ${raw.kind}`);
  }
}

function copyLexical(raw) {
  const output = {
    dictionary: raw.dictionary,
    entryId: raw.entryId,
    partOfSpeech: raw.partOfSpeech,
    age: nullableString(raw.age),
    subject: nullableString(raw.subject),
    geography: nullableString(raw.geography),
    frequency: nullableString(raw.frequency),
    source: nullableString(raw.source),
  };
  switch (raw.partOfSpeech) {
    case "noun":
      return Object.assign(output, {
        declension: nullableNumber(raw.declension),
        variant: nullableNumber(raw.variant),
        gender: nullableString(raw.gender),
        nounKind: nullableString(raw.nounKind),
      });
    case "pronoun":
      return Object.assign(output, {
        declension: nullableNumber(raw.declension),
        variant: nullableNumber(raw.variant),
        pronounKind: nullableString(raw.pronounKind),
        requiredPackonId: raw.hasRequiredPackon
          ? raw.requiredPackonId
          : null,
      });
    case "adjective":
      return Object.assign(output, {
        declension: nullableNumber(raw.declension),
        variant: nullableNumber(raw.variant),
        degree: nullableString(raw.degree),
      });
    case "numeral":
      return Object.assign(output, {
        declension: nullableNumber(raw.declension),
        variant: nullableNumber(raw.variant),
        numeralType: nullableString(raw.numeralType),
        numeralValue: nullableNumber(raw.numeralValue),
      });
    case "adverb":
      return Object.assign(output, {degree: nullableString(raw.degree)});
    case "verb":
      return Object.assign(output, {
        conjugation: nullableNumber(raw.conjugation),
        variant: nullableNumber(raw.variant),
        verbKind: nullableString(raw.verbKind),
      });
    case "preposition":
      return Object.assign(output, {governs: nullableString(raw.governs)});
    default:
      return output;
  }
}

function copyForm(raw) {
  return {
    stem: raw.stem,
    stemKey: raw.hasStemKey ? raw.stemKey : null,
    ending: raw.ending,
    recognized: raw.recognized,
  };
}

function copyDerivation(raw) {
  return {
    method: raw.method,
    steps: copyOwnedVector(raw.steps, (step) => {
      const output = {
        kind: step.kind,
        target: step.target,
        id: step.id,
        type: step.type,
      };
      if (step.kind === "addon") {
        output.text = step.text;
        output.enclitic = step.enclitic;
      } else {
        output.rule = step.rule;
        if (step.before !== "") output.before = step.before;
        if (step.after !== "") output.after = step.after;
      }
      if (step.hasMeaning) output.meaning = step.meaning;
      return output;
    }),
  };
}

function copyHit(raw) {
  const hit = {
    kind: raw.kind,
    partOfSpeech: raw.partOfSpeech,
    form: copyForm(raw.form),
    morphology: copyMorphology(raw.morphology),
    derivation: copyDerivation(raw.derivation),
  };
  if (raw.kind === "artificial") {
    hit.artificial = {
      method: raw.artificialMethod,
      value: raw.artificialValue,
      wellFormed: raw.artificialWellFormed,
    };
    return hit;
  }

  hit.lexemeId = raw.lexemeId;
  hit.lemma = raw.lemma;
  hit.lexical = copyLexical(raw.lexical);
  hit.rule = raw.rule.present ? {
    id: raw.ruleId,
    age: nullableString(raw.rule.age),
    frequency: nullableString(raw.rule.frequency),
  } : null;
  if (raw.hasMeaning) hit.meaning = raw.meaning;
  if (raw.kind === "lexical") {
    hit.quantityMatch = raw.quantityMatch;
  } else if (raw.kind === "compound") {
    hit.compound = {
      construction: raw.compoundConstruction,
      auxiliary: raw.compoundAuxiliary,
      sourceTense: nullableString(raw.compoundSourceTense),
      sourceVoice: nullableString(raw.compoundSourceVoice),
    };
  }
  return hit;
}

function copySegment(segment) {
  return {
    text: segment.text,
    hits: copyOwnedVector(segment.hits, copyHit),
  };
}

function copySuggestion(suggestion) {
  return {
    method: suggestion.method,
    splitAt: suggestion.splitAt,
    classification: suggestion.classification,
    segments: copyOwnedVector(suggestion.segments, copySegment),
  };
}

function copyResult(raw) {
  const handles = [];
  try {
    const hits = raw.hits;
    handles.push(hits);
    const diagnostics = raw.diagnostics;
    handles.push(diagnostics);
    const suggestions = raw.suggestions;
    handles.push(suggestions);
    return {
      schema: raw.schema,
      schemaVersion: raw.schemaVersion,
      datasetId: raw.datasetId,
      query: raw.query,
      status: raw.status,
      hits: copyVector(hits, copyHit),
      diagnostics: copyVector(diagnostics, (diagnostic) => ({
        code: diagnostic.code,
        severity: diagnostic.severity,
        parameters: diagnostic.partOfSpeech === ""
          ? {}
          : {partOfSpeech: diagnostic.partOfSpeech},
      })),
      suggestions: copyVector(suggestions, copySuggestion),
    };
  } finally {
    for (const handle of handles) deleteHandle(handle);
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
  const hasDatabaseBytes = databaseBytes !== undefined;
  const hasDatabaseUrl = databaseUrl !== undefined;
  if (hasDatabaseBytes === hasDatabaseUrl) {
    throw new TypeError(
      "pass exactly one of databaseUrl or databaseBytes",
    );
  }
  const bytesPromise = hasDatabaseBytes
    ? Promise.resolve(databaseView(databaseBytes))
    : downloadDatabase(databaseUrl, fetchImpl);
  const resolvedModuleUrl = new URL(moduleUrl, import.meta.url);
  const locateFile = moduleOptions.locateFile ??
    ((path) => new URL(path, resolvedModuleUrl).href);
  const modulePromise = resolveModuleFactory(moduleFactory, resolvedModuleUrl)
    .then((factory) => factory({...moduleOptions, locateFile}));
  const [module, bytes] = await Promise.all([modulePromise, bytesPromise]);
  if (typeof module.AnalysisEngine !== "function") {
    throw new TypeError("WebAssembly module has no AnalysisEngine binding");
  }
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
      // WHY: Embind transports typed value objects.  Only the CLI owns JSON
      // presentation; browser callers receive normal JavaScript structures.
      return copyResult(native[operation](text, twoWords));
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
