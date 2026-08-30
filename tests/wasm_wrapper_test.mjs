import assert from "node:assert/strict";
import test from "node:test";

import {createWordsAnalysisEngine} from "../wasmsrc/words-engine.mjs";

const datasetId = `sha256:${"a".repeat(64)}`;

function fakeFactory(
  log,
  loadResult = {ok: true, code: "", message: ""},
  databaseKind = "full",
) {
  return async () => ({
    AnalysisEngine: class {
      loadDatabase(bytes, id) {
        log.push(["load", [...bytes], id]);
        return {...loadResult, databaseBytes: bytes.byteLength};
      }

      datasetId() { return datasetId; }
      databaseBytes() { return 3; }
      databaseKind() { return databaseKind; }

      analyze(text, twoWords) {
        log.push(["analyze", text, twoWords]);
        return JSON.stringify({schema: "analysis-v1", text, twoWords});
      }

      search(text, twoWords) {
        log.push(["search", text, twoWords]);
        return JSON.stringify({schema: "search-v1", text, twoWords});
      }

      delete() { log.push(["delete"]); }
    },
  });
}

test("loads bytes once and exposes parsed analysis/search contracts", async () => {
  const log = [];
  const engine = await createWordsAnalysisEngine({
    datasetId,
    databaseBytes: new Uint8Array([1, 2, 3]),
    moduleFactory: fakeFactory(log),
  });

  assert.equal(engine.datasetId, datasetId);
  assert.equal(engine.databaseBytes, 3);
  assert.equal(engine.databaseKind, "full");
  assert.deepEqual(engine.analyze("mālum"), {
    schema: "analysis-v1", text: "mālum", twoWords: false,
  });
  assert.deepEqual(engine.search("anaticulus", {twoWords: true}), {
    schema: "search-v1", text: "anaticulus", twoWords: true,
  });
  engine.dispose();
  engine.dispose();

  assert.deepEqual(log, [
    ["load", [1, 2, 3], datasetId],
    ["analyze", "mālum", false],
    ["search", "anaticulus", true],
    ["delete"],
  ]);
  assert.throws(() => engine.analyze("amo"), /disposed/);
});

test("deletes the native object when WWDB validation fails", async () => {
  const log = [];
  await assert.rejects(
    createWordsAnalysisEngine({
      datasetId,
      databaseBytes: new Uint8Array([0]),
      moduleFactory: fakeFactory(log, {
        ok: false,
        code: "invalid-magic",
        message: "not a WWDB image",
      }),
    }),
    /invalid-magic: not a WWDB image/,
  );
  assert.deepEqual(log.at(-1), ["delete"]);
});

test("search database exposes search but refuses the full contract", async () => {
  const log = [];
  const engine = await createWordsAnalysisEngine({
    datasetId,
    databaseBytes: new Uint8Array([1, 2, 3]),
    moduleFactory: fakeFactory(
      log,
      {ok: true, code: "", message: ""},
      "search",
    ),
  });

  assert.equal(engine.databaseKind, "search");
  assert.equal(engine.search("puella").schema, "search-v1");
  assert.throws(() => engine.analyze("puella"), /words-full\.wwdb/);
  engine.dispose();
});

test("rejects malformed configuration before instantiating WebAssembly", async () => {
  let instantiated = false;
  await assert.rejects(
    createWordsAnalysisEngine({
      datasetId: "latest",
      databaseBytes: new Uint8Array(),
      moduleFactory: async () => {
        instantiated = true;
        return {};
      },
    }),
    /datasetId/,
  );
  assert.equal(instantiated, false);
});
