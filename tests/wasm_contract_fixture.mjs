import {readFile} from "node:fs/promises";
import {pathToFileURL} from "node:url";

import {createWordsAnalysisEngine} from "../wasmsrc/words-engine.mjs";

const [modulePath, databasePath, datasetId] = process.argv.slice(2);
if (!modulePath || !databasePath || !datasetId) {
  throw new Error("usage: node wasm_contract_fixture.mjs MODULE FULL_DATABASE DATASET_ID");
}

const moduleUrl = pathToFileURL(modulePath);
const databaseBytes = new Uint8Array(await readFile(databasePath));
const engine = await createWordsAnalysisEngine({
  datasetId,
  databaseBytes,
  moduleUrl,
});

try {
  const fixtures = [
    ["studiisque", undefined],
    ["pretoribusque", undefined],
    ["anaticulus", undefined],
    ["ivque", undefined],
    ["IV", undefined],
    ["amata est", undefined],
    ["amata estque", undefined],
    ["amaturus fuisse", undefined],
    ["respublica", {twoWords: true}],
  ];
  process.stdout.write(JSON.stringify({
    analysis: fixtures.map(([text, options]) => engine.analyze(text, options)),
    search: fixtures.map(([text, options]) => engine.search(text, options)),
  }));
} finally {
  engine.dispose();
}
