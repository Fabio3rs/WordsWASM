import {createHash} from "node:crypto";
import {mkdir, readFile, writeFile} from "node:fs/promises";
import {Session} from "node:inspector/promises";
import {dirname, resolve} from "node:path";
import {pathToFileURL} from "node:url";

const defaults = Object.freeze({
  module: "build/wasm-profile/words_wasm.mjs",
  baseline: null,
  candidate: null,
  output: "build/wasm-profile/profiles/wwdb-database-load-ab.json",
  warmup: 10,
  samples: 101,
  profileIterations: 0,
  interval: 250,
  measureMemory: false,
});

function usage() {
  console.error(`usage: node scripts/benchmark-wasm-database-load.mjs [options]

  --module FILE               generated words_wasm.mjs
  --baseline FILE             baseline WWDB image
  --candidate FILE            candidate WWDB image
  --output FILE               JSON result destination
  --warmup N                  unmeasured loads per variant
  --samples N                 timed loads per variant
  --profile-iterations N      loads in each optional Inspector profile
  --interval MICROSECONDS     V8 sampling interval
  --measure-memory            compare loaded C++ heap in isolated modules
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
      case "--module": options.module = value(); break;
      case "--baseline": options.baseline = value(); break;
      case "--candidate": options.candidate = value(); break;
      case "--output": options.output = value(); break;
      case "--warmup":
        options.warmup = positiveInteger(value(), argument, true);
        break;
      case "--samples":
        options.samples = positiveInteger(value(), argument);
        break;
      case "--profile-iterations":
        options.profileIterations = positiveInteger(value(), argument, true);
        break;
      case "--interval":
        options.interval = positiveInteger(value(), argument);
        break;
      case "--measure-memory": options.measureMemory = true; break;
      case "--help": usage(); process.exit(0);
      default: throw new TypeError(`unknown option: ${argument}`);
    }
  }
  if (options.baseline === null || options.candidate === null) {
    throw new TypeError("--baseline and --candidate are required");
  }
  return options;
}

function sha256(buffer) {
  return createHash("sha256").update(buffer).digest("hex");
}

function percentile(sorted, fraction) {
  const index = Math.floor((sorted.length - 1) * fraction);
  return sorted[index];
}

function summarize(samples) {
  const sorted = [...samples].sort((left, right) => left - right);
  const total = samples.reduce((sum, sample) => sum + sample, 0);
  return {
    count: samples.length,
    minimumNs: sorted[0],
    medianNs: percentile(sorted, 0.5),
    p95Ns: percentile(sorted, 0.95),
    maximumNs: sorted.at(-1),
    meanNs: total / samples.length,
  };
}

function percent(before, after) {
  return ((after - before) / before) * 100;
}

function loadOnce(engine, variant) {
  engine.reset();
  const started = process.hrtime.bigint();
  const loaded = engine.loadDatabase(variant.bytes, variant.datasetId);
  const elapsed = process.hrtime.bigint() - started;
  if (!loaded.ok) {
    throw new Error(`${variant.label}: ${loaded.code}: ${loaded.message}`);
  }
  if (loaded.databaseBytes !== variant.bytes.byteLength) {
    throw new Error(`${variant.label}: database byte count changed`);
  }
  return Number(elapsed);
}

async function profileLoads(engine, variant, iterations, interval, output) {
  const session = new Session();
  session.connect();
  try {
    await session.post("Profiler.enable");
    await session.post("Profiler.setSamplingInterval", {interval});
    engine.reset();
    await session.post("Profiler.start");
    for (let iteration = 0; iteration < iterations; ++iteration) {
      const loaded = engine.loadDatabase(variant.bytes, variant.datasetId);
      if (!loaded.ok) {
        throw new Error(`${variant.label}: ${loaded.code}: ${loaded.message}`);
      }
      if (iteration + 1 < iterations) engine.reset();
    }
    const {profile} = await session.post("Profiler.stop");
    await writeFile(output, JSON.stringify(profile));
  } finally {
    session.disconnect();
    engine.reset();
  }
}

async function instantiate(factory, moduleUrl) {
  return factory({locateFile: (name) => new URL(name, moduleUrl).href});
}

async function measureLoadedMemory(factory, moduleUrl, variant) {
  const isolatedModule = await instantiate(factory, moduleUrl);
  const engine = new isolatedModule.AnalysisEngine();
  try {
    if (typeof engine.heapSnapshot !== "function") {
      throw new Error(
        "heapSnapshot is absent; configure with WORDS_WASM_PROFILING=ON",
      );
    }
    const loaded = engine.loadDatabase(variant.bytes, variant.datasetId);
    if (!loaded.ok) {
      throw new Error(`${variant.label}: ${loaded.code}: ${loaded.message}`);
    }
    return engine.heapSnapshot();
  } finally {
    engine.delete();
  }
}

function subtractSnapshots(baseline, candidate) {
  return Object.fromEntries(Object.keys(baseline).map((property) => [
    property,
    candidate[property] - baseline[property],
  ]));
}

async function main() {
  const options = parseArguments(process.argv.slice(2));
  const modulePath = resolve(options.module);
  const wasmPath = modulePath.endsWith(".mjs")
    ? `${modulePath.slice(0, -4)}.wasm`
    : `${modulePath}.wasm`;
  const baselinePath = resolve(options.baseline);
  const candidatePath = resolve(options.candidate);
  const [moduleBuffer, wasmBuffer, baselineBuffer, candidateBuffer] =
    await Promise.all([
      readFile(modulePath),
      readFile(wasmPath),
      readFile(baselinePath),
      readFile(candidatePath),
    ]);
  const variant = (label, path, buffer) => {
    const hash = sha256(buffer);
    return {
      label,
      path,
      bytes: new Uint8Array(buffer),
      databaseBytes: buffer.byteLength,
      databaseSha256: hash,
      datasetId: `sha256:${hash}`,
    };
  };
  const baseline = variant("baseline", baselinePath, baselineBuffer);
  const candidate = variant("candidate", candidatePath, candidateBuffer);

  const moduleUrl = pathToFileURL(modulePath);
  const exports = await import(moduleUrl.href);
  const factory = exports.default ?? exports.createWordsModule;
  if (typeof factory !== "function") {
    throw new TypeError("Wasm module does not export an Emscripten factory");
  }
  const module = await instantiate(factory, moduleUrl);
  const engine = new module.AnalysisEngine();
  try {
    for (let iteration = 0; iteration < options.warmup; ++iteration) {
      const order = iteration % 2 === 0
        ? [baseline, candidate]
        : [candidate, baseline];
      for (const item of order) loadOnce(engine, item);
    }

    const baselineSamples = [];
    const candidateSamples = [];
    const pairedDeltaNs = [];
    for (let sample = 0; sample < options.samples; ++sample) {
      const order = sample % 2 === 0
        ? [baseline, candidate]
        : [candidate, baseline];
      const pair = new Map();
      for (const item of order) pair.set(item.label, loadOnce(engine, item));
      baselineSamples.push(pair.get("baseline"));
      candidateSamples.push(pair.get("candidate"));
      pairedDeltaNs.push(
        pair.get("candidate") - pair.get("baseline"),
      );
    }
    engine.reset();

    const outputPath = resolve(options.output);
    await mkdir(dirname(outputPath), {recursive: true});
    const profiles = {baseline: null, candidate: null};
    if (options.profileIterations > 0) {
      profiles.baseline = `${outputPath}.baseline.cpuprofile`;
      profiles.candidate = `${outputPath}.candidate.cpuprofile`;
      await profileLoads(
        engine, baseline, options.profileIterations, options.interval,
        profiles.baseline,
      );
      await profileLoads(
        engine, candidate, options.profileIterations, options.interval,
        profiles.candidate,
      );
    }

    let memory = null;
    if (options.measureMemory) {
      const baselineLoaded = await measureLoadedMemory(
        factory, moduleUrl, baseline,
      );
      const candidateLoaded = await measureLoadedMemory(
        factory, moduleUrl, candidate,
      );
      memory = {
        scope: "loaded C++ heap; one isolated Wasm module per variant",
        baseline: baselineLoaded,
        candidate: candidateLoaded,
        delta: subtractSnapshots(baselineLoaded, candidateLoaded),
      };
    }

    const baselineSummary = summarize(baselineSamples);
    const candidateSummary = summarize(candidateSamples);
    const result = {
      benchmark: "wwdb-database-load-ab-v1",
      region: "AnalysisEngine.loadDatabase; module initialization excluded",
      warmupLoadsPerVariant: options.warmup,
      timedLoadsPerVariant: options.samples,
      alternatingOrder: true,
      baseline: {
        path: baseline.path,
        databaseBytes: baseline.databaseBytes,
        databaseSha256: baseline.databaseSha256,
        timing: baselineSummary,
      },
      candidate: {
        path: candidate.path,
        databaseBytes: candidate.databaseBytes,
        databaseSha256: candidate.databaseSha256,
        timing: candidateSummary,
      },
      delta: {
        databaseBytes:
          candidate.databaseBytes - baseline.databaseBytes,
        medianNs:
          candidateSummary.medianNs - baselineSummary.medianNs,
        medianPercent: percent(
          baselineSummary.medianNs, candidateSummary.medianNs,
        ),
        pairedMedianNs: summarize(pairedDeltaNs).medianNs,
      },
      samples: {
        baselineNs: baselineSamples,
        candidateNs: candidateSamples,
        pairedDeltaNs,
      },
      profiles,
      memory,
      artifacts: {
        module: modulePath,
        glueBytes: moduleBuffer.byteLength,
        glueSha256: sha256(moduleBuffer),
        wasmBytes: wasmBuffer.byteLength,
        wasmSha256: sha256(wasmBuffer),
      },
      runtime: {
        executable: process.execPath,
        node: process.versions.node,
        v8: process.versions.v8,
        platform: process.platform,
        architecture: process.arch,
      },
    };
    await writeFile(outputPath, `${JSON.stringify(result, null, 2)}\n`);
    const concise = structuredClone(result);
    delete concise.samples;
    console.log(JSON.stringify({...concise, output: outputPath}, null, 2));
  } finally {
    engine.delete();
  }
}

await main();
