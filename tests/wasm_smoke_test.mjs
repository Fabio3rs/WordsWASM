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
  const rawSearch = reloadable.search("anaticulus", false);
  try {
    assert.equal(rawSearch.status, "analyzed");
  } finally {
    rawSearch.hits.delete();
    rawSearch.diagnostics.delete();
    rawSearch.suggestions.delete();
  }
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
const expectedSearches = new Map();
try {
  const macron = engine.analyze("mālum");
  assert.equal(macron.schema, "whitakers-words.browser-analysis");
  assert.equal(macron.schemaVersion, 3);
  assert.equal(macron.query.normalized, "mālum");
  assert.ok(macron.hits.some((hit) => typeof hit.meaning === "string"));

  const diminutive = engine.search("anaticulus");
  assert.equal(diminutive.schema, "whitakers-words.browser-search");
  assert.equal(diminutive.schemaVersion, 3);
  assert.equal(diminutive.status, "analyzed");
  assert.ok(diminutive.hits.every((hit) => hit.meaning === undefined));
  assert.ok(diminutive.hits.some((hit) =>
    hit.form.recognized === "anaticulus" &&
    hit.derivation.steps.some((step) =>
      step.kind === "addon" && step.type === "suffix" && step.text === "icul"
    )
  ));
  expectedSearch = diminutive;

  const firstPlural = engine.search("amamus");
  assert.ok(firstPlural.hits.some((hit) =>
    hit.lexemeId === 2870 &&
    hit.rule?.id === 1312 &&
    hit.lemma === "amo" &&
    hit.partOfSpeech === "verb" &&
    hit.morphology.tense === "present" &&
    hit.morphology.voice === "active" &&
    hit.morphology.mood === "indicative" &&
    hit.morphology.person === 1 &&
    hit.morphology.number === "plural"
  ));

  const resDocument = engine.search("res");
  expectedSearches.set("res", resDocument);
  assert.ok(resDocument.hits.every((hit) => hit.lemma !== "reor"));

  for (const [surface, person] of [["reor", 1], ["reris", 2]]) {
    const document = engine.search(surface);
    expectedSearches.set(surface, document);
    assert.ok(document.hits.some((hit) =>
      hit.lemma === "reor" &&
      hit.lexical.verbKind === "deponent" &&
      hit.morphology.voice === "passive" &&
      hit.morphology.person === person
    ));
  }

  const studies = engine.search("studiisque");
  expectedSearches.set("studiisque", studies);
  const study = studies.hits.find((hit) =>
    hit.kind === "lexical" && hit.lemma === "studium"
  );
  assert.ok(study);
  assert.equal(study.form.recognized, "studiis");
  assert.deepEqual(
    study.derivation.steps.map(({kind, id, type, text}) =>
      ({kind, id, type, text})),
    [{kind: "addon", id: 314, type: "tackon", text: "que"}],
  );
  assert.equal(study.derivation.steps[0].meaning, undefined);
  assert.equal(study.derivation.steps[0].target, "form");
  assert.equal(study.derivation.steps[0].enclitic, true);
  assert.equal("addonIds" in study, false);
  const analyzedStudy = engine.analyze("studiisque").hits.find((hit) =>
    hit.kind === "lexical" && hit.lemma === "studium"
  );
  assert.equal(typeof analyzedStudy.derivation.steps[0].meaning, "string");
  assert.equal(analyzedStudy.derivation.steps[0].enclitic, true);

  const requiredPackon = engine.search("quidam").hits.find((hit) =>
    hit.kind === "lexical" &&
    hit.lexical.partOfSpeech === "pronoun" &&
    hit.lexical.requiredPackonId !== null
  );
  assert.ok(requiredPackon);
  assert.ok(requiredPackon.derivation.steps.some((step) =>
    step.kind === "addon" && step.type === "packon" &&
    step.id === requiredPackon.lexical.requiredPackonId
  ));

  const praetorDocument = engine.search("pretoribusque");
  expectedSearches.set("pretoribusque", praetorDocument);
  const praetor = praetorDocument.hits.find((hit) =>
    hit.kind === "lexical" && hit.lemma === "praetor"
  );
  assert.ok(praetor);
  assert.equal(praetor.form.recognized, "praetoribus");
  assert.deepEqual(
    praetor.derivation.steps.map(({kind}) => kind),
    ["addon", "rewrite"],
  );
  assert.equal(praetor.derivation.steps[0].text, "que");
  assert.equal(praetor.derivation.steps[1].before, "pre");
  assert.equal(praetor.derivation.steps[1].after, "prae");

  const romanDocument = engine.search("IV");
  expectedSearches.set("IV", romanDocument);
  const roman = romanDocument.hits.find((hit) =>
    hit.kind === "artificial"
  );
  assert.ok(roman);
  assert.equal(roman.artificial.value, 4);
  assert.equal(roman.artificial.wellFormed, true);
  assert.equal(roman.morphology.numeralType, "cardinal");
  assert.equal("lexical" in roman, false);
  assert.equal("lexemeId" in roman, false);

  const romanWithQueDocument = engine.search("ivque");
  expectedSearches.set("ivque", romanWithQueDocument);
  const romanWithQue = romanWithQueDocument.hits.find((hit) =>
    hit.kind === "artificial"
  );
  assert.ok(romanWithQue);
  assert.equal(romanWithQue.form.recognized, "iv");
  assert.equal(romanWithQue.artificial.value, 4);
  assert.equal(romanWithQue.artificial.wellFormed, false);
  assert.equal(romanWithQue.derivation.steps[0].type, "tackon");
  assert.equal(romanWithQue.derivation.steps[0].text, "que");

  const republic = engine.search("respublica", {twoWords: true});
  assert.equal(republic.status, "unknown");
  assert.equal(republic.hits.length, 0);
  assert.equal(republic.suggestions[0].method, "two-words");

  const compoundDocument = engine.search("amata est");
  expectedSearches.set("amata est", compoundDocument);
  const compound = compoundDocument.hits.find((hit) =>
    hit.kind === "compound"
  );
  assert.ok(compound);
  assert.equal(compound.compound.construction, "finite-sum");
  assert.equal(compound.compound.auxiliary, "est");
  assert.equal(compound.form.recognized, "amata est");

  const auxiliaryEnclitic = engine.search("amata estque").hits.find((hit) =>
    hit.kind === "compound"
  );
  assert.ok(auxiliaryEnclitic);
  assert.equal(auxiliaryEnclitic.form.recognized, "amata estque");
  assert.equal(auxiliaryEnclitic.derivation.steps[0].target, "auxiliary");
  assert.equal(auxiliaryEnclitic.derivation.steps[0].text, "que");

  const pastPeriphrastic = engine.search("amaturus fuisse").hits.find((hit) =>
    hit.kind === "compound"
  );
  assert.equal(pastPeriphrastic.compound.construction, "fuisse");

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
    for (const fixture of [
      "res", "reor", "reris", "studiisque", "pretoribusque", "ivque", "IV",
      "amata est",
    ]) {
      assert.deepEqual(searchEngine.search(fixture), expectedSearches.get(fixture));
    }
    assert.throws(
      () => searchEngine.analyze("anaticulus"),
      /words-full\.wwdb/,
    );
  } finally {
    searchEngine.dispose();
  }
}
