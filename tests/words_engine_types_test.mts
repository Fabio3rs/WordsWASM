import {
  createWordsAnalysisEngine,
  type AnalysisDocument,
  type AnalysisHit,
  type SearchDocument,
  type SearchHit,
  type WordsAnalysisEngine,
} from "../wasmsrc/words-engine.mjs";

declare const searchHit: SearchHit;
searchHit.form.display satisfies string;
searchHit.form.quantity.annotated satisfies string | null;
searchHit.form.quantity.positions[0]?.origin satisfies
  "stem" | "suffix" | "ending" | undefined;
if (searchHit.kind === "artificial") {
  searchHit.artificial.value satisfies number;
  // @ts-expect-error Artificial hits do not pretend to be dictionary entries.
  searchHit.lexemeId;
  // @ts-expect-error Search results never expose editorial meanings.
  searchHit.meaning;
} else {
  searchHit.lexemeId satisfies number;
  searchHit.lexical.dictionary satisfies "general" | "unique";
  // @ts-expect-error Lexical and compound hits are not artificial readings.
  searchHit.artificial;
}

declare const analysisHit: AnalysisHit;
if (analysisHit.kind !== "artificial") {
  analysisHit.meaning satisfies string;
}
if (analysisHit.morphology.kind === "verb") {
  analysisHit.morphology.tense;
  // @ts-expect-error Verb morphology has no nominal case.
  analysisHit.morphology.case;
}
if (analysisHit.kind !== "artificial" &&
    analysisHit.lexical.partOfSpeech === "verb") {
  analysisHit.lexical.verbKind satisfies import(
    "../wasmsrc/words-engine.mjs"
  ).VerbKind | null;
}
declare const addonStep: import("../wasmsrc/words-engine.mjs").AddonStepBase;
addonStep.enclitic satisfies boolean;

declare const engine: WordsAnalysisEngine;
engine.analyzeLine("amo amatus sum")[0] satisfies AnalysisDocument;
engine.searchLine("amo amatus sum")[0] satisfies SearchDocument;

void createWordsAnalysisEngine({
  datasetId: "sha256:test",
  databaseBytes: new Uint8Array(),
});
void createWordsAnalysisEngine({
  datasetId: "sha256:test",
  databaseUrl: "/words-search.wwdb",
});
void createWordsAnalysisEngine({
  databaseBytes: new Uint8Array(),
});
// @ts-expect-error Exactly one database source is required.
void createWordsAnalysisEngine({datasetId: "sha256:test"});
// @ts-expect-error The two database sources are mutually exclusive.
void createWordsAnalysisEngine({
  datasetId: "sha256:test",
  databaseBytes: new Uint8Array(),
  databaseUrl: "/words-search.wwdb",
});

const filters = {excludeWhitakerTrimReasons: ["deponent-active-form"] as const};
engine.analyze("rēs", {filters}) satisfies AnalysisDocument;
engine.search("rēs", {filters}) satisfies SearchDocument;
engine.analyzeLine("rēs reg", {filters}) satisfies AnalysisDocument[];
engine.searchLine("rēs reg", {filters}) satisfies SearchDocument[];
// @ts-expect-error Filter values reuse the existing closed reason vocabulary.
engine.analyze("rēs", {filters: {excludeWhitakerTrimReasons: ["invalid-latin"]}});
// @ts-expect-error A list is required, not a scalar reason.
engine.search("rēs", {filters: {excludeWhitakerTrimReasons: "deponent-active-form"}});
