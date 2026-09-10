import {readFile} from "node:fs/promises";
import {resolve} from "node:path";

function usage() {
  console.error(
    "usage: node scripts/compare-wasm-profiles.mjs " +
    "BASELINE.summary.json CANDIDATE.summary.json",
  );
}

async function readSummary(path) {
  return JSON.parse(await readFile(resolve(path), "utf8"));
}

function percent(before, after) {
  return ((after - before) / before) * 100;
}

function requireEqual(baseline, candidate, property, description = property) {
  if (baseline[property] !== candidate[property]) {
    throw new Error(
      `${description} differs: ${baseline[property]} != ${candidate[property]}`,
    );
  }
}

const [baselinePath, candidatePath, extra] = process.argv.slice(2);
if (baselinePath === undefined || candidatePath === undefined ||
    extra !== undefined) {
  usage();
  process.exitCode = 2;
} else {
  const [baseline, candidate] = await Promise.all([
    readSummary(baselinePath),
    readSummary(candidatePath),
  ]);

  requireEqual(baseline, candidate, "mode");
  requireEqual(baseline, candidate, "profiler");
  requireEqual(baseline, candidate, "samplingIntervalUs");
  requireEqual(baseline, candidate, "warmupIterations");
  requireEqual(baseline, candidate, "twoWords");
  requireEqual(baseline, candidate, "measureCppHeap");
  requireEqual(baseline, candidate, "corpusBytes");
  requireEqual(baseline, candidate, "corpusSha256");
  requireEqual(baseline, candidate, "databaseBytes");
  requireEqual(baseline, candidate, "datasetId");
  requireEqual(
    baseline.runtime, candidate.runtime, "node", "Node version",
  );
  requireEqual(baseline.runtime, candidate.runtime, "v8", "V8 version");
  requireEqual(
    baseline.runtime, candidate.runtime, "executable", "Node executable",
  );
  requireEqual(baseline.runtime, candidate.runtime, "platform");
  requireEqual(baseline.runtime, candidate.runtime, "architecture");
  const baselineChecksum = baseline.checksum / baseline.iterations;
  const candidateChecksum = candidate.checksum / candidate.iterations;
  if (!Number.isSafeInteger(baselineChecksum) ||
      baselineChecksum !== candidateChecksum) {
    throw new Error(
      `checksum per iteration differs: ${baselineChecksum} != ` +
      `${candidateChecksum}`,
    );
  }

  const comparison = {
    mode: baseline.mode,
    checksumPerIteration: baselineChecksum,
    runtime: {
      executable: baseline.runtime.executable,
      node: baseline.runtime.node,
      v8: baseline.runtime.v8,
      platform: baseline.runtime.platform,
      architecture: baseline.runtime.architecture,
    },
    baseline: {
      summary: resolve(baselinePath),
      nsPerIteration: baseline.nsPerIteration,
      wasmBytes: baseline.wasmBytes,
      wasmSha256: baseline.wasmSha256,
    },
    candidate: {
      summary: resolve(candidatePath),
      nsPerIteration: candidate.nsPerIteration,
      wasmBytes: candidate.wasmBytes,
      wasmSha256: candidate.wasmSha256,
    },
    delta: {
      nsPerIteration: candidate.nsPerIteration - baseline.nsPerIteration,
      timePercent: percent(
        baseline.nsPerIteration, candidate.nsPerIteration,
      ),
      wasmBytes: candidate.wasmBytes - baseline.wasmBytes,
      wasmPercent: percent(baseline.wasmBytes, candidate.wasmBytes),
      sameWasmArtifact: baseline.wasmSha256 === candidate.wasmSha256,
    },
  };
  if (baseline.measureCppHeap) {
    comparison.baseline.cppHeap = baseline.cppHeap;
    comparison.candidate.cppHeap = candidate.cppHeap;
    comparison.delta.resultsLiveAllocatedBytes =
      candidate.cppHeap.resultsLiveAllocatedDeltaBytes -
      baseline.cppHeap.resultsLiveAllocatedDeltaBytes;
    comparison.delta.maximumLinearMemoryBytes =
      candidate.cppHeap.maximumLinearMemoryBytes -
      baseline.cppHeap.maximumLinearMemoryBytes;
    comparison.delta.linearMemoryGrowthBytes =
      candidate.cppHeap.linearMemoryGrowthBytes -
      baseline.cppHeap.linearMemoryGrowthBytes;
  }
  console.log(JSON.stringify(comparison, null, 2));
}
