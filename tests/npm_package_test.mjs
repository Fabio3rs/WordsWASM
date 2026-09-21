import assert from "node:assert/strict";
import {spawnSync} from "node:child_process";
import {mkdir, readFile, rm, symlink} from "node:fs/promises";
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
const wasmRoot = path.join(root, wasm.directory, "dist");
const {assets, createWordsAnalysisEngine} = await import(
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

if (process.platform === "linux" && process.arch === "x64") {
  const wrapper = plan.packages.at(-2);
  const native = plan.packages.find(
    (item) => item.name === "@fabiors/wordswasm-cli-linux-x64",
  );
  const wrapperRoot = path.join(root, wrapper.directory);
  const linkedScope = path.join(wrapperRoot, "node_modules", "@fabiors");
  await rm(path.join(wrapperRoot, "node_modules"), {recursive: true, force: true});
  await mkdir(linkedScope, {recursive: true});
  await symlink(
    path.relative(linkedScope, path.join(root, native.directory)),
    path.join(linkedScope, "wordswasm-cli-linux-x64"),
    "dir",
  );
  const result = spawnSync(process.execPath, [
    path.join(wrapperRoot, "bin", "wordswasm.cjs"), "amo",
  ], {encoding: "utf8"});
  await rm(path.join(wrapperRoot, "node_modules"), {recursive: true, force: true});
  assert.equal(result.status, 0, result.stderr);
  assert.equal(JSON.parse(result.stdout).schemaVersion, 3);
}
