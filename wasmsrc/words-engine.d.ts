export type QueryStatus = "analyzed" | "unknown" | "error";

export interface QueryIdentity {
  text: string;
  normalized: string;
  mode: "latin";
}

export interface Diagnostic {
  code: string;
  severity: "info" | "warning" | "error";
  parameters: Record<string, string>;
}

export interface AnalysisDocument {
  schema: "whitakers-words.analysis";
  schemaVersion: 1;
  query: QueryIdentity;
  status: QueryStatus;
  analyses: unknown[];
  diagnostics: Diagnostic[];
}

export interface SearchDocument {
  schema: "whitakers-words.search";
  schemaVersion: 1;
  datasetId: string;
  query: QueryIdentity;
  status: QueryStatus;
  hits: unknown[];
  diagnostics: Diagnostic[];
}

export interface AnalyzeOptions {
  twoWords?: boolean;
}

export interface WordsAnalysisEngine {
  readonly datasetId: string;
  readonly databaseBytes: number;
  readonly databaseKind: "full" | "search";
  analyze(text: string, options?: AnalyzeOptions): AnalysisDocument;
  search(text: string, options?: AnalyzeOptions): SearchDocument;
  dispose(): void;
}

export interface CreateWordsAnalysisEngineOptions {
  datasetId: string;
  databaseUrl?: string | URL;
  databaseBytes?: Uint8Array | ArrayBuffer;
  moduleUrl?: string | URL;
  moduleFactory?: (options?: unknown) => Promise<unknown>;
  moduleOptions?: Record<string, unknown>;
  fetchImpl?: typeof fetch;
}

export function createWordsAnalysisEngine(
  options: CreateWordsAnalysisEngineOptions,
): Promise<WordsAnalysisEngine>;
