import {createHash} from "node:crypto";
import {mkdir, readFile, writeFile} from "node:fs/promises";
import {Session} from "node:inspector/promises";
import {dirname, resolve} from "node:path";
import {pathToFileURL} from "node:url";

const defaults = Object.freeze({
  mode: "end-to-end",
  profiler: "inspector",
  module: "build/wasm-profile/words_wasm.mjs",
  wrapper: "build/wasm-profile/words-engine.mjs",
  database:
    "whitakers-words/poc/compact-db/output/words-poc-dense.wwdb",
  corpus: "whitakers-words/test/01_aeneid/input.txt",
  output: null,
  warmup: 2,
  iterations: 5,
  interval: 250,
  twoWords: false,
  measureHeap: false,
});

function usage() {
  console.error(`usage: node scripts/profile-wasm.mjs [options]

  --mode end-to-end|core   public JS wrapper or C++-only Wasm loop
  --profiler inspector|none
  --module FILE            generated words_wasm.mjs
  --wrapper FILE           copied words-engine.mjs
  --database FILE          full WWDB image
  --corpus FILE            UTF-8 corpus passed to analyzeLine
  --output FILE            .cpuprofile destination
  --warmup N               iterations before profiling
  --iterations N           iterations inside the measured region
  --interval MICROSECONDS  V8 sampling interval
  --two-words              enable legacy two-word recovery
  --measure-heap           sample the C++ allocator; requires core/none
  --help`);
}

function positiveInteger(text, option, allowZero = false) {
  const value = Number(text);
  if (!Number.isSafeInteger(value) || value < (allowZero ? 0 : 1)) {
    const requirement = allowZero ? "non-negative" : "positive";
    throw new TypeError(`${option} must be ${requirement}`);
  }
  return value;
}

function parseArguments(arguments_) {
  const options = {...defaults};
  for (let index = 0; index < arguments_.length; ++index) {
    const argument = arguments_[index];
    const value = () => {
      if (++index >= arguments_.length) {
        throw new TypeError(`missing value for ${argument}`);
      }
      return arguments_[index];
    };
    switch (argument) {
      case "--mode": options.mode = value(); break;
      case "--profiler": options.profiler = value(); break;
      case "--module": options.module = value(); break;
      case "--wrapper": options.wrapper = value(); break;
      case "--database": options.database = value(); break;
      case "--corpus": options.corpus = value(); break;
      case "--output": options.output = value(); break;
      case "--warmup":
        options.warmup = positiveInteger(value(), argument, true);
        break;
      case "--iterations":
        options.iterations = positiveInteger(value(), argument);
        break;
      case "--interval":
        options.interval = positiveInteger(value(), argument);
        break;
      case "--two-words": options.twoWords = true; break;
      case "--measure-heap": options.measureHeap = true; break;
      case "--help": usage(); process.exit(0);
      default: throw new TypeError(`unknown option: ${argument}`);
    }
  }
  if (!new Set(["end-to-end", "core"]).has(options.mode)) {
    throw new TypeError("--mode must be end-to-end or core");
  }
  if (!new Set(["inspector", "none"]).has(options.profiler)) {
    throw new TypeError("--profiler must be inspector or none");
  }
  if (options.measureHeap &&
      (options.mode !== "core" || options.profiler !== "none")) {
    throw new TypeError(
      "--measure-heap requires --mode core --profiler none",
    );
  }
  options.output ??=
    `build/wasm-profile/profiles/words-wasm-${options.mode}.cpuprofile`;
  return options;
}

function addCounts(left, right) {
  const result = {
    units: left.units + right.units,
    tokens: left.tokens + right.tokens,
    analyses: left.analyses + right.analyses,
    checksum: left.checksum + right.checksum,
  };
  if (!Object.values(result).every(Number.isSafeInteger)) {
    throw new RangeError("benchmark count exceeds JavaScript safe integer");
  }
  return result;
}

function countResults(results) {
  let analyses = 0;
  let tokens = 0;
  for (const result of results) {
    analyses += result.hits.length;
    const independentTokens = result.tokens ?? [];
    tokens += independentTokens.length;
    for (const token of independentTokens) analyses += token.hits.length;
  }
  return {
    units: results.length,
    tokens,
    analyses,
    checksum: results.length + tokens + analyses,
  };
}

function runEndToEnd(engine, corpus, iterations, twoWords) {
  let total = {units: 0, tokens: 0, analyses: 0, checksum: 0};
  for (let iteration = 0; iteration < iterations; ++iteration) {
    total = addCounts(
      total,
      countResults(engine.analyzeLine(corpus, {twoWords})),
    );
  }
  return total;
}

function runCore(engine, corpus, iterations, twoWords, measureHeap) {
  const operation = measureHeap
    ? engine.benchmarkCorpusMemory
    : engine.benchmarkCorpus;
  return operation(corpus, iterations, twoWords);
}

function assertDeterministic(warmup, measured, warmupIterations,
                             measuredIterations) {
  if (warmupIterations === 0) return;
  const warmupPerIteration = warmup.checksum / warmupIterations;
  const measuredPerIteration = measured.checksum / measuredIterations;
  if (!Number.isSafeInteger(warmupPerIteration) ||
      warmupPerIteration !== measuredPerIteration) {
    throw new Error(
      `checksum changed after warmup: ${warmupPerIteration} != ` +
      `${measuredPerIteration}`,
    );
  }
}

async function createEndToEnd(options, databaseBytes, datasetId) {
  const wrapperUrl = pathToFileURL(resolve(options.wrapper));
  const moduleUrl = pathToFileURL(resolve(options.module));
  const wrapper = await import(wrapperUrl.href);
  return wrapper.createWordsAnalysisEngine({
    datasetId,
    databaseBytes,
    moduleUrl,
  });
}

async function createCore(options, databaseBytes, datasetId) {
  const moduleUrl = pathToFileURL(resolve(options.module));
  const exports = await import(moduleUrl.href);
  const factory = exports.default ?? exports.createWordsModule;
  if (typeof factory !== "function") {
    throw new TypeError("Wasm module does not export an Emscripten factory");
  }
  const module = await factory({
    locateFile: (name) => new URL(name, moduleUrl).href,
  });
  const engine = new module.AnalysisEngine();
  const loaded = engine.loadDatabase(databaseBytes, datasetId);
  if (!loaded.ok) {
    engine.delete();
    throw new Error(`${loaded.code}: ${loaded.message}`);
  }
  if (typeof engine.benchmarkCorpus !== "function") {
    engine.delete();
    throw new Error(
      "benchmarkCorpus is absent; configure with WORDS_WASM_PROFILING=ON",
    );
  }
  if (options.measureHeap &&
      typeof engine.benchmarkCorpusMemory !== "function") {
    engine.delete();
    throw new Error("benchmarkCorpusMemory is absent from the Wasm module");
  }
  return Object.freeze({
    benchmarkCorpus: (corpus, iterations, twoWords) =>
      engine.benchmarkCorpus(corpus, iterations, twoWords),
    benchmarkCorpusMemory: (corpus, iterations, twoWords) =>
      engine.benchmarkCorpusMemory(corpus, iterations, twoWords),
    dispose: () => engine.delete(),
  });
}

async function main() {
  const options = parseArguments(process.argv.slice(2));
  const modulePath = resolve(options.module);
  const wasmPath = modulePath.endsWith(".mjs")
    ? `${modulePath.slice(0, -4)}.wasm`
    : `${modulePath}.wasm`;
  const [databaseBuffer, corpus, glueBuffer, wasmBuffer] = await Promise.all([
    readFile(resolve(options.database)),
    readFile(resolve(options.corpus), "utf8"),
    readFile(modulePath),
    readFile(wasmPath),
  ]);
  const databaseBytes = new Uint8Array(databaseBuffer);
  const databaseHash = createHash("sha256")
    .update(databaseBuffer)
    .digest("hex");
  const datasetId = `sha256:${databaseHash}`;
  const engine = options.mode === "core"
    ? await createCore(options, databaseBytes, datasetId)
    : await createEndToEnd(options, databaseBytes, datasetId);
  const run = options.mode === "core"
    ? (iterations) => runCore(
      engine, corpus, iterations, options.twoWords, options.measureHeap,
    )
    : (iterations) =>
      runEndToEnd(engine, corpus, iterations, options.twoWords);

  let session;
  try {
    const warmup = options.warmup === 0
      ? {units: 0, tokens: 0, analyses: 0, checksum: 0}
      : run(options.warmup);

    if (options.profiler === "inspector") {
      session = new Session();
      session.connect();
      await session.post("Profiler.enable");
      await session.post("Profiler.setSamplingInterval", {
        interval: options.interval,
      });
      await session.post("Profiler.start");
    }

    const started = process.hrtime.bigint();
    const measured = run(options.iterations);
    const elapsedNs = process.hrtime.bigint() - started;

    let profile;
    if (session !== undefined) {
      ({profile} = await session.post("Profiler.stop"));
    }
    assertDeterministic(
      warmup, measured, options.warmup, options.iterations,
    );

    const outputPath = resolve(options.output);
    if (profile !== undefined) {
      await mkdir(dirname(outputPath), {recursive: true});
      await writeFile(outputPath, JSON.stringify(profile));
    }
    const summary = {
      mode: options.mode,
      profiler: options.profiler,
      samplingIntervalUs: options.profiler === "inspector"
        ? options.interval
        : null,
      warmupIterations: options.warmup,
      iterations: options.iterations,
      twoWords: options.twoWords,
      measureCppHeap: options.measureHeap,
      checksum: measured.checksum,
      units: measured.units,
      tokens: measured.tokens,
      analyses: measured.analyses,
      warmupChecksum: warmup.checksum,
      elapsedNs: Number(elapsedNs),
      nsPerIteration: Number(elapsedNs) / options.iterations,
      corpusBytes: Buffer.byteLength(corpus),
      corpusSha256: createHash("sha256").update(corpus).digest("hex"),
      corpusLines: corpus.split(/\r?\n/).filter(Boolean).length,
      databaseBytes: databaseBuffer.byteLength,
      datasetId,
      wasmBytes: wasmBuffer.byteLength,
      wasmSha256: createHash("sha256").update(wasmBuffer).digest("hex"),
      glueBytes: glueBuffer.byteLength,
      glueSha256: createHash("sha256").update(glueBuffer).digest("hex"),
      module: modulePath,
      wrapper: options.mode === "end-to-end" ? resolve(options.wrapper) : null,
      output: profile === undefined ? null : outputPath,
      runtime: {
        pid: process.pid,
        executable: process.execPath,
        node: process.versions.node,
        v8: process.versions.v8,
        platform: process.platform,
        architecture: process.arch,
      },
    };
    if (options.measureHeap) {
      summary.cppHeap = {
        before: measured.before,
        peakResultsLive: measured.peakResultsLive,
        after: measured.after,
        maximumLinearMemoryBytes: measured.maximumLinearMemoryBytes,
        resultsLiveAllocatedDeltaBytes:
          measured.peakResultsLive.allocatedBytes -
          measured.before.allocatedBytes,
        afterAllocatedDeltaBytes:
          measured.after.allocatedBytes - measured.before.allocatedBytes,
        linearMemoryGrowthBytes:
          measured.maximumLinearMemoryBytes -
          measured.before.linearMemoryBytes,
      };
    }
    const summaryPath = `${outputPath}.summary.json`;
    await mkdir(dirname(summaryPath), {recursive: true});
    await writeFile(summaryPath, `${JSON.stringify(summary, null, 2)}\n`);
    console.log(JSON.stringify({...summary, summary: summaryPath}, null, 2));
  } finally {
    session?.disconnect();
    engine.dispose();
  }
}

await main();
