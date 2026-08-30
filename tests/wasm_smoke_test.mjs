import assert from "node:assert/strict";
import {readFile} from "node:fs/promises";
import {pathToFileURL} from "node:url";

import {createWordsAnalysisEngine} from "../wasmsrc/words-engine.mjs";

const [modulePath, databasePath, datasetId, searchDatabasePath] =
  process.argv.slice(2);
if (modulePath === undefined || databasePath === undefined ||
    datasetId === undefined) {
  throw new Error(
    "usage: node wasm_smoke_test.mjs MODULE FULL_DATABASE sha256:... " +
      "[SEARCH_DATABASE]",
  );
}

const moduleUrl = pathToFileURL(modulePath);
const moduleExports = await import(moduleUrl.href);
const databaseBytes = new Uint8Array(await readFile(databasePath));
const module = await moduleExports.default({
  locateFile: (name) => new URL(name, moduleUrl).href,
});

assert.equal(module._malloc, undefined);
assert.equal(module.HEAPU8, undefined);

const reloadable = new module.AnalysisEngine();
try {
  assert.equal(reloadable.loadDatabase(databaseBytes, datasetId).ok, true);
  assert.equal(
    reloadable.loadDatabase(new Uint8Array([0]), datasetId).ok,
    false,
  );
  assert.equal(reloadable.ready(), true);
  assert.equal(
    JSON.parse(reloadable.search("anaticulus", false)).status,
    "analyzed",
  );
} finally {
  reloadable.delete();
}

const engine = await createWordsAnalysisEngine({
  datasetId,
  databaseBytes,
  moduleUrl,
  moduleFactory: async () => module,
});

let expectedSearch;
try {
  const macron = engine.analyze("mālum");
  assert.equal(macron.schema, "whitakers-words.analysis");
  assert.equal(macron.schemaVersion, 1);
  assert.equal(macron.query.normalized, "mālum");

  const diminutive = engine.search("anaticulus");
  assert.equal(diminutive.schema, "whitakers-words.search");
  assert.equal(diminutive.schemaVersion, 1);
  assert.equal(diminutive.status, "analyzed");
  expectedSearch = diminutive;

  const invalid = engine.analyze("ß");
  assert.equal(invalid.status, "error");
  assert.ok(
    invalid.diagnostics.some(({code}) => code === "unsupported-character"),
  );

  console.log(JSON.stringify({
    ok: true,
    datasetId: engine.datasetId,
    databaseBytes: engine.databaseBytes,
  }));
} finally {
  engine.dispose();
}

if (searchDatabasePath !== undefined) {
  const searchBytes = new Uint8Array(await readFile(searchDatabasePath));
  const searchEngine = await createWordsAnalysisEngine({
    datasetId,
    databaseBytes: searchBytes,
    moduleUrl,
    moduleFactory: async () => module,
  });
  try {
    assert.equal(searchEngine.databaseKind, "search");
    assert.deepEqual(searchEngine.search("anaticulus"), expectedSearch);
    assert.throws(
      () => searchEngine.analyze("anaticulus"),
      /words-full\.wwdb/,
    );
  } finally {
    searchEngine.dispose();
  }
}
