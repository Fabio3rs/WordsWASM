import assert from "node:assert/strict";
import {spawnSync} from "node:child_process";
import {readFile} from "node:fs/promises";
import {pathToFileURL} from "node:url";
import {createWordsAnalysisEngine} from "../wasmsrc/words-engine.mjs";

const [cli, database, modulePath, searchDatabase] = process.argv.slice(2);
if (!cli || !database) throw new Error("usage: client_filters_test.mjs CLI FULL_DB [WASM_MODULE SEARCH_DB]");
const reasons = [
  "unsupported-short-imperative", "invalid-imperative-person",
  "impersonal-non-third-person", "deponent-active-form",
  "semideponent-passive-present-system", "semideponent-active-perfect-system",
];
const forms = ["rēs", "res", "reg", "audebantur", "ausi", "auderi", "liceo", "licent", "C.", "amaturus est", "respublica", "xyzxyz"];
function run(format, args, input, db = database) {
  return spawnSync(cli, ["--database", db, "--format", format, ...args], {
    encoding: "utf8", input: input ?? "", timeout: 15000,
  });
}
function documents(format, args, input, db) {
  const result = run(format, args, input, db);
  assert.equal(result.status, 0, `${args.join(" ")}: ${result.error ?? result.stderr}`);
  return result.stdout.trim().split("\n").map((line) => JSON.parse(line));
}
const options = {twoWords: true, filters: {excludeWhitakerTrimReasons: reasons}};
let checked = 0;
for (const format of ["analysis-v3", "search-v3"]) {
  const baseArgs = ["--two-words=legacy", "--input", "-"];
  const text = forms.join("\n");
  const plain = run(format, baseArgs, text);
  assert.equal(plain.status, 0, `${plain.error ?? plain.stderr}`);
  assert.equal(run(format, [...baseArgs, "--filter-trim=none"], text).stdout, plain.stdout);
  const rawDocs = plain.stdout.trim().split("\n").map(JSON.parse);
  const filteredDocs = documents(format, [...baseArgs, "--filter-trim", reasons.join(",")], text);
  const key = format === "analysis-v3" ? "analyses" : "hits";
  for (let index = 0; index < forms.length; ++index) {
    const raw = rawDocs[index];
    const filtered = filteredDocs[index];
    const kept = raw[key].filter((hit) => hit.assessment.whitakerTrim.compatible);
    assert.deepEqual(filtered[key], kept);
    assert.equal(filtered.status, raw.status);
    if (raw[key].length && !kept.length) {
      assert.equal(filtered.diagnostics.at(-1).code, "all-analyses-filtered");
    } else assert.deepEqual(filtered.diagnostics, raw.diagnostics);
    ++checked;
  }
  assert.ok(filteredDocs[0][key].length > 0, "rēs retains noun readings");
  assert.ok(filteredDocs[0][key].length < rawDocs[0][key].length);
  for (const index of [2, 3]) {
    assert.equal(filteredDocs[index][key].length, 0, forms[index]);
    assert.equal(filteredDocs[index].status, "analyzed");
  }
  for (const reason of reasons) {
    const selected = documents(format, [...baseArgs, `--filter-trim=${reason}`], text);
    for (let index = 0; index < forms.length; ++index) {
      assert.deepEqual(selected[index][key], rawDocs[index][key].filter(
        (hit) => !hit.assessment.whitakerTrim.reasons.includes(reason),
      ));
    }
  }
  const pretty = run(format, ["--pretty", `--filter-trim=${reasons.join(",")}`, "reg amo"]);
  assert.equal(pretty.status, 0, pretty.stderr);
  const prettyDocs = JSON.parse(pretty.stdout);
  assert.equal(prettyDocs.length, 2);
  assert.equal(prettyDocs[0][key].length, 0);
  assert.ok(prettyDocs[1][key].length > 0);
}
for (const args of [["--filter-trim=bad"], ["--filter-trim="], ["--filter-trim"],
  ["--filter-trim=none,reg"], ["--filter-trim=none", "--filter-trim=none"]]) {
  assert.equal(run("analysis-v3", args).status, 2);
}
for (const format of ["analysis", "search", "analysis-v2", "search-v2"]) {
  assert.equal(run(format, ["--filter-trim=deponent-active-form", "res"]).status, 2);
  assert.equal(run(format, ["--filter-trim=none", "res"]).status, 0);
}
assert.deepEqual(
  documents("analysis-v3", ["--filter-trim=deponent-active-form,deponent-active-form", "res"]),
  documents("analysis-v3", ["--filter-trim=deponent-active-form", "res"]),
);

if (modulePath) {
  for (const db of [database, ...(searchDatabase ? [searchDatabase] : [])]) {
    const engine = await createWordsAnalysisEngine({
      databaseBytes: new Uint8Array(await readFile(db)),
      moduleUrl: pathToFileURL(modulePath),
    });
    try {
      for (const form of forms) {
        const before = engine.search(form, {twoWords: true});
        const after = engine.search(form, options);
        assert.deepEqual(after.hits, before.hits.filter((hit) => hit.assessment.whitakerTrim.compatible));
        const [native] = documents("search-v3", ["--two-words=legacy", `--filter-trim=${reasons.join(",")}`, "--input", "-"], form, db);
        // Compound spelling differs by projection; identities and assessments agree.
        const identity = (hit) => [hit.lexemeId ?? null, hit.compound ? null : hit.form.recognized,
          hit.assessment.whitakerTrim,
          hit.assessment.notices.map((notice) => typeof notice === "string" ? notice : notice.code)];
        assert.deepEqual(after.hits.map(identity), native.hits.map(identity));
        assert.deepEqual(after.diagnostics, native.diagnostics);
        assert.equal(after.status, native.status);
        assert.deepEqual(after.tokens.map((token) => token.hits.map(identity)),
          (native.tokens ?? []).map((token) => token.hits.map(identity)));
        assert.deepEqual(after.suggestions.map((s) => s.segments.map((segment) => segment.hits.map(identity))),
          (native.suggestions ?? []).map((s) => s.segments.map((segment) => segment.hits.map(identity))));
        if (db === database) {
          const raw = engine.analyze(form, {twoWords: true});
          assert.deepEqual(engine.analyze(form, options).hits,
            raw.hits.filter((hit) => hit.assessment.whitakerTrim.compatible));
        }
        ++checked;
      }
      for (const method of db === database ? ["analyzeLine", "searchLine"] : ["searchLine"]) {
        const result = engine[method]("reg amo", options);
        assert.equal(result[0].hits.length, 0);
        assert.equal(result[0].diagnostics.at(-1).code, "all-analyses-filtered");
        assert.ok(result[1].hits.length > 0);
      }
    } finally { engine.dispose(); }
  }
}
console.log(`Client filters: ${checked} real query/projection checks passed`);
