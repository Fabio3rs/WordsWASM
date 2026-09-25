import assert from "node:assert/strict";
import {spawnSync} from "node:child_process";
import {cp, mkdtemp, mkdir, readFile, rm, symlink, writeFile} from "node:fs/promises";
import os from "node:os";
import path from "node:path";
import {pathToFileURL} from "node:url";

const planPath = path.resolve(process.argv[2] ?? "release/npm/publish-plan.json");
const root = path.dirname(planPath);
const plan = JSON.parse(await readFile(planPath, "utf8"));

assert.equal(plan.packages.length, 8);
assert.equal(plan.packages.filter((item) => item.kind === "platform").length, 6);
assert.equal(plan.packages.at(-2).name, "wordswasm-cli");
assert.equal(plan.packages.at(-1).name, "wordswasm");

for (const item of plan.packages) {
  assert.equal(typeof item.integrity, "string", `${item.name} has integrity`);
  const listing = spawnSync("tar", ["-tzf", path.join(root, item.tarball)], {encoding: "utf8"});
  assert.equal(listing.status, 0, listing.stderr);
  assert.match(listing.stdout, /package\/package\.json/);
  assert.match(listing.stdout, /package\/THIRD_PARTY_NOTICES\.md/);
  assert.doesNotMatch(listing.stdout, /\.(?:br|gz)$/m);
}

const wasm = plan.packages.at(-1);
const wasmTarball = path.join(root, wasm.tarball);
const wasmRoot = path.join(root, wasm.directory, "dist");
const {assets, createBundledWordsAnalysisEngine, createWordsAnalysisEngine} = await import(
  pathToFileURL(path.join(wasmRoot, "index.mjs")).href,
);
const [bytes, manifest] = await Promise.all([
  readFile(assets.fullDatabase),
  readFile(assets.manifest, "utf8"),
]);
const engine = await createWordsAnalysisEngine({
  databaseBytes: bytes,
  datasetId: JSON.parse(manifest).datasetId,
});
assert.ok(Number.isInteger(engine.analyze("amo").schemaVersion));
engine.dispose();

const bundled = await createBundledWordsAnalysisEngine();
assert.equal(bundled.databaseKind, "full");
assert.ok(Number.isInteger(bundled.analyze("amo").schemaVersion));
bundled.dispose();

const packageMetadata = JSON.parse(
  spawnSync("tar", ["-xOzf", wasmTarball, "package/package.json"], {encoding: "utf8"}).stdout,
);
assert.equal(packageMetadata.types, "./dist/index.d.mts");
assert.equal(packageMetadata.engines.node, ">=20");
assert.deepEqual(Object.keys(packageMetadata.exports).sort(), [
  ".",
  "./assets/dataset-manifest.json",
  "./assets/manifest.json",
  "./assets/words-full.wwdb",
  "./assets/words-search.wwdb",
  "./assets/words_wasm.mjs",
  "./assets/words_wasm.wasm",
]);
assert.ok(packageMetadata.keywords.includes("latin"));
assert.ok(packageMetadata.keywords.includes("webassembly"));

const consumerDirectory = await mkdtemp(path.join(os.tmpdir(), "wordswasm-npm-consumer-"));
try {
  await writeFile(path.join(consumerDirectory, "package.json"),
    '{"private":true,"type":"module"}\n');
  const install = spawnSync("npm", [
    "install", "--ignore-scripts", "--no-audit", "--no-fund", wasmTarball,
  ], {cwd: consumerDirectory, encoding: "utf8"});
  assert.equal(install.status, 0, install.stderr);

  await writeFile(path.join(consumerDirectory, "consumer.mjs"), `
    import assert from "node:assert/strict";
    import {
      assets,
      createBundledWordsAnalysisEngine,
    } from "wordswasm";

    assert.equal(assets.fullDatabase.protocol, "file:");
    const engine = await createBundledWordsAnalysisEngine();
    assert.equal(engine.databaseKind, "full");
    assert.equal(engine.analyze("amo").schemaVersion, 6);
    engine.dispose();
    await import("wordswasm/assets/manifest.json", {with: {type: "json"}});
  `);
  const consumer = spawnSync(process.execPath, ["consumer.mjs"], {
    cwd: consumerDirectory,
    encoding: "utf8",
  });
  assert.equal(consumer.status, 0, consumer.stderr);

  await writeFile(path.join(consumerDirectory, "consumer.mts"), `
    import {
      createBundledWordsAnalysisEngine,
      type CreateBundledWordsAnalysisEngineOptions,
    } from "wordswasm";

    const options: CreateBundledWordsAnalysisEngineOptions = {database: "full"};
    void createBundledWordsAnalysisEngine(options).then((engine) => {
      engine.databaseKind satisfies "full" | "search";
      engine.dispose();
    });
    // @ts-expect-error A bundled engine chooses its own package database.
    void createBundledWordsAnalysisEngine({databaseBytes: new Uint8Array()});
  `);
  await writeFile(path.join(consumerDirectory, "tsconfig.json"), JSON.stringify({
    compilerOptions: {
      target: "ES2022", module: "NodeNext", moduleResolution: "NodeNext",
      strict: true, noEmit: true, lib: ["ES2022", "DOM"],
    },
    include: ["consumer.mts"],
  }, null, 2));
  const typecheck = spawnSync("tsc", ["--project", "tsconfig.json"], {
    cwd: consumerDirectory,
    encoding: "utf8",
  });
  assert.equal(typecheck.status, 0, typecheck.stderr || typecheck.stdout);

  const viteFixture = path.resolve("tests/fixtures/npm-vite-consumer");
  await cp(viteFixture, consumerDirectory, {recursive: true});
  const vite = spawnSync("vite", ["build"], {
    cwd: consumerDirectory,
    encoding: "utf8",
  });
  assert.equal(vite.status, 0, vite.stderr || vite.stdout);
} finally {
  await rm(consumerDirectory, {recursive: true, force: true});
}

if (process.platform === "linux" && process.arch === "x64") {
  const wrapper = plan.packages.at(-2);
  const wrapperRoot = path.join(root, wrapper.directory);
  const wrapperScript = path.join(wrapperRoot, "bin", "wordswasm.cjs");
  const help = spawnSync(process.execPath, [wrapperScript, "--help"], {encoding: "utf8"});
  assert.equal(help.status, 0, help.stderr);
  assert.match(help.stdout, /WordsWASM command-line interface/);
  assert.match(help.stdout, /--input FILE/);
  assert.match(help.stdout, /--batch-json-lines/);
  assert.match(help.stdout, /native command exits 2 for invalid input/);

  const version = spawnSync(process.execPath, [wrapperScript, "--version"], {encoding: "utf8"});
  assert.equal(version.status, 0, version.stderr);
  assert.match(version.stdout, new RegExp(`wordswasm-cli ${plan.version}`));

  const missing = spawnSync(process.execPath, [wrapperScript, "amo"], {encoding: "utf8"});
  assert.equal(missing.status, 1);
  assert.match(missing.stderr, /unavailable.*without --omit=optional/);

  const native = plan.packages.find(
    (item) => item.name === "@fabiors/wordswasm-cli-linux-x64",
  );
  const linkedScope = path.join(wrapperRoot, "node_modules", "@fabiors");
  await rm(path.join(wrapperRoot, "node_modules"), {recursive: true, force: true});
  await mkdir(linkedScope, {recursive: true});
  await symlink(
    path.relative(linkedScope, path.join(root, native.directory)),
    path.join(linkedScope, "wordswasm-cli-linux-x64"),
    "dir",
  );
  const result = spawnSync(process.execPath, [wrapperScript, "amo"], {encoding: "utf8"});
  assert.equal(result.status, 0, result.stderr);
  assert.match(result.stdout, /verb/);
  assert.match(result.stdout, /quantity evidence/);

  const explicitJson = spawnSync(process.execPath, [
    wrapperScript, "--format", "analysis-v3", "amo",
  ], {encoding: "utf8"});
  assert.equal(explicitJson.status, 0, explicitJson.stderr);
  assert.equal(JSON.parse(explicitJson.stdout).schemaVersion, 3);

  const stream = spawnSync(process.execPath, [wrapperScript, "--format", "analysis-v3"], {
    encoding: "utf8",
    input: "amo\npuella\n",
  });
  assert.equal(stream.status, 0, stream.stderr);
  assert.equal(stream.stdout.split("\n").filter(Boolean).length, 2);

  const explicitStream = spawnSync(process.execPath, [
    wrapperScript, "--format", "analysis-v3", "--input", "-"], {
    encoding: "utf8",
    input: "amo\n",
  });
  assert.equal(explicitStream.status, 0, explicitStream.stderr);
  assert.equal(explicitStream.stdout.split("\n").filter(Boolean).length, 1);

  const pretty = spawnSync(process.execPath, [wrapperScript, "--pretty", "amo"], {
    encoding: "utf8",
  });
  assert.equal(pretty.status, 0, pretty.stderr);
  assert.ok(pretty.stdout.split("\n").length > 3);
  assert.equal(JSON.parse(pretty.stdout).schemaVersion, 4);

  const prettyLine = spawnSync(process.execPath, [
    wrapperScript, "--pretty", "amo puellam",
  ], {encoding: "utf8"});
  assert.equal(prettyLine.status, 0, prettyLine.stderr);
  const prettyLineDocument = JSON.parse(prettyLine.stdout);
  assert.ok(Array.isArray(prettyLineDocument));
  assert.equal(prettyLineDocument.length, 2);
  assert.ok(prettyLineDocument.every((item) => item.schemaVersion === 4));

  const invalidBatch = spawnSync(process.execPath, [
    wrapperScript, "--pretty", "--batch-json-lines",
  ], {encoding: "utf8"});
  await rm(path.join(wrapperRoot, "node_modules"), {recursive: true, force: true});
  assert.equal(invalidBatch.status, 2);
  assert.match(invalidBatch.stderr, /cannot be used with stream input/);
}
