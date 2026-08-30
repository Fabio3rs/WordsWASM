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

export interface MorphologyFlags {
  kind: string;
  declension: number;
  conjugation: number;
  variant: number;
  case: string;
  number: string;
  gender: string;
  degree: string;
  numeralType: string;
  tense: string;
  voice: string;
  mood: string;
  person: number;
  governs: string;
}

export interface LexicalFlags {
  dictionary: "general" | "unique";
  entryId: number;
  partOfSpeech: string;
  declension: number;
  conjugation: number;
  variant: number;
  gender: string;
  nounKind: string;
  pronounKind: string;
  degree: string;
  numeralType: string;
  numeralValue: number;
  verbKind: string;
  governs: string;
  age: string;
  subject: string;
  geography: string;
  frequency: string;
  source: string;
}

export interface RuleFlags {
  age: string | null;
  frequency: string | null;
}

export interface ResolvedHit {
  lexemeId: number | null;
  ruleId: number | null;
  addonIds: number[];
  rewriteIds: number[];
  scoreFlags: number;
  lemma: string;
  partOfSpeech: string;
  morphology: MorphologyFlags;
  lexical: LexicalFlags;
  rule: RuleFlags | null;
  meaning?: string;
  compound?: {
    construction: string;
    auxiliary: string;
  };
  artificial?: {
    method: "roman-numeral";
    value: number;
    wellFormed: boolean;
  };
}

export interface SearchSuggestion {
  method: "two-words";
  splitAt: number;
  classification: "number-pair" | "unconstrained";
  segments: Array<{text: string; hits: ResolvedHit[]}>;
}

export interface EngineDocument {
  schema: "whitakers-words.analysis" | "whitakers-words.search";
  schemaVersion: 2;
  datasetId: string;
  query: QueryIdentity;
  status: QueryStatus;
  hits: ResolvedHit[];
  diagnostics: Diagnostic[];
  suggestions: SearchSuggestion[];
}

export interface AnalyzeOptions {
  twoWords?: boolean;
}

export interface WordsAnalysisEngine {
  readonly datasetId: string;
  readonly databaseBytes: number;
  readonly databaseKind: "full" | "search";
  analyze(text: string, options?: AnalyzeOptions): EngineDocument;
  search(text: string, options?: AnalyzeOptions): EngineDocument;
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
