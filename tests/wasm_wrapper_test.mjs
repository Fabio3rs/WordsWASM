import assert from "node:assert/strict";
import test from "node:test";

import {createWordsAnalysisEngine} from "../wasmsrc/words-engine.mjs";

const datasetId = `sha256:${"a".repeat(64)}`;

function fakeResult(schema, text, twoWords) {
  const tokens = text.includes(" ")
    ? text.split(/\s+/u).map((token) => ({
      query: {text: token, normalized: token, mode: "latin"},
      status: "analyzed",
      hits: [],
      diagnostics: [],
    }))
    : [];
  return {
    schema,
    schemaVersion: 4,
    datasetId,
    query: {text, normalized: text, mode: "latin"},
    status: "analyzed",
    hits: [],
    diagnostics: [],
    suggestions: [],
    tokens,
    twoWords,
  };
}

function fakeLineResults(schema, text, twoWords) {
  return text.split(/\s+/u).map((unit) => fakeResult(schema, unit, twoWords));
}

function fakeFactory(
  log,
  loadResult = {ok: true, code: "", message: ""},
  databaseKind = "full",
) {
  let loadedDatasetId = datasetId;
  return async () => ({
    AnalysisEngine: class {
      loadDatabase(bytes, id) {
        loadedDatasetId = id;
        log.push(["load", [...bytes], id]);
        return {...loadResult, databaseBytes: bytes.byteLength};
      }

      datasetId() { return loadedDatasetId; }
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

      analyzeLine(text, twoWords) {
        log.push(["analyzeLine", text, twoWords]);
        return fakeLineResults(
          "whitakers-words.browser-analysis", text, twoWords,
        );
      }

      searchLine(text, twoWords) {
        log.push(["searchLine", text, twoWords]);
        return fakeLineResults("whitakers-words.browser-search", text, twoWords);
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
  assert.deepEqual(
    engine.analyzeLine("mālum amamus").map(({query}) => query.text),
    ["mālum", "amamus"],
  );
  assert.deepEqual(
    engine.searchLine("amo amare", {twoWords: true}).map(({query}) => query.text),
    ["amo", "amare"],
  );
  assert.deepEqual(
    engine.analyze("amata est").tokens.map(({query}) => query.text),
    ["amata", "est"],
  );
  engine.dispose();
  engine.dispose();

  assert.deepEqual(log, [
    ["load", [1, 2, 3], datasetId],
    ["analyze", "mālum", false],
    ["search", "anaticulus", true],
    ["analyzeLine", "mālum amamus", false],
    ["searchLine", "amo amare", true],
    ["analyze", "amata est", false],
    ["delete"],
  ]);
  assert.throws(() => engine.analyze("amo"), /disposed/);
  assert.throws(() => engine.searchLine("amo amare"), /disposed/);
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
  assert.equal(engine.searchLine("puella rosa").length, 2);
  assert.throws(() => engine.analyze("puella"), /words-full\.wwdb/);
  assert.throws(() => engine.analyzeLine("puella rosa"), /words-full\.wwdb/);
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

test("uses anonymous dataset mode when datasetId is omitted", async () => {
  const log = [];
  const engine = await createWordsAnalysisEngine({
    databaseBytes: new Uint8Array([1, 2, 3]),
    moduleFactory: fakeFactory(log),
  });

  assert.equal(engine.datasetId, "");
  assert.deepEqual(log[0], ["load", [1, 2, 3], ""]);
  engine.dispose();
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

test("copies typed morphology assessments and concrete rewrite provenance", async () => {
  const deletions = [];
  const rewrite = {
    kind: "rewrite",
    target: "form",
    id: 17,
    type: "orthographic",
    rule: "t -> th",
    before: "t",
    after: "th",
    category: "medieval",
    scope: "initial",
    operation: "literal",
    stage: "main",
    position: 0,
    removeCount: 1,
    observed: "t",
    replacement: "th",
    hasMeaning: false,
    meaning: "",
  };
  const rawHit = {
    kind: "lexical",
    lexemeId: 7,
    lemma: "theologia",
    dictionaryForm: "theologia, theologiae",
    hasMeaning: false,
    meaning: "",
    partOfSpeech: "conjunction",
    form: {
      stem: "theologia",
      hasStemKey: false,
      stemKey: 0,
      ending: "",
      recognized: "theologia",
    },
    morphology: {kind: "conjunction"},
    derivation: {
      method: "orthographic",
      steps: fakeVector([rewrite], deletions, "steps"),
    },
    assessment: {
      generatedByWhitaker: true,
      whitakerTrimCompatible: false,
      whitakerTrimReasons: fakeVector(
        ["semideponent-passive-present-system"], deletions, "reasons",
      ),
      notices: fakeVector(
        ["source-disagreement"], deletions, "notices",
      ),
    },
    lexical: {
      dictionary: "general",
      entryId: 8,
      partOfSpeech: "conjunction",
      age: "",
      subject: "",
      geography: "",
      frequency: "",
      source: "",
    },
    rule: {present: false},
    ruleId: 0,
    quantityMatch: "unspecified",
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
          schemaVersion: 4,
          datasetId,
          query: {text: "teologia", normalized: "teologia", mode: "latin"},
          status: "analyzed",
          hits: fakeVector([rawHit], deletions, "hits"),
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
  const hit = engine.search("teologia").hits[0];
  assert.deepEqual(hit.assessment, {
    generatedByWhitaker: true,
    whitakerTrim: {
      compatible: false,
      reasons: ["semideponent-passive-present-system"],
    },
    notices: ["source-disagreement"],
  });
  assert.deepEqual(hit.derivation.steps[0].application, {
    position: 0,
    removeCount: 1,
    observed: "t",
    replacement: "th",
  });
  assert.equal(hit.derivation.steps[0].category, "medieval");
  assert.equal(hit.dictionaryForm, "theologia, theologiae");
  assert.deepEqual(deletions.sort(), [
    "diagnostics", "hits", "notices", "reasons", "steps", "suggestions",
  ]);
  engine.dispose();
});

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
          schemaVersion: 4,
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

test("releases the line and nested handles when result copying throws", async () => {
  const deletions = [];
  const moduleFactory = async () => ({
    AnalysisEngine: class {
      loadDatabase() { return {ok: true, code: "", message: ""}; }
      datasetId() { return datasetId; }
      databaseBytes() { return 1; }
      databaseKind() { return "full"; }
      searchLine() {
        const result = {
          schema: "whitakers-words.browser-search",
          schemaVersion: 4,
          datasetId,
          query: {text: "x", normalized: "x", mode: "latin"},
          status: "analyzed",
          hits: fakeVector([{
            kind: "lexical",
            partOfSpeech: "noun",
            form: {
              stem: "x",
              hasStemKey: false,
              stemKey: 0,
              ending: "",
              recognized: "x",
            },
            morphology: {kind: "unsupported"},
          }], deletions, "hits"),
          diagnostics: fakeVector([], deletions, "diagnostics"),
          suggestions: fakeVector([], deletions, "suggestions"),
        };
        return fakeVector([result], deletions, "results");
      }
      delete() {}
    },
  });
  const engine = await createWordsAnalysisEngine({
    datasetId,
    databaseBytes: new Uint8Array([1]),
    moduleFactory,
  });
  assert.throws(() => engine.searchLine("x"), /unsupported morphology/);
  assert.deepEqual(
    deletions.sort(),
    ["diagnostics", "hits", "results", "suggestions"],
  );
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
          schemaVersion: 4,
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
