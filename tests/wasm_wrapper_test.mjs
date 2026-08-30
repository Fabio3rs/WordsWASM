import assert from "node:assert/strict";
import test from "node:test";

import {createWordsAnalysisEngine} from "../wasmsrc/words-engine.mjs";

const datasetId = `sha256:${"a".repeat(64)}`;

function fakeResult(schema, text, twoWords) {
  return {
    schema,
    schemaVersion: 3,
    datasetId,
    query: {text, normalized: text, mode: "latin"},
    status: "analyzed",
    hits: [],
    diagnostics: [],
    suggestions: [],
    twoWords,
  };
}

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
        return fakeResult("whitakers-words.browser-analysis", text, twoWords);
      }

      search(text, twoWords) {
        log.push(["search", text, twoWords]);
        return fakeResult("whitakers-words.browser-search", text, twoWords);
      }

      delete() { log.push(["delete"]); }
    },
  });
}

test("loads bytes once and exposes typed analysis/search contracts", async () => {
  const log = [];
  const engine = await createWordsAnalysisEngine({
    datasetId,
    databaseBytes: new Uint8Array([1, 2, 3]),
    moduleFactory: fakeFactory(log),
  });

  assert.equal(engine.datasetId, datasetId);
  assert.equal(engine.databaseBytes, 3);
  assert.equal(engine.databaseKind, "full");
  assert.equal(
    engine.analyze("mālum").schema,
    "whitakers-words.browser-analysis",
  );
  assert.equal(
    engine.search("anaticulus", {twoWords: true}).schema,
    "whitakers-words.browser-search",
  );
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
  assert.equal(
    engine.search("puella").schema,
    "whitakers-words.browser-search",
  );
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

test("validates the database source before instantiating WebAssembly", async () => {
  let instantiated = false;
  const moduleFactory = async () => {
    instantiated = true;
    return {};
  };
  await assert.rejects(
    createWordsAnalysisEngine({datasetId, moduleFactory}),
    /exactly one/,
  );
  await assert.rejects(
    createWordsAnalysisEngine({
      datasetId,
      databaseBytes: new Uint8Array(),
      databaseUrl: "words.wwdb",
      moduleFactory,
    }),
    /exactly one/,
  );
  assert.equal(instantiated, false);
});

test("starts module instantiation and database download in parallel", async () => {
  const log = [];
  let moduleStarted = false;
  let downloadStarted = false;
  let release;
  const gate = new Promise((resolve) => { release = resolve; });
  const baseFactory = fakeFactory(log);
  const creation = createWordsAnalysisEngine({
    datasetId,
    databaseUrl: "https://example.test/words.wwdb",
    moduleFactory: async (options) => {
      moduleStarted = true;
      await gate;
      return baseFactory(options);
    },
    fetchImpl: async () => {
      downloadStarted = true;
      await gate;
      return {
        ok: true,
        status: 200,
        statusText: "OK",
        async arrayBuffer() {
          return new Uint8Array([1, 2, 3]).buffer;
        },
      };
    },
  });
  await Promise.resolve();
  await Promise.resolve();
  assert.equal(moduleStarted, true);
  assert.equal(downloadStarted, true);
  release();
  const engine = await creation;
  engine.dispose();
});

function fakeVector(items, deletions, name) {
  return {
    size() { return items.length; },
    get(index) { return items[index]; },
    delete() { deletions.push(name); },
  };
}

test("releases every direct result handle when hit copying throws", async () => {
  const deletions = [];
  const log = [];
  const moduleFactory = async () => ({
    AnalysisEngine: class {
      loadDatabase() { return {ok: true, code: "", message: ""}; }
      datasetId() { return datasetId; }
      databaseBytes() { return 1; }
      databaseKind() { return "full"; }
      search() {
        return {
          schema: "whitakers-words.browser-search",
          schemaVersion: 3,
          datasetId,
          query: {text: "x", normalized: "x", mode: "latin"},
          status: "analyzed",
          hits: fakeVector([{
            kind: "lexical",
            partOfSpeech: "noun",
            form: {stem: "x", hasStemKey: false, stemKey: 0, ending: "", recognized: "x"},
            morphology: {kind: "unsupported"},
          }], deletions, "hits"),
          diagnostics: fakeVector([], deletions, "diagnostics"),
          suggestions: fakeVector([], deletions, "suggestions"),
        };
      }
      delete() { log.push("native"); }
    },
  });
  const engine = await createWordsAnalysisEngine({
    datasetId,
    databaseBytes: new Uint8Array([1]),
    moduleFactory,
  });
  assert.throws(() => engine.search("x"), /unsupported morphology/);
  assert.deepEqual(deletions.sort(), ["diagnostics", "hits", "suggestions"]);
  engine.dispose();
});

test("releases nested derivation handles when their copy throws", async () => {
  const deletions = [];
  const brokenSteps = {
    size() { throw new Error("step copy failed"); },
    delete() { deletions.push("steps"); },
  };
  const moduleFactory = async () => ({
    AnalysisEngine: class {
      loadDatabase() { return {ok: true, code: "", message: ""}; }
      datasetId() { return datasetId; }
      databaseBytes() { return 1; }
      databaseKind() { return "full"; }
      search() {
        return {
          schema: "whitakers-words.browser-search",
          schemaVersion: 3,
          datasetId,
          query: {text: "x", normalized: "x", mode: "latin"},
          status: "analyzed",
          hits: fakeVector([{
            kind: "lexical",
            partOfSpeech: "conjunction",
            form: {stem: "x", hasStemKey: false, stemKey: 0, ending: "", recognized: "x"},
            morphology: {kind: "conjunction"},
            derivation: {method: "regular", steps: brokenSteps},
          }], deletions, "hits"),
          diagnostics: fakeVector([], deletions, "diagnostics"),
          suggestions: fakeVector([], deletions, "suggestions"),
        };
      }
      delete() {}
    },
  });
  const engine = await createWordsAnalysisEngine({
    datasetId,
    databaseBytes: new Uint8Array([1]),
    moduleFactory,
  });
  assert.throws(() => engine.search("x"), /step copy failed/);
  assert.deepEqual(
    deletions.sort(),
    ["diagnostics", "hits", "steps", "suggestions"],
  );
  engine.dispose();
});
