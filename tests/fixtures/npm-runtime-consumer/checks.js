import {createBundledWordsAnalysisEngine, createWordsAnalysisEngine} from "wordswasm";

function check(value, message) {
  if (!value) throw new Error(message);
}
async function rejects(action, pattern) {
  try { await action(); } catch (error) {
    check(pattern.test(error.message), `Unexpected error: ${error.message}`);
    return;
  }
  throw new Error(`Expected rejection: ${pattern}`);
}

export async function checkEngine() {
  for (const database of ["full", "search"]) {
    const engine = await createBundledWordsAnalysisEngine({database});
    try {
      check(engine.databaseKind === database, "Wrong database kind");
      for (const operation of database === "full" ? ["analyze", "search"] : ["search"]) {
        const result = engine[operation]("amo");
        check(result.schemaVersion === 6, `${operation}: wrong schema version`);
        check(result.schema === `whitakers-words.browser-${operation === "analyze" ? "analysis" : "search"}`,
          `${operation}: wrong schema`);
        check(result.status === "analyzed" && result.hits.length > 0, `${operation}: no results`);
      }
    } finally {
      engine.dispose();
    }
    engine.dispose();
    await rejects(() => engine.search("amo"), /disposed/);
  }
  await rejects(() => createBundledWordsAnalysisEngine({datasetId: "sha256:wrong"}), /datasetId/);
  await rejects(() => createWordsAnalysisEngine({databaseBytes: new Uint8Array([0])}), /./);
  // Browser fetch failures must propagate, followed by a successful new engine.
  if (typeof window !== "undefined" || typeof WorkerGlobalScope !== "undefined") {
    await rejects(() => createBundledWordsAnalysisEngine({
      fetchImpl: async () => new Response("missing", {status: 404}),
    }), /HTTP 404/);
  }
  const recovered = await createBundledWordsAnalysisEngine();
  recovered.dispose();
  return "passed";
}
