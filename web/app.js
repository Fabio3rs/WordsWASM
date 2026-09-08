import {createWordsAnalysisEngine} from "./engine/words-engine.mjs";

const ui = {
  loader: document.querySelector("#loader"),
  loaderText: document.querySelector("#loader-text"),
  loaderProgress: document.querySelector("#loader-progress"),
  retry: document.querySelector("#retry"),
  form: document.querySelector("#query-form"),
  controls: document.querySelector("#query-controls"),
  input: document.querySelector("#latin-input"),
  analyze: document.querySelector("#analyze"),
  results: document.querySelector("#results"),
  metaDescription: document.querySelector("#meta-description"),
  languageButtons: [...document.querySelectorAll("[data-language]")],
};

const translations = Object.freeze({
  en: {
    staticText: {
      title: "WordsWASM — Latin morphological analysis",
      metaDescription:
        "Browser demonstration of the WordsWASM Latin morphological analyzer.",
      languageAriaLabel: "Interface language",
      introduction:
        "A C++23 rewrite of Whitaker's WORDS for Latin morphological analysis, designed primarily for WebAssembly.",
      dictionaryNote:
        "Interface labels can be translated. Dictionary definitions remain in the original English distributed with Whitaker's WORDS.",
      retry: "Try again",
      formAriaLabel: "Analyze Latin text",
      inputLabel: "Latin text",
      inputPlaceholder: "Enter a word or a short passage…",
      analyze: "Analyze",
      examples: "Examples",
      sourceCode: "Source code",
    },
    partOfSpeech: {
      noun: "noun", pronoun: "pronoun", adjective: "adjective",
      numeral: "numeral", adverb: "adverb", verb: "verb",
      participle: "participle", supine: "supine",
      preposition: "preposition", conjunction: "conjunction",
      interjection: "interjection",
    },
    case: {
      nominative: "nominative", vocative: "vocative", genitive: "genitive",
      locative: "locative", dative: "dative", ablative: "ablative",
      accusative: "accusative",
    },
    number: {singular: "singular", plural: "plural"},
    gender: {
      masculine: "masculine", feminine: "feminine", neuter: "neuter",
      common: "common",
    },
    degree: {
      positive: "positive", comparative: "comparative",
      superlative: "superlative",
    },
    tense: {
      present: "present", imperfect: "imperfect", future: "future",
      perfect: "perfect", pluperfect: "pluperfect",
      "future-perfect": "future perfect",
    },
    voice: {active: "active", passive: "passive"},
    mood: {
      indicative: "indicative", subjunctive: "subjunctive",
      imperative: "imperative", infinitive: "infinitive",
      participle: "participle",
    },
    numeralType: {
      cardinal: "cardinal", ordinal: "ordinal", distributive: "distributive",
      adverbial: "adverbial",
    },
    verbKind: {
      "to-be": "verb to be", "compound-of-to-be": "compound of sum",
      "governs-genitive": "governs the genitive",
      "governs-dative": "governs the dative",
      "governs-ablative": "governs the ablative",
      transitive: "transitive", intransitive: "intransitive",
      impersonal: "impersonal", deponent: "deponent",
      semideponent: "semideponent",
      "perfect-definite": "perfect definite",
    },
    status: {analyzed: "analyzed", unknown: "not found", error: "error"},
    diagnostics: {
      "empty-input": "The query is empty.",
      "input-too-large": "The text is too large to analyze.",
      "invalid-utf8": "The text is not valid UTF-8.",
      "invalid-vowel-quantity": "The vowel-quantity notation is invalid.",
      "unicode-normalization-failed": "The text could not be normalized.",
      "unsupported-character": "The text contains an unsupported character.",
      "unsupported-part-of-speech": "This part of speech is not supported yet.",
      "unsupported-token-count": "The unit contains too many tokens.",
      "unsupported-multi-token": "This word combination is not supported.",
      "unknown-word": "No analysis was found.",
      "two-words-suggestion": "The input may correspond to two words.",
    },
    derivationType: {
      prefix: "prefix", suffix: "suffix", tickon: "tickon",
      tackon: "tackon", packon: "packon", syncope: "syncope",
      orthographic: "orthographic change",
    },
    messages: {
      preparing: "Preparing the engine…",
      readingManifest: "Reading the manifest…",
      missingDatabase: "The manifest does not contain the full database.",
      databaseDownloaded: (bytes) => `Database: ${formatBytes(bytes)}`,
      downloadingDatabase: (bytes, percent) =>
        `Downloading database: ${formatBytes(bytes)}${percent}`,
      initializingWasm: "Initializing WebAssembly…",
      ready: (bytes, transport) =>
        `Engine ready · full database ${formatBytes(bytes)} · database transport: ${transport}`,
      initializeFailed: (detail) => `Could not start: ${detail}`,
      httpError: (status, path) => `HTTP ${status} while loading ${path}`,
      noCompression: "no compression",
      deponentDetail: (voice) => `passive-form morphology (${voice}), active meaning`,
      activeBehavior: "verbal behavior with active meaning",
      voice: (voice) => `${voice} voice`,
      declension: (value) => `declension ${value}`,
      conjugation: (value) => `conjugation ${value}`,
      variant: (value) => `variant ${value}`,
      person: (value) => `person ${value}`,
      governs: (value) => `governs ${value}`,
      readingData: "Reading data",
      recognizedForm: "Recognized form",
      stem: "Stem", stemId: "Stem ID", ending: "Ending", ruleId: "Rule ID",
      ruleAge: "Rule age", ruleFrequency: "Rule frequency",
      vowelQuantity: "Vowel quantity", derivation: "Derivation",
      lexemeData: "Lexeme data", lexemeId: "Lexeme ID",
      dictionary: "Dictionary", verbClass: "Verb class", age: "Age",
      subject: "Subject", geography: "Region", frequency: "Frequency",
      source: "Source", derivationSteps: "Derivation and transformations",
      enclitic: (text) => `enclitic -${text}`,
      recognizedFormLead: "Recognized form: ",
      compound: (construction, auxiliary) =>
        `Compound construction: ${construction} · auxiliary ${auxiliary}`,
      reading: (value) => `Reading ${value}`,
      romanNumeral: (value) => `Roman numeral ${value}`,
      readingCount: (count) =>
        `${count} morphological ${count === 1 ? "reading" : "readings"}`,
      possibleSplit: (words) => `Possible split: ${words}`,
      normalized: (text) => `Normalized: ${text}`,
      rawObject: "Object returned by the API",
      resultSummary: (units, entries, hits, elapsed) =>
        `${units} ${units === 1 ? "unit" : "units"} · ` +
        `${entries} ${entries === 1 ? "entry" : "entries"} · ` +
        `${hits} ${hits === 1 ? "interpretation" : "interpretations"} · ${elapsed} ms`,
      noUnits: "No analyzable units.",
      analyzing: "Analyzing…",
      analysisFailed: (detail) => `Analysis failed: ${detail}`,
    },
  },
  "pt-BR": {
    staticText: {
      title: "WordsWASM — análise morfológica de latim",
      metaDescription:
        "Demonstração no navegador do analisador morfológico de latim WordsWASM.",
      languageAriaLabel: "Idioma da interface",
      introduction:
        "Uma reescrita em C++23 do WORDS de Whitaker para análise morfológica de latim, projetada principalmente para WebAssembly.",
      dictionaryNote:
        "Os rótulos da interface podem ser traduzidos. As definições do dicionário permanecem no inglês original distribuído com o WORDS de Whitaker.",
      retry: "Tentar novamente",
      formAriaLabel: "Analisar texto em latim",
      inputLabel: "Texto em latim",
      inputPlaceholder: "Digite uma palavra ou um trecho curto…",
      analyze: "Analisar",
      examples: "Exemplos",
      sourceCode: "Código-fonte",
    },
    partOfSpeech: {
      noun: "substantivo", pronoun: "pronome", adjective: "adjetivo",
      numeral: "numeral", adverb: "advérbio", verb: "verbo",
      participle: "particípio", supine: "supino", preposition: "preposição",
      conjunction: "conjunção", interjection: "interjeição",
    },
    case: {
      nominative: "nominativo", vocative: "vocativo", genitive: "genitivo",
      locative: "locativo", dative: "dativo", ablative: "ablativo",
      accusative: "acusativo",
    },
    number: {singular: "singular", plural: "plural"},
    gender: {
      masculine: "masculino", feminine: "feminino", neuter: "neutro",
      common: "comum",
    },
    degree: {
      positive: "positivo", comparative: "comparativo",
      superlative: "superlativo",
    },
    tense: {
      present: "presente", imperfect: "imperfeito", future: "futuro",
      perfect: "perfeito", pluperfect: "mais-que-perfeito",
      "future-perfect": "futuro perfeito",
    },
    voice: {active: "ativa", passive: "passiva"},
    mood: {
      indicative: "indicativo", subjunctive: "subjuntivo",
      imperative: "imperativo", infinitive: "infinitivo",
      participle: "particípio",
    },
    numeralType: {
      cardinal: "cardinal", ordinal: "ordinal", distributive: "distributivo",
      adverbial: "adverbial",
    },
    verbKind: {
      "to-be": "verbo ser", "compound-of-to-be": "composto de sum",
      "governs-genitive": "rege genitivo",
      "governs-dative": "rege dativo",
      "governs-ablative": "rege ablativo",
      transitive: "transitivo", intransitive: "intransitivo",
      impersonal: "impessoal", deponent: "depoente",
      semideponent: "semidepoente",
      "perfect-definite": "perfeito definido",
    },
    status: {analyzed: "analisado", unknown: "não encontrado", error: "erro"},
    diagnostics: {
      "empty-input": "A consulta está vazia.",
      "input-too-large": "O texto é grande demais para ser analisado.",
      "invalid-utf8": "O texto não é UTF-8 válido.",
      "invalid-vowel-quantity": "A marcação de quantidade vocálica é inválida.",
      "unicode-normalization-failed": "Não foi possível normalizar o texto.",
      "unsupported-character": "O texto contém um caractere não suportado.",
      "unsupported-part-of-speech": "A classe gramatical ainda não é suportada.",
      "unsupported-token-count": "A unidade contém tokens demais.",
      "unsupported-multi-token": "Esta combinação de palavras não é suportada.",
      "unknown-word": "Nenhuma análise foi encontrada.",
      "two-words-suggestion": "A entrada talvez corresponda a duas palavras.",
    },
    derivationType: {
      prefix: "prefixo", suffix: "sufixo", tickon: "acréscimo inicial",
      tackon: "acréscimo final", packon: "acréscimo combinatório",
      syncope: "síncope", orthographic: "alteração ortográfica",
    },
    messages: {
      preparing: "Preparando a engine…",
      readingManifest: "Lendo o manifesto…",
      missingDatabase: "O manifesto não contém o banco full.",
      databaseDownloaded: (bytes) => `Banco: ${formatBytes(bytes)}`,
      downloadingDatabase: (bytes, percent) =>
        `Baixando banco: ${formatBytes(bytes)}${percent}`,
      initializingWasm: "Inicializando WebAssembly…",
      ready: (bytes, transport) =>
        `Engine pronta · banco full ${formatBytes(bytes)} · transporte do banco: ${transport}`,
      initializeFailed: (detail) => `Não foi possível iniciar: ${detail}`,
      httpError: (status, path) => `HTTP ${status} ao carregar ${path}`,
      noCompression: "sem compressão",
      deponentDetail: (voice) => `morfologia de forma passiva (${voice}), sentido ativo`,
      activeBehavior: "comportamento verbal de sentido ativo",
      voice: (voice) => `voz ${voice}`,
      declension: (value) => `${value}ª declinação`,
      conjugation: (value) => `${value}ª conjugação`,
      variant: (value) => `variante ${value}`,
      person: (value) => `${value}ª pessoa`,
      governs: (value) => `rege ${value}`,
      readingData: "Dados da leitura",
      recognizedForm: "Forma reconhecida",
      stem: "Radical", stemId: "ID do radical", ending: "Terminação",
      ruleId: "ID da regra", ruleAge: "Época da regra",
      ruleFrequency: "Frequência da regra",
      vowelQuantity: "Quantidade vocálica", derivation: "Derivação",
      lexemeData: "Dados do lexema", lexemeId: "ID do lexema",
      dictionary: "Dicionário", verbClass: "Classe verbal", age: "Época",
      subject: "Área", geography: "Região", frequency: "Frequência",
      source: "Fonte", derivationSteps: "Derivação e transformações",
      enclitic: (text) => `enclítico -${text}`,
      recognizedFormLead: "Forma reconhecida: ",
      compound: (construction, auxiliary) =>
        `Construção composta: ${construction} · auxiliar ${auxiliary}`,
      reading: (value) => `Leitura ${value}`,
      romanNumeral: (value) => `Número romano ${value}`,
      readingCount: (count) =>
        `${count} ${count === 1 ? "leitura morfológica" : "leituras morfológicas"}`,
      possibleSplit: (words) => `Possível divisão: ${words}`,
      normalized: (text) => `Normalizado: ${text}`,
      rawObject: "Objeto retornado pela API",
      resultSummary: (units, entries, hits, elapsed) =>
        `${units} ${units === 1 ? "unidade" : "unidades"} · ` +
        `${entries} ${entries === 1 ? "entrada" : "entradas"} · ` +
        `${hits} ${hits === 1 ? "interpretação" : "interpretações"} · ${elapsed} ms`,
      noUnits: "Nenhuma unidade analisável.",
      analyzing: "Analisando…",
      analysisFailed: (detail) => `A análise falhou: ${detail}`,
    },
  },
  la: {
    staticText: {
      title: "WordsWASM — analysis morphologica Latina",
      metaDescription:
        "Demonstratio analysatoris morphologici Latini WordsWASM in situ interretiali.",
      languageAriaLabel: "Lingua interfaciei",
      introduction:
        "Nova scriptio C++23 operis Whitaker's WORDS ad analysin morphologicam Latinam, praecipue ad WebAssembly destinata.",
      dictionaryNote:
        "Tituli interfaciei vertuntur; definitiones lexici Anglicae manent, sicut in opere Whitaker's WORDS distributae sunt.",
      retry: "Iterum conare",
      formAriaLabel: "Textum Latinum examina",
      inputLabel: "Textus Latinus",
      inputPlaceholder: "Verbum vel brevem locum inscribe…",
      analyze: "Examina",
      examples: "Exempla",
      sourceCode: "Codex fontalis",
    },
    partOfSpeech: {
      noun: "nomen substantivum", pronoun: "pronomen",
      adjective: "adiectivum", numeral: "numerale", adverb: "adverbium",
      verb: "verbum", participle: "participium", supine: "supinum",
      preposition: "praepositio", conjunction: "coniunctio",
      interjection: "interiectio",
    },
    case: {
      nominative: "nominativus", vocative: "vocativus", genitive: "genitivus",
      locative: "locativus", dative: "dativus", ablative: "ablativus",
      accusative: "accusativus",
    },
    number: {singular: "singularis", plural: "pluralis"},
    gender: {
      masculine: "masculinum", feminine: "femininum", neuter: "neutrum",
      common: "commune",
    },
    degree: {
      positive: "positivus", comparative: "comparativus",
      superlative: "superlativus",
    },
    tense: {
      present: "praesens", imperfect: "imperfectum", future: "futurum",
      perfect: "perfectum", pluperfect: "plusquamperfectum",
      "future-perfect": "futurum exactum",
    },
    voice: {active: "activa", passive: "passiva"},
    mood: {
      indicative: "indicativus", subjunctive: "coniunctivus",
      imperative: "imperativus", infinitive: "infinitivus",
      participle: "participium",
    },
    numeralType: {
      cardinal: "cardinale", ordinal: "ordinale", distributive: "distributivum",
      adverbial: "adverbiale",
    },
    verbKind: {
      "to-be": "verbum sum", "compound-of-to-be": "compositum verbi sum",
      "governs-genitive": "genitivum regit",
      "governs-dative": "dativum regit",
      "governs-ablative": "ablativum regit",
      transitive: "transitivum", intransitive: "intransitivum",
      impersonal: "impersonale", deponent: "deponens",
      semideponent: "semideponens", "perfect-definite": "perfectum definitum",
    },
    status: {analyzed: "examinatum", unknown: "non inventum", error: "error"},
    diagnostics: {
      "empty-input": "Consultatio vacua est.",
      "input-too-large": "Textus longior est quam qui examinari possit.",
      "invalid-utf8": "Textus ex norma UTF-8 non constat.",
      "invalid-vowel-quantity": "Notatio quantitatis vocalium vitiosa est.",
      "unicode-normalization-failed": "Textus ad formam canonicam redigi non potuit.",
      "unsupported-character": "Textus litteram non admissam continet.",
      "unsupported-part-of-speech": "Haec pars orationis nondum sustinetur.",
      "unsupported-token-count": "Unitas nimis multa vocabula continet.",
      "unsupported-multi-token": "Haec verborum coniunctio non sustinetur.",
      "unknown-word": "Nulla analysis inventa est.",
      "two-words-suggestion": "Quod inscriptum est fortasse duo verba continet.",
    },
    derivationType: {
      prefix: "praefixum", suffix: "suffixum", tickon: "additamentum initiale",
      tackon: "additamentum finale", packon: "additamentum compositum",
      syncope: "syncope", orthographic: "mutatio orthographica",
    },
    messages: {
      preparing: "Machina paratur…",
      readingManifest: "Manifestum legitur…",
      missingDatabase: "Manifestum thesaurum plenum non indicat.",
      databaseDownloaded: (bytes) => `Thesaurus datorum: ${formatBytes(bytes)}`,
      downloadingDatabase: (bytes, percent) =>
        `Thesaurus datorum transfertur: ${formatBytes(bytes)}${percent}`,
      initializingWasm: "WebAssembly initium capit…",
      ready: (bytes, transport) =>
        `Machina parata · thesaurus datorum plenus ${formatBytes(bytes)} · modus transmissionis: ${transport}`,
      initializeFailed: (detail) => `Initium fieri non potuit: ${detail}`,
      httpError: (status, path) => `HTTP ${status}, dum ${path} transfertur`,
      noCompression: "sine compressione",
      deponentDetail: (voice) =>
        `morphologia formae passivae (${voice}), significatio activa`,
      activeBehavior: "usus verbalis significatione activa",
      voice: (voice) => `vox ${voice}`,
      declension: (value) => `declinatio ${value}`,
      conjugation: (value) => `coniugatio ${value}`,
      variant: (value) => `forma ${value}`,
      person: (value) => `persona ${value}`,
      governs: (value) => `${value} regit`,
      readingData: "Notitiae interpretationis",
      recognizedForm: "Forma agnita", stem: "Stirps", stemId: "Stirpis ID",
      ending: "Terminatio", ruleId: "Regulae ID", ruleAge: "Regulae aetas",
      ruleFrequency: "Regulae frequentia", vowelQuantity: "Quantitas vocalium",
      derivation: "Derivatio", lexemeData: "Notitiae lexematis",
      lexemeId: "Lexematis ID", dictionary: "Lexicon",
      verbClass: "Genus verbi", age: "Aetas", subject: "Materia",
      geography: "Regio", frequency: "Frequentia", source: "Fons",
      derivationSteps: "Derivatio et mutationes",
      enclitic: (text) => `encliticum -${text}`,
      recognizedFormLead: "Forma agnita: ",
      compound: (construction, auxiliary) =>
        `Constructio composita: ${construction} · auxiliare ${auxiliary}`,
      reading: (value) => `Interpretatio ${value}`,
      romanNumeral: (value) => `Numerus Romanus ${value}`,
      readingCount: (count) =>
        `${count} ${count === 1 ? "interpretatio morphologica" : "interpretationes morphologicae"}`,
      possibleSplit: (words) => `Divisio possibilis: ${words}`,
      normalized: (text) => `Ad formam canonicam redactum: ${text}`,
      rawObject: "Res ab API reddita",
      resultSummary: (units, entries, hits, elapsed) =>
        `${units} ${units === 1 ? "unitas" : "unitates"} · ` +
        `${entries} ${entries === 1 ? "lemma" : "lemmata"} · ` +
        `${hits} ${hits === 1 ? "interpretatio" : "interpretationes"} · ${elapsed} ms`,
      noUnits: "Nullae unitates examinabiles.",
      analyzing: "Examinatur…",
      analysisFailed: (detail) => `Analysis defecit: ${detail}`,
    },
  },
});

const supportedLanguages = new Set(Object.keys(translations));
const storageKey = "wordswasm-language";
let language = initialLanguage();
let engine;
let loaderState = {kind: "loading", key: "preparing", loaded: 0, total: 0,
  args: []};
let latestResults = null;
let analyzing = false;

function initialLanguage() {
  try {
    const saved = localStorage.getItem(storageKey);
    if (supportedLanguages.has(saved)) return saved;
  } catch {
    // The interface still works when storage is unavailable.
  }
  const requested = navigator.languages ?? [navigator.language];
  if (requested.some((value) => value?.toLowerCase().startsWith("pt"))) {
    return "pt-BR";
  }
  if (requested.some((value) => value?.toLowerCase().startsWith("la"))) {
    return "la";
  }
  return "en";
}

function copy() {
  return translations[language];
}

function message(key, ...args) {
  const value = copy().messages[key];
  return typeof value === "function" ? value(...args) : value;
}

function element(tag, className, text) {
  const output = document.createElement(tag);
  if (className) output.className = className;
  if (text !== undefined) output.textContent = text;
  return output;
}

function translated(group, value) {
  return copy()[group]?.[value] ?? String(value).replaceAll("-", " ");
}

function formatBytes(bytes) {
  return `${new Intl.NumberFormat(language, {maximumFractionDigits: 1})
    .format(bytes / 1024 / 1024)} MiB`;
}

function formatElapsed(elapsed) {
  return elapsed.toLocaleString(language, {maximumFractionDigits: 2});
}

function transportName(contentEncoding) {
  const encodings = contentEncoding.toLowerCase().split(",")
    .map((value) => value.trim());
  if (encodings.includes("br")) return "Brotli";
  if (encodings.includes("gzip")) return "gzip";
  if (encodings.includes("zstd")) return "Zstandard";
  return message("noCompression");
}

function renderLoader() {
  const state = loaderState;
  ui.retry.hidden = state.kind !== "error";
  ui.loaderProgress.hidden = state.kind !== "loading";

  if (state.kind === "ready") {
    ui.loader.className = "loader is-ready";
    ui.loaderText.textContent = message("ready", state.bytes,
      transportName(state.contentEncoding));
    return;
  }
  if (state.kind === "error") {
    ui.loader.className = "loader is-error";
    ui.loaderText.textContent = message("initializeFailed", state.detail);
    return;
  }

  ui.loader.className = "loader";
  ui.loaderText.textContent = message(state.key, ...state.args);
  if (state.total > 0) {
    ui.loaderProgress.max = state.total;
    ui.loaderProgress.value = state.loaded;
  } else {
    ui.loaderProgress.removeAttribute("value");
  }
}

function setLoading(key, loaded = 0, total = 0, ...args) {
  loaderState = {kind: "loading", key, loaded, total, args};
  renderLoader();
}

function applyLanguage() {
  const staticText = copy().staticText;
  document.documentElement.lang = language;
  document.title = staticText.title;
  ui.metaDescription.content = staticText.metaDescription;
  for (const node of document.querySelectorAll("[data-i18n]")) {
    node.textContent = staticText[node.dataset.i18n];
  }
  for (const node of document.querySelectorAll("[data-i18n-placeholder]")) {
    node.placeholder = staticText[node.dataset.i18nPlaceholder];
  }
  for (const node of document.querySelectorAll("[data-i18n-aria-label]")) {
    node.setAttribute("aria-label", staticText[node.dataset.i18nAriaLabel]);
  }
  for (const button of ui.languageButtons) {
    button.setAttribute("aria-pressed", String(button.dataset.language === language));
  }
  ui.analyze.textContent = analyzing ? message("analyzing") : staticText.analyze;
  renderLoader();
  if (latestResults) renderResults(latestResults.documents, latestResults.elapsed);
}

function selectLanguage(nextLanguage) {
  if (!supportedLanguages.has(nextLanguage) || nextLanguage === language) return;
  language = nextLanguage;
  try {
    localStorage.setItem(storageKey, language);
  } catch {
    // A private browsing policy may prohibit storage.
  }
  applyLanguage();
}

async function fetchJson(url) {
  const response = await fetch(url);
  if (!response.ok) throw new Error(message("httpError", response.status, url.pathname));
  return response.json();
}

async function downloadBytes(url, expectedBytes) {
  const response = await fetch(url);
  if (!response.ok) throw new Error(message("httpError", response.status, url.pathname));
  const contentEncoding = response.headers.get("content-encoding") ?? "identity";

  if (!response.body) {
    const bytes = new Uint8Array(await response.arrayBuffer());
    setLoading("databaseDownloaded", bytes.byteLength, expectedBytes,
      bytes.byteLength);
    return {bytes, contentEncoding};
  }

  const reader = response.body.getReader();
  const chunks = [];
  let received = 0;
  while (true) {
    const {done, value} = await reader.read();
    if (done) break;
    chunks.push(value);
    received += value.byteLength;
    const percent = expectedBytes > 0
      ? ` · ${Math.min(100, Math.round(received / expectedBytes * 100))}%`
      : "";
    setLoading("downloadingDatabase", received, expectedBytes, received, percent);
  }

  const bytes = new Uint8Array(received);
  let offset = 0;
  for (const chunk of chunks) {
    bytes.set(chunk, offset);
    offset += chunk.byteLength;
  }
  return {bytes, contentEncoding};
}

async function initialize() {
  ui.controls.disabled = true;
  setLoading("readingManifest");

  try {
    const manifestUrl = new URL("./engine/manifest.json", import.meta.url);
    const manifest = await fetchJson(manifestUrl);
    const database = manifest.databases?.full;
    if (!database) throw new Error(message("missingDatabase"));

    const databaseUrl = new URL(database.file, manifestUrl);
    const expectedBytes = manifest.files?.[database.file]?.bytes ?? 0;
    const download = await downloadBytes(databaseUrl, expectedBytes);

    setLoading("initializingWasm");
    engine?.dispose();
    engine = await createWordsAnalysisEngine({
      databaseBytes: download.bytes,
      datasetId: manifest.datasetId,
    });

    loaderState = {kind: "ready", bytes: engine.databaseBytes,
      contentEncoding: download.contentEncoding};
    renderLoader();
    ui.controls.disabled = false;
    ui.input.focus();
  } catch (error) {
    loaderState = {kind: "error", detail: error.message};
    renderLoader();
  }
}

function describeVoice(hit) {
  const voice = hit.morphology.voice;
  const verbKind = hit.lexical?.partOfSpeech === "verb"
    ? hit.lexical.verbKind : null;

  if (verbKind === "deponent" || verbKind === "semideponent") {
    return {
      title: translated("verbKind", verbKind),
      detail: voice
        ? message("deponentDetail", translated("voice", voice))
        : message("activeBehavior"),
    };
  }
  if (!voice) return null;
  return {title: message("voice", translated("voice", voice)), detail: null};
}

function morphologyTraits(hit) {
  const morphology = hit.morphology;
  const traits = [];
  const append = (value, text) => {
    if (value !== null && value !== undefined) traits.push(text(value));
  };

  append(morphology.declension, (value) => message("declension", value));
  append(morphology.conjugation, (value) => message("conjugation", value));
  append(morphology.variant, (value) => message("variant", value));
  append(morphology.case, (value) => translated("case", value));
  append(morphology.number, (value) => translated("number", value));
  append(morphology.gender, (value) => translated("gender", value));
  append(morphology.degree, (value) => translated("degree", value));
  append(morphology.numeralType, (value) => translated("numeralType", value));
  append(morphology.tense, (value) => translated("tense", value));
  append(morphology.mood, (value) => translated("mood", value));
  append(morphology.person, (value) => message("person", value));
  append(morphology.governs,
    (value) => message("governs", translated("case", value)));

  const verbKind = hit.lexical?.partOfSpeech === "verb"
    ? hit.lexical.verbKind : null;
  if (verbKind && verbKind !== "deponent" && verbKind !== "semideponent") {
    traits.push(translated("verbKind", verbKind));
  }
  return traits;
}

function renderVoice(hit, container) {
  const description = describeVoice(hit);
  if (!description) return;
  const voice = element("div", "voice");
  voice.append(element("strong", "", description.title));
  if (description.detail) voice.append(element("span", "", description.detail));
  container.append(voice);
}

function renderTraits(hit, container) {
  const traits = morphologyTraits(hit);
  if (traits.length === 0) return;
  const list = element("ul", "traits");
  for (const trait of traits) list.append(element("li", "", trait));
  container.append(list);
}

function addDefinitionList(details, entries) {
  const list = element("dl");
  for (const [term, value] of entries) {
    if (value === null || value === undefined || value === "") continue;
    list.append(element("dt", "", term), element("dd", "", String(value)));
  }
  details.append(list);
}

function renderDetails(hit, container) {
  const details = element("details");
  details.append(element("summary", "", message("readingData")));
  addDefinitionList(details, [
    [message("recognizedForm"), hit.form.recognized],
    [message("stem"), hit.form.stem], [message("stemId"), hit.form.stemKey],
    [message("ending"), hit.form.ending], [message("ruleId"), hit.rule?.id],
    [message("ruleAge"), hit.rule?.age],
    [message("ruleFrequency"), hit.rule?.frequency],
    [message("vowelQuantity"), hit.quantityMatch],
    [message("derivation"), hit.derivation.method],
  ]);
  container.append(details);
}

function renderLexemeDetails(hit, container) {
  if (!hit.lexical) return;
  const lexical = hit.lexical;
  const details = element("details", "lexeme-details");
  details.append(element("summary", "", message("lexemeData")));
  addDefinitionList(details, [
    [message("lexemeId"), hit.lexemeId],
    [message("dictionary"), lexical.dictionary],
    [message("verbClass"), lexical.verbKind
      ? translated("verbKind", lexical.verbKind) : null],
    [message("age"), lexical.age], [message("subject"), lexical.subject],
    [message("geography"), lexical.geography],
    [message("frequency"), lexical.frequency], [message("source"), lexical.source],
  ]);
  container.append(details);
}

function renderDerivation(hit, container) {
  if (hit.derivation.method === "regular" && hit.derivation.steps.length === 0) {
    return;
  }
  const details = element("details");
  details.append(element("summary", "", message("derivationSteps")));
  const list = element("ol");
  for (const step of hit.derivation.steps) {
    const type = translated("derivationType", step.type);
    const description = step.kind === "addon"
      ? `${type}: ${step.text}` : `${type}: ${step.rule}`;
    const item = element("li", "", description);
    if (step.meaning) item.append(` — ${step.meaning}`);
    list.append(item);
  }
  if (list.childElementCount === 0) {
    list.append(element("li", "", translated("derivationType",
      hit.derivation.method)));
  }
  details.append(list);
  container.append(details);
}

function renderDerivationHighlights(hit, container) {
  const enclitics = hit.derivation.steps.filter(
    (step) => step.kind === "addon" && step.enclitic,
  );
  if (enclitics.length === 0) return;

  const highlights = element("div", "derivation-highlights");
  for (const step of enclitics) {
    highlights.append(element("span", "derivation-highlight",
      message("enclitic", step.text)));
  }
  container.append(highlights);
}

function renderForm(hit, container) {
  const form = element("p", "form-reading");
  form.append(message("recognizedFormLead"),
    element("strong", "", hit.form.recognized));
  if (hit.form.ending) form.append(` · ${hit.form.stem} + ${hit.form.ending}`);
  container.append(form);
}

function sameForm(left, right) {
  return left.form.recognized === right.form.recognized &&
    left.form.stem === right.form.stem && left.form.ending === right.form.ending;
}

function sameDerivation(left, right) {
  const leftDerivation = left.derivation;
  const rightDerivation = right.derivation;
  if (leftDerivation.method !== rightDerivation.method ||
      leftDerivation.steps.length !== rightDerivation.steps.length) return false;
  return leftDerivation.steps.every((leftStep, index) => {
    const rightStep = rightDerivation.steps[index];
    return leftStep.kind === rightStep.kind &&
      leftStep.target === rightStep.target && leftStep.id === rightStep.id &&
      leftStep.type === rightStep.type && leftStep.text === rightStep.text &&
      leftStep.enclitic === rightStep.enclitic && leftStep.rule === rightStep.rule &&
      leftStep.before === rightStep.before && leftStep.after === rightStep.after &&
      leftStep.meaning === rightStep.meaning;
  });
}

function groupHits(hits) {
  const groups = [];
  const byLexeme = new Map();
  for (const [index, hit] of hits.entries()) {
    // Artificial analyses have no lexeme identity and remain independent.
    const key = hit.lexemeId === undefined
      ? `artificial:${index}` : `lexeme:${hit.lexemeId}`;
    let group = byLexeme.get(key);
    if (!group) {
      group = [];
      byLexeme.set(key, group);
      groups.push(group);
    }
    group.push(hit);
  }
  return groups;
}

function renderReading(hit, index, groupSize, hasCommonForm,
  hasCommonDerivation) {
  const reading = element("section", "reading");
  if (groupSize > 1) {
    const heading = element("div", "reading__heading");
    heading.append(
      element("span", "reading__number", message("reading", index + 1)),
      element("span", "reading__kind",
        translated("partOfSpeech", hit.partOfSpeech)),
    );
    reading.append(heading);
  }

  renderVoice(hit, reading);
  renderTraits(hit, reading);
  if (!hasCommonForm) renderForm(hit, reading);

  if (hit.kind === "compound") {
    reading.append(element("p", "form-reading",
      message("compound", hit.compound.construction, hit.compound.auxiliary)));
  }
  if (!hasCommonDerivation) {
    renderDerivationHighlights(hit, reading);
    renderDerivation(hit, reading);
  }
  renderDetails(hit, reading);
  return reading;
}

function renderHitGroup(hits) {
  const first = hits[0];
  const group = element("article", "lexeme-group");
  const heading = element("div", "lexeme-group__heading");
  const title = first.kind === "artificial"
    ? message("romanNumeral", first.artificial.value) : first.lemma;
  const lexicalPart = first.lexical?.partOfSpeech ?? first.partOfSpeech;
  heading.append(
    element("h3", "", title),
    element("span", "part-of-speech", translated("partOfSpeech", lexicalPart)),
  );
  group.append(heading);

  const meaningText = hits.find(({meaning}) => meaning)?.meaning;
  if (meaningText) {
    const meaning = element("p", "meaning", meaningText);
    meaning.lang = "en";
    group.append(meaning);
  }

  const hasCommonForm = hits.length > 1 && hits.every((hit) => sameForm(first, hit));
  if (hasCommonForm) renderForm(first, group);
  const hasCommonDerivation = hits.length > 1 &&
    hits.every((hit) => sameDerivation(first, hit));
  if (hasCommonDerivation) {
    renderDerivationHighlights(first, group);
    renderDerivation(first, group);
  }

  if (hits.length > 1) {
    group.append(element("p", "reading-count", message("readingCount", hits.length)));
  }

  const readings = element("div",
    hits.length > 1 ? "readings" : "readings readings--single");
  for (const [index, hit] of hits.entries()) {
    readings.append(renderReading(hit, index, hits.length, hasCommonForm,
      hasCommonDerivation));
  }
  group.append(readings);
  renderLexemeDetails(first, group);
  return group;
}

function renderDiagnostics(document, card) {
  if (document.diagnostics.length === 0) return;
  const hasError = document.diagnostics.some(({severity}) => severity === "error");
  const list = element("ul", hasError
    ? "diagnostics diagnostics--error" : "diagnostics");
  for (const diagnostic of document.diagnostics) {
    let diagnosticMessage = copy().diagnostics[diagnostic.code] ?? diagnostic.code;
    if (diagnostic.parameters.partOfSpeech) {
      diagnosticMessage += ` (${translated("partOfSpeech",
        diagnostic.parameters.partOfSpeech)})`;
    }
    list.append(element("li", "", diagnosticMessage));
  }
  card.append(list);
}

function renderSuggestions(document, card) {
  if (document.suggestions.length === 0) return;
  const list = element("ul", "suggestions");
  for (const suggestion of document.suggestions) {
    const words = suggestion.segments.map(({text}) => text).join(" + ");
    list.append(element("li", "", message("possibleSplit", words)));
  }
  card.append(list);
}

function renderDocument(document) {
  const card = element("article", "query-card");
  const header = element("header", "query-card__header");
  const identity = element("div");
  identity.append(element("h2", "", document.query.text));
  if (document.query.normalized !== document.query.text) {
    identity.append(element("p", "", message("normalized",
      document.query.normalized)));
  }
  const status = element("span", `status status--${document.status}`,
    translated("status", document.status));
  header.append(identity, status);
  card.append(header);

  for (const hits of groupHits(document.hits)) card.append(renderHitGroup(hits));
  renderDiagnostics(document, card);
  renderSuggestions(document, card);

  const raw = element("details", "raw-output");
  raw.append(
    element("summary", "", message("rawObject")),
    element("pre", "", JSON.stringify(document, null, 2)),
  );
  card.append(raw);
  return card;
}

function renderResults(documents, elapsed) {
  latestResults = {documents, elapsed};
  ui.results.replaceChildren();
  const hits = documents.reduce((total, document) => total + document.hits.length, 0);
  const groups = documents.reduce(
    (total, document) => total + groupHits(document.hits).length, 0);
  ui.results.append(element("p", "result-summary",
    message("resultSummary", documents.length, groups, hits,
      formatElapsed(elapsed))));

  if (documents.length === 0) {
    ui.results.append(element("p", "empty-state", message("noUnits")));
    return;
  }
  for (const document of documents) ui.results.append(renderDocument(document));
}

async function analyze() {
  const text = ui.input.value.trim();
  if (!text || !engine) return;

  analyzing = true;
  ui.analyze.disabled = true;
  ui.analyze.textContent = message("analyzing");
  ui.results.setAttribute("aria-busy", "true");
  await new Promise((resolve) => requestAnimationFrame(resolve));

  try {
    const startedAt = performance.now();
    const documents = engine.analyzeLine(text, {twoWords: true});
    renderResults(documents, performance.now() - startedAt);
  } catch (error) {
    latestResults = null;
    ui.results.replaceChildren(element("p", "empty-state",
      message("analysisFailed", error.message)));
  } finally {
    analyzing = false;
    ui.results.setAttribute("aria-busy", "false");
    ui.analyze.disabled = false;
    ui.analyze.textContent = copy().staticText.analyze;
  }
}

ui.form.addEventListener("submit", (event) => {
  event.preventDefault();
  void analyze();
});

ui.input.addEventListener("keydown", (event) => {
  if (event.key === "Enter" && (event.ctrlKey || event.metaKey)) {
    event.preventDefault();
    ui.form.requestSubmit();
  }
});

for (const example of document.querySelectorAll("[data-example]")) {
  example.addEventListener("click", () => {
    ui.input.value = example.dataset.example;
    ui.form.requestSubmit();
  });
}

for (const button of ui.languageButtons) {
  button.addEventListener("click", () => selectLanguage(button.dataset.language));
}

ui.retry.addEventListener("click", () => void initialize());
window.addEventListener("pagehide", () => engine?.dispose(), {once: true});

applyLanguage();
void initialize();
