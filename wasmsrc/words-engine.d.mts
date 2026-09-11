export type QueryStatus = "analyzed" | "unknown" | "error";
export type GrammaticalCase = "nominative" | "vocative" | "genitive" |
  "locative" | "dative" | "ablative" | "accusative";
export type GrammaticalNumber = "singular" | "plural";
export type Gender = "masculine" | "feminine" | "neuter" | "common";
export type Degree = "positive" | "comparative" | "superlative";
export type NumeralType = "cardinal" | "ordinal" | "distributive" | "adverbial";
export type Tense = "present" | "imperfect" | "future" | "perfect" |
  "pluperfect" | "future-perfect";
export type Voice = "active" | "passive";
export type Mood = "indicative" | "subjunctive" | "imperative" |
  "infinitive" | "participle";
export type PartOfSpeech = "noun" | "pronoun" | "adjective" | "numeral" |
  "adverb" | "verb" | "participle" | "supine" | "preposition" |
  "conjunction" | "interjection";
export type QuantityMatch = "unspecified" | "exact" | "unknown";
export type NounKind = "singular-only" | "plural-only" | "abstract" |
  "group" | "proper-name" | "person" | "thing" | "locale" | "place";
export type PronounKind = "personal" | "relative" | "reflexive" |
  "demonstrative" | "interrogative" | "indefinite" | "adjectival";
export type VerbKind = "to-be" | "compound-of-to-be" |
  "governs-genitive" | "governs-dative" | "governs-ablative" |
  "transitive" | "intransitive" | "impersonal" | "deponent" |
  "semideponent" | "perfect-definite";
export type Age = "archaic" | "early" | "classical" | "late" | "later" |
  "medieval" | "scholarly" | "modern";
export type SubjectArea = "agriculture" | "biological-medical" |
  "drama-arts" | "ecclesiastic" | "grammar-literature" |
  "legal-government" | "poetic" | "science-philosophy" | "technical" |
  "military" | "mythology";
export type Geography = "africa" | "britain" | "china" | "scandinavia" |
  "egypt" | "france-gaul" | "germany" | "greece" | "italy-rome" |
  "india" | "balkans" | "netherlands" | "persia" | "near-east" |
  "russia" | "spain-iberia" | "eastern-europe";
export type LexicalFrequency = "very-frequent" | "frequent" | "common" |
  "lesser" | "uncommon" | "very-rare" | "inscription" | "graffiti" |
  "pliny";
export type RuleFrequency = "most-frequent" | "sometimes" | "uncommon" |
  "infrequent" | "rare" | "very-rare" | "inscription" | "reserved-m" |
  "reserved-n";
export type Source = "source-a" | "beeson" | "cassells" |
  "adams-latin-sexual-vocabulary" | "stelten-ecclesiastical-latin" |
  "deferrari-aquinas" | "gildersleeve-lodge" | "collatinus" | "leverett" |
  "bracton" | "calepinus-novus" | "lewis-elementary-latin-dictionary" |
  "latham-medieval-word-list" | "lynn-nelson" |
  "oxford-latin-dictionary" | "souter" | "other-dictionaries" |
  "plater-white" | "lewis-short" | "found-in-translation" | "source-u" |
  "saxonis-vademecum" | "whitaker" | "temporary" | "user-submitted";

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

type NullableNominal = {
  declension: number | null;
  variant: number | null;
  case: GrammaticalCase | null;
  number: GrammaticalNumber | null;
  gender: Gender | null;
};

export type Morphology =
  | ({kind: "noun" | "pronoun"} & NullableNominal)
  | ({kind: "adjective"; degree: Degree | null} & NullableNominal)
  | ({kind: "numeral"; numeralType: NumeralType | null} & NullableNominal)
  | {kind: "adverb"; degree: Degree | null}
  | {
      kind: "verb";
      conjugation: number | null;
      variant: number | null;
      tense: Tense | null;
      voice: Voice | null;
      mood: Mood | null;
      person: 1 | 2 | 3 | null;
      number: GrammaticalNumber | null;
    }
  | {
      kind: "participle";
      conjugation: number | null;
      variant: number | null;
      case: GrammaticalCase | null;
      number: GrammaticalNumber | null;
      gender: Gender | null;
      tense: Tense | null;
      voice: Voice | null;
    }
  | {
      kind: "supine";
      conjugation: number | null;
      variant: number | null;
      case: GrammaticalCase | null;
      number: GrammaticalNumber | null;
      gender: Gender | null;
    }
  | {kind: "preposition"; governs: GrammaticalCase | null}
  | {kind: "conjunction" | "interjection"};

export interface LexicalMetadata {
  dictionary: "general" | "unique";
  entryId: number;
  age: Age | null;
  subject: SubjectArea | null;
  geography: Geography | null;
  frequency: LexicalFrequency | null;
  source: Source | null;
}

export type LexicalFlags = LexicalMetadata & (
  | {
      partOfSpeech: "noun";
      declension: number | null;
      variant: number | null;
      gender: Gender | null;
      nounKind: NounKind | null;
    }
  | {
      partOfSpeech: "pronoun";
      declension: number | null;
      variant: number | null;
      pronounKind: PronounKind | null;
      requiredPackonId: number | null;
    }
  | {
      partOfSpeech: "adjective";
      declension: number | null;
      variant: number | null;
      degree: Degree | null;
    }
  | {
      partOfSpeech: "numeral";
      declension: number | null;
      variant: number | null;
      numeralType: NumeralType | null;
      numeralValue: number | null;
    }
  | {partOfSpeech: "adverb"; degree: Degree | null}
  | {
      partOfSpeech: "verb";
      conjugation: number | null;
      variant: number | null;
      verbKind: VerbKind | null;
    }
  | {partOfSpeech: "preposition"; governs: GrammaticalCase | null}
  | {partOfSpeech: "conjunction" | "interjection"}
);

export interface RuleFlags {
  id: number;
  age: Age | null;
  frequency: RuleFrequency | null;
}

export interface ResolvedForm {
  stem: string;
  stemKey: number | null;
  ending: string;
  recognized: string;
}

export interface AddonStepBase {
  kind: "addon";
  target: "form" | "source" | "auxiliary";
  id: number;
  type: "prefix" | "tickon" | "suffix" | "tackon" | "packon";
  text: string;
  enclitic: boolean;
}

export interface RewriteStepBase {
  kind: "rewrite";
  target: "form" | "source" | "auxiliary";
  id: number;
  type: "syncope" | "orthographic";
  rule: string;
  before?: string;
  after?: string;
  category: "classical" | "medieval" | "syncope";
  scope: "initial" | "internal" | "final";
  operation: "literal" | "slur" | "double-consonant";
  stage: "main" | "early" | "fallback";
  application: {
    position: number;
    removeCount: number;
    observed: string;
    replacement: string;
  };
}

export type WhitakerTrimReason = "unsupported-short-imperative" |
  "invalid-imperative-person" | "impersonal-non-third-person" |
  "deponent-active-form" | "semideponent-passive-present-system" |
  "semideponent-active-perfect-system";

export type MorphologicalNotice = "related-passive-usage-attested" |
  "source-disagreement" | "manual-review-recommended";

export interface MorphologicalAssessment {
  generatedByWhitaker: boolean;
  whitakerTrim: {
    compatible: boolean;
    reasons: WhitakerTrimReason[];
  };
  notices: MorphologicalNotice[];
}

export type SearchDerivationStep =
  | (AddonStepBase & {meaning?: never})
  | (RewriteStepBase & {meaning?: never});
export type AnalysisDerivationStep =
  | (AddonStepBase & {meaning: string})
  | (RewriteStepBase & {meaning: string});

export interface SearchDerivation {
  method: "regular" | "unique" | "derived" | "syncope" |
    "orthographic" | "compound" | "roman-numeral";
  steps: SearchDerivationStep[];
}

export interface AnalysisDerivation {
  method: SearchDerivation["method"];
  steps: AnalysisDerivationStep[];
}

interface HitBase<D extends SearchDerivation | AnalysisDerivation> {
  partOfSpeech: PartOfSpeech;
  form: ResolvedForm;
  morphology: Morphology;
  derivation: D;
}

interface LexicalBase<D extends SearchDerivation | AnalysisDerivation>
  extends HitBase<D> {
  kind: "lexical";
  lexemeId: number;
  lemma: string;
  /** Whitaker-style citation form, e.g. "rosa, rosae". */
  dictionaryForm: string;
  lexical: LexicalFlags;
  rule: RuleFlags | null;
  quantityMatch: QuantityMatch;
  assessment: MorphologicalAssessment;
}

interface CompoundBase<D extends SearchDerivation | AnalysisDerivation>
  extends HitBase<D> {
  kind: "compound";
  lexemeId: number;
  lemma: string;
  /** Whitaker-style citation form of the source lexeme. */
  dictionaryForm: string;
  lexical: LexicalFlags;
  rule: RuleFlags | null;
  assessment: MorphologicalAssessment;
  compound: {
    construction: "finite-sum" | "esse" | "fuisse" | "iri";
    auxiliary: string;
    sourceTense: Tense | null;
    sourceVoice: Voice | null;
  };
}

interface ArtificialBase<D extends SearchDerivation | AnalysisDerivation>
  extends HitBase<D> {
  kind: "artificial";
  partOfSpeech: "numeral";
  morphology: Extract<Morphology, {kind: "numeral"}>;
  assessment: MorphologicalAssessment;
  artificial: {
    method: "roman-numeral";
    value: number;
    wellFormed: boolean;
  };
}

export type SearchLexicalHit = LexicalBase<SearchDerivation> & {meaning?: never};
export type SearchCompoundHit = CompoundBase<SearchDerivation> & {meaning?: never};
export type SearchArtificialHit = ArtificialBase<SearchDerivation>;
export type SearchHit = SearchLexicalHit | SearchCompoundHit | SearchArtificialHit;

export type AnalysisLexicalHit = LexicalBase<AnalysisDerivation> & {meaning: string};
export type AnalysisCompoundHit = CompoundBase<AnalysisDerivation> & {meaning: string};
export type AnalysisArtificialHit = ArtificialBase<AnalysisDerivation>;
export type AnalysisHit = AnalysisLexicalHit | AnalysisCompoundHit |
  AnalysisArtificialHit;

export interface SearchSuggestion<H extends SearchLexicalHit | AnalysisLexicalHit> {
  method: "two-words";
  splitAt: number;
  classification: "number-pair" | "unconstrained";
  segments: Array<{text: string; hits: H[]}>;
}

interface DocumentBase {
  schemaVersion: 4;
  datasetId: string;
  query: QueryIdentity;
  status: QueryStatus;
  diagnostics: Diagnostic[];
}

export interface IndependentToken<H extends SearchHit | AnalysisHit> {
  query: QueryIdentity;
  status: QueryStatus;
  hits: H[];
  diagnostics: Diagnostic[];
}

export interface AnalysisDocument extends DocumentBase {
  schema: "whitakers-words.browser-analysis";
  hits: AnalysisHit[];
  suggestions: Array<SearchSuggestion<AnalysisLexicalHit>>;
  tokens: Array<IndependentToken<AnalysisHit>>;
}

export interface SearchDocument extends DocumentBase {
  schema: "whitakers-words.browser-search";
  hits: SearchHit[];
  suggestions: Array<SearchSuggestion<SearchLexicalHit>>;
  tokens: Array<IndependentToken<SearchHit>>;
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
  analyzeLine(text: string, options?: AnalyzeOptions): AnalysisDocument[];
  searchLine(text: string, options?: AnalyzeOptions): SearchDocument[];
  dispose(): void;
}

type DatabaseSource =
  | {databaseUrl: string | URL; databaseBytes?: never}
  | {databaseBytes: Uint8Array | ArrayBuffer; databaseUrl?: never};

export type CreateWordsAnalysisEngineOptions = DatabaseSource & {
  /**
   * Canonical dataset identity. Omit or use an empty string to disable
   * provenance checks; named datasets require `sha256:` plus 64 lowercase
   * hexadecimal digits.
   */
  datasetId?: string;
  moduleUrl?: string | URL;
  moduleFactory?: (options?: unknown) => Promise<unknown>;
  moduleOptions?: Record<string, unknown>;
  fetchImpl?: typeof fetch;
};

export function createWordsAnalysisEngine(
  options: CreateWordsAnalysisEngineOptions,
): Promise<WordsAnalysisEngine>;
