import {
  createWordsAnalysisEngine,
  type AnalysisHit,
  type SearchHit,
} from "../wasmsrc/words-engine.mjs";

declare const searchHit: SearchHit;
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

void createWordsAnalysisEngine({
  datasetId: "sha256:test",
  databaseBytes: new Uint8Array(),
});
void createWordsAnalysisEngine({
  datasetId: "sha256:test",
  databaseUrl: "/words-search.wwdb",
});
// @ts-expect-error Exactly one database source is required.
void createWordsAnalysisEngine({datasetId: "sha256:test"});
// @ts-expect-error The two database sources are mutually exclusive.
void createWordsAnalysisEngine({
  datasetId: "sha256:test",
  databaseBytes: new Uint8Array(),
  databaseUrl: "/words-search.wwdb",
});
