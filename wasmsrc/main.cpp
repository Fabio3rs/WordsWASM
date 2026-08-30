#include "words/engine.hpp"
#include "words/lexeme.hpp"

#include <emscripten/bind.h>
#include <emscripten/val.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>
#include <memory>
#include <new>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace {

struct LoadResult final {
    bool ok{};
    std::string code;
    std::string message;
    std::uint32_t database_bytes{};
    std::string database_kind;
};

constexpr std::string_view full_database_kind{"full"};
constexpr std::string_view search_database_kind{"search"};

struct BrowserQuery final {
    std::string text;
    std::string normalized;
    std::string mode{"latin"};
};

struct BrowserDiagnostic final {
    std::string code;
    std::string severity;
    std::string part_of_speech;
};

// WHY: Embind has no useful JavaScript representation for the morphology
// variant.  A tagged, fixed-shape value object retains every grammatical flag
// without JSON serialization or heap-allocated polymorphic objects.
struct BrowserMorphology final {
    std::string kind;
    std::uint32_t declension{};
    std::uint32_t conjugation{};
    std::uint32_t variant{};
    std::string grammatical_case;
    std::string number;
    std::string gender;
    std::string degree;
    std::string numeral_type;
    std::string tense;
    std::string voice;
    std::string mood;
    std::uint32_t person{};
    std::string governs;
    auto operator<=>(const BrowserMorphology &) const = default;
};

struct BrowserLexicalFlags final {
    std::string dictionary;
    std::uint32_t entry_id{};
    std::string part_of_speech;
    std::uint32_t declension{};
    std::uint32_t conjugation{};
    std::uint32_t variant{};
    std::string gender;
    std::string noun_kind;
    std::string pronoun_kind;
    std::string degree;
    std::string numeral_type;
    std::uint32_t numeral_value{};
    std::string verb_kind;
    std::string governs;
    std::string age;
    std::string subject;
    std::string geography;
    std::string frequency;
    std::string source;
    auto operator<=>(const BrowserLexicalFlags &) const = default;
};

struct BrowserRuleFlags final {
    bool present{};
    std::string age;
    std::string frequency;
    auto operator<=>(const BrowserRuleFlags &) const = default;
};

struct BrowserSearchHit final {
    bool has_lexeme{true};
    std::uint32_t lexeme_id{};
    bool has_rule{};
    std::uint32_t rule_id{};
    std::vector<std::uint32_t> addon_ids;
    std::vector<std::uint32_t> rewrite_ids;
    std::uint32_t score_flags{};
    std::string lemma;
    bool has_meaning{};
    std::string meaning;
    std::string part_of_speech;
    BrowserMorphology morphology;
    BrowserLexicalFlags lexical;
    BrowserRuleFlags rule;
    bool compound{};
    std::string compound_construction;
    std::string compound_auxiliary;
    bool artificial{};
    std::string artificial_method;
    std::uint32_t artificial_value{};
    bool artificial_well_formed{};
    auto operator<=>(const BrowserSearchHit &) const = default;
};

struct BrowserSearchSegment final {
    std::string text;
    std::vector<BrowserSearchHit> hits;
};

struct BrowserSearchSuggestion final {
    std::string method{"two-words"};
    std::uint32_t split_at{};
    std::string classification;
    std::vector<BrowserSearchSegment> segments;
};

struct BrowserSearchResult final {
    std::string schema{"whitakers-words.search"};
    std::uint32_t schema_version{2U};
    std::string dataset_id;
    BrowserQuery query;
    std::string status;
    std::vector<BrowserSearchHit> hits;
    std::vector<BrowserDiagnostic> diagnostics;
    std::vector<BrowserSearchSuggestion> suggestions;
};

template <std::size_t Size>
[[nodiscard]] std::string token(
    const std::uint8_t ordinal,
    const std::array<std::string_view, Size> &values) {
    return ordinal == 0U || ordinal >= values.size()
               ? std::string{}
               : std::string{values[ordinal]};
}

[[nodiscard]] std::string age_name(const std::uint8_t value) {
    constexpr std::array<std::string_view, 9> values{
        "",      "archaic",  "early",     "classical", "late",
        "later", "medieval", "scholarly", "modern",
    };
    return token(value, values);
}

[[nodiscard]] std::string lexical_frequency_name(const std::uint8_t value) {
    constexpr std::array<std::string_view, 10> values{
        "",         "very-frequent", "frequent",    "common",   "lesser",
        "uncommon", "very-rare",     "inscription", "graffiti", "pliny",
    };
    return token(value, values);
}

[[nodiscard]] std::string rule_frequency_name(const std::uint8_t value) {
    constexpr std::array<std::string_view, 10> values{
        "",     "most-frequent", "sometimes",   "uncommon",   "infrequent",
        "rare", "very-rare",     "inscription", "reserved-m", "reserved-n",
    };
    return token(value, values);
}

[[nodiscard]] std::string subject_name(const std::uint8_t value) {
    constexpr std::array<std::string_view, 12> values{
        "",                    "agriculture",
        "biological-medical",  "drama-arts",
        "ecclesiastic",        "grammar-literature",
        "legal-government",    "poetic",
        "science-philosophy",  "technical",
        "military",            "mythology",
    };
    return token(value, values);
}

[[nodiscard]] std::string geography_name(const std::uint8_t value) {
    constexpr std::array<std::string_view, 18> values{
        "",       "africa",       "britain",        "china",  "scandinavia",
        "egypt",  "france-gaul",  "germany",        "greece", "italy-rome",
        "india",  "balkans",      "netherlands",    "persia", "near-east",
        "russia", "spain-iberia", "eastern-europe",
    };
    return token(value, values);
}

[[nodiscard]] std::string source_name(const std::uint8_t value) {
    constexpr std::array<std::string_view, 26> values{
        "", "source-a", "beeson", "cassells",
        "adams-latin-sexual-vocabulary", "stelten-ecclesiastical-latin",
        "deferrari-aquinas", "gildersleeve-lodge", "collatinus", "leverett",
        "bracton", "calepinus-novus", "lewis-elementary-latin-dictionary",
        "latham-medieval-word-list", "lynn-nelson", "oxford-latin-dictionary",
        "souter", "other-dictionaries", "plater-white", "lewis-short",
        "found-in-translation", "source-u", "saxonis-vademecum", "whitaker",
        "temporary", "user-submitted",
    };
    return token(value, values);
}

[[nodiscard]] std::string noun_kind_name(const std::uint8_t value) {
    constexpr std::array<std::string_view, 10> values{
        "",       "singular-only", "plural-only", "abstract", "group",
        "proper-name", "person", "thing", "locale", "place",
    };
    return token(value, values);
}

template <class Enum, std::size_t Size>
[[nodiscard]] std::string enum_name(
    const Enum value, const std::array<std::string_view, Size> &values) {
    return token(static_cast<std::uint8_t>(std::to_underlying(value)), values);
}

[[nodiscard]] std::string gender_name(const words::Gender value) {
    constexpr std::array<std::string_view, 5> values{
        "", "masculine", "feminine", "neuter", "common"};
    return enum_name(value, values);
}

[[nodiscard]] std::string case_name(const words::GrammaticalCase value) {
    constexpr std::array<std::string_view, 8> values{
        "", "nominative", "vocative", "genitive", "locative", "dative",
        "ablative", "accusative"};
    return enum_name(value, values);
}

[[nodiscard]] std::string number_name(const words::GrammaticalNumber value) {
    constexpr std::array<std::string_view, 3> values{"", "singular", "plural"};
    return enum_name(value, values);
}

[[nodiscard]] std::string degree_name(const words::Degree value) {
    constexpr std::array<std::string_view, 4> values{
        "", "positive", "comparative", "superlative"};
    return enum_name(value, values);
}

[[nodiscard]] std::string numeral_type_name(const words::NumeralType value) {
    constexpr std::array<std::string_view, 5> values{
        "", "cardinal", "ordinal", "distributive", "adverbial"};
    return enum_name(value, values);
}

[[nodiscard]] std::string tense_name(const words::Tense value) {
    constexpr std::array<std::string_view, 7> values{
        "", "present", "imperfect", "future", "perfect", "pluperfect",
        "future-perfect"};
    return enum_name(value, values);
}

[[nodiscard]] std::string voice_name(const words::Voice value) {
    constexpr std::array<std::string_view, 3> values{"", "active", "passive"};
    return enum_name(value, values);
}

[[nodiscard]] std::string mood_name(const words::Mood value) {
    constexpr std::array<std::string_view, 6> values{
        "", "indicative", "subjunctive", "imperative", "infinitive",
        "participle"};
    return enum_name(value, values);
}

[[nodiscard]] std::string pronoun_kind_name(const words::PronounKind value) {
    constexpr std::array<std::string_view, 8> values{
        "", "personal", "relative", "reflexive", "demonstrative",
        "interrogative", "indefinite", "adjectival"};
    return enum_name(value, values);
}

[[nodiscard]] std::string verb_kind_name(const words::VerbKind value) {
    constexpr std::array<std::string_view, 12> values{
        "", "to-be", "compound-of-to-be", "governs-genitive",
        "governs-dative", "governs-ablative", "transitive", "intransitive",
        "impersonal", "deponent", "semideponent", "perfect-definite"};
    return enum_name(value, values);
}

[[nodiscard]] std::string lexical_part_name(const words::PartOfSpeech part) {
    constexpr std::array<std::string_view, 13> values{
        "unknown", "noun", "pronoun", "pronoun", "adjective", "numeral",
        "adverb", "verb", "verb", "verb", "preposition", "conjunction",
        "interjection"};
    const auto ordinal = static_cast<std::size_t>(std::to_underlying(part));
    return std::string{ordinal < values.size() ? values[ordinal] : values[0]};
}

[[nodiscard]] std::string normalized_meaning(const std::string_view meaning) {
    auto clean = meaning;
    while (!clean.empty() && static_cast<unsigned char>(clean.front()) <= ' ') {
        clean.remove_prefix(1U);
    }
    while (!clean.empty() && static_cast<unsigned char>(clean.back()) <= ' ') {
        clean.remove_suffix(1U);
    }
    if (!clean.empty() && clean.front() == '|') {
        clean.remove_prefix(1U);
        while (!clean.empty() &&
               static_cast<unsigned char>(clean.front()) <= ' ') {
            clean.remove_prefix(1U);
        }
    }
    return std::string{clean};
}

[[nodiscard]] std::string compound_kind_name(const words::CompoundKind kind) {
    constexpr std::array<std::string_view, 5> values{
        "", "finite-sum", "esse", "fuisse", "iri"};
    return enum_name(kind, values);
}

[[nodiscard]] std::string status_name(const words::QueryStatus status) {
    constexpr std::array<std::string_view, 3> values{
        "analyzed", "unknown", "error"};
    const auto ordinal = static_cast<std::size_t>(std::to_underlying(status));
    return std::string{ordinal < values.size() ? values[ordinal] : values[2]};
}

[[nodiscard]] BrowserMorphology
browser_morphology(const words::Morphology &morphology,
                   const words::PartOfSpeech lexical_part) {
    BrowserMorphology output;
    std::visit(
        [&](const auto &value) {
            using Value = std::remove_cvref_t<decltype(value)>;
            if constexpr (std::is_same_v<Value, words::NounMorphology> ||
                          std::is_same_v<Value, words::PronounMorphology>) {
                output.kind = std::is_same_v<Value, words::NounMorphology>
                                  ? "noun"
                                  : "pronoun";
                output.declension = value.declension;
                output.variant = value.variant;
                output.grammatical_case = case_name(value.grammatical_case);
                output.number = number_name(value.number);
                output.gender = gender_name(value.gender);
            } else if constexpr (std::is_same_v<Value,
                                                words::AdjectiveMorphology>) {
                output.kind = "adjective";
                output.declension = value.declension;
                output.variant = value.variant;
                output.grammatical_case = case_name(value.grammatical_case);
                output.number = number_name(value.number);
                output.gender = gender_name(value.gender);
                output.degree = degree_name(value.degree);
            } else if constexpr (std::is_same_v<Value,
                                                words::NumeralMorphology>) {
                output.kind = "numeral";
                output.declension = value.declension;
                output.variant = value.variant;
                output.grammatical_case = case_name(value.grammatical_case);
                output.number = number_name(value.number);
                output.gender = gender_name(value.gender);
                output.numeral_type = numeral_type_name(value.numeral_type);
            } else if constexpr (std::is_same_v<Value,
                                                words::AdverbMorphology>) {
                output.kind = "adverb";
                output.degree = degree_name(value.degree);
            } else if constexpr (std::is_same_v<Value,
                                                words::VerbMorphology>) {
                output.kind = "verb";
                output.conjugation = value.conjugation;
                output.variant = value.variant;
                output.tense = tense_name(value.tense);
                output.voice = voice_name(value.voice);
                output.mood = mood_name(value.mood);
                output.person = value.person;
                output.number = number_name(value.number);
            } else if constexpr (std::is_same_v<Value,
                                                words::ParticipleMorphology>) {
                output.kind = "participle";
                output.conjugation = value.conjugation;
                output.variant = value.variant;
                output.grammatical_case = case_name(value.grammatical_case);
                output.number = number_name(value.number);
                output.gender = gender_name(value.gender);
                output.tense = tense_name(value.tense);
                output.voice = voice_name(value.voice);
            } else if constexpr (std::is_same_v<Value,
                                                words::SupineMorphology>) {
                output.kind = "supine";
                output.conjugation = value.conjugation;
                output.variant = value.variant;
                output.grammatical_case = case_name(value.grammatical_case);
                output.number = number_name(value.number);
                output.gender = gender_name(value.gender);
            } else if constexpr (std::is_same_v<
                                     Value, words::PrepositionMorphology>) {
                output.kind = "preposition";
                output.governs = case_name(value.governs);
            } else {
                output.kind = lexical_part == words::PartOfSpeech::interjection
                                  ? "interjection"
                                  : "conjunction";
            }
        },
        morphology);
    return output;
}

[[nodiscard]] BrowserLexicalFlags
browser_lexical_flags(const words::LexemeRecord &lexeme) {
    BrowserLexicalFlags flags;
    flags.dictionary = lexeme.dictionary == words::DictionaryKind::unique
                           ? "unique"
                           : "general";
    flags.entry_id = lexeme.dictionary_entry + 1U;
    flags.part_of_speech = lexical_part_name(lexeme.part_of_speech);
    flags.age = age_name(lexeme.age);
    flags.subject = subject_name(lexeme.subject);
    flags.geography = geography_name(lexeme.geography);
    flags.frequency = lexical_frequency_name(lexeme.frequency);
    flags.source = source_name(lexeme.source);

    switch (std::to_underlying(lexeme.part_of_speech)) {
    case std::to_underlying(words::PartOfSpeech::noun):
        flags.declension = lexeme.declension;
        flags.variant = lexeme.variant;
        flags.gender = gender_name(lexeme.gender);
        flags.noun_kind = noun_kind_name(lexeme.noun_kind);
        break;
    case std::to_underlying(words::PartOfSpeech::pronoun):
    case std::to_underlying(words::PartOfSpeech::pack):
        flags.declension = lexeme.declension;
        flags.variant = lexeme.variant;
        flags.pronoun_kind = pronoun_kind_name(lexeme.pronoun_kind);
        break;
    case std::to_underlying(words::PartOfSpeech::adjective):
        flags.declension = lexeme.declension;
        flags.variant = lexeme.variant;
        flags.degree = degree_name(lexeme.adjective_degree);
        break;
    case std::to_underlying(words::PartOfSpeech::numeral):
        flags.declension = lexeme.declension;
        flags.variant = lexeme.variant;
        flags.numeral_type = numeral_type_name(lexeme.numeral_type);
        flags.numeral_value = lexeme.numeral_value;
        break;
    case std::to_underlying(words::PartOfSpeech::adverb):
        flags.degree = degree_name(lexeme.adverb_degree);
        break;
    case std::to_underlying(words::PartOfSpeech::verb):
        flags.conjugation = lexeme.declension;
        flags.variant = lexeme.variant;
        flags.verb_kind = verb_kind_name(lexeme.verb_kind);
        break;
    case std::to_underlying(words::PartOfSpeech::preposition):
        flags.governs = case_name(lexeme.governs);
        break;
    default:
        break;
    }
    return flags;
}

void append_derivation(BrowserSearchHit &hit,
                       const words::DerivationIR &derivation) {
    hit.addon_ids.reserve(derivation.count);
    std::ranges::transform(derivation.steps(),
                           std::back_inserter(hit.addon_ids),
                           [](const words::AddonId id) { return id.value(); });
    if (derivation.rewritten_form) {
        hit.rewrite_ids.reserve(derivation.rewritten_form->count);
        std::ranges::transform(
            derivation.rewritten_form->steps(),
            std::back_inserter(hit.rewrite_ids),
            [](const words::RewriteId id) { return id.value(); });
    }
}

[[nodiscard]] BrowserSearchHit
browser_hit(const words::Database &database, const words::SurfaceForm &surface,
            const words::AnalysisIR &analysis, const bool include_meaning) {
    BrowserSearchHit hit;
    hit.lexeme_id = analysis.lexeme.value();
    hit.has_rule = analysis.rule.has_value();
    if (analysis.rule) {
        hit.rule_id = analysis.rule->value();
        const auto &rule = database.rule(*analysis.rule);
        hit.rule = {true, age_name(rule.age), rule_frequency_name(rule.frequency)};
    }
    const auto &lexeme = database.lexeme(analysis.lexeme);
    hit.lemma = words::citation_lemma(database, lexeme, surface.normalized_nfc);
    if (include_meaning) {
        hit.has_meaning = true;
        hit.meaning = normalized_meaning(database.meaning(lexeme.meaning));
    }
    hit.morphology = browser_morphology(analysis.morphology,
                                        lexeme.part_of_speech);
    hit.part_of_speech = hit.morphology.kind;
    hit.lexical = browser_lexical_flags(lexeme);
    append_derivation(hit, analysis.derivation);
    return hit;
}

[[nodiscard]] BrowserSearchHit
browser_hit(const words::Database &database, const words::SurfaceForm &surface,
            const words::CompoundAnalysisIR &analysis,
            const bool include_meaning) {
    BrowserSearchHit hit;
    hit.lexeme_id = analysis.lexeme.value();
    hit.has_rule = analysis.source_rule.has_value();
    if (analysis.source_rule) {
        hit.rule_id = analysis.source_rule->value();
        const auto &rule = database.rule(*analysis.source_rule);
        hit.rule = {true, age_name(rule.age), rule_frequency_name(rule.frequency)};
    }
    const auto &lexeme = database.lexeme(analysis.lexeme);
    hit.lemma = words::citation_lemma(database, lexeme, surface.normalized_nfc);
    if (include_meaning) {
        hit.has_meaning = true;
        hit.meaning = normalized_meaning(database.meaning(lexeme.meaning));
    }
    hit.morphology = browser_morphology(analysis.morphology,
                                        words::PartOfSpeech::verb);
    hit.part_of_speech = "verb";
    hit.lexical = browser_lexical_flags(lexeme);
    append_derivation(hit, analysis.source_derivation);
    hit.compound = true;
    hit.compound_construction = compound_kind_name(analysis.kind);
    hit.compound_auxiliary = analysis.auxiliary;
    return hit;
}

[[nodiscard]] bool search_hit_less(const BrowserSearchHit &left,
                                   const BrowserSearchHit &right) {
    if (left.has_lexeme != right.has_lexeme) {
        return left.has_lexeme;
    }
    return left < right;
}

void canonicalize_hits(std::vector<BrowserSearchHit> &hits) {
    std::ranges::sort(hits, search_hit_less);
    const auto unique = std::ranges::unique(hits).begin();
    hits.erase(unique, hits.end());
}

[[nodiscard]] BrowserSearchResult
browser_search_result(const words::Engine &engine,
                      const words::QueryResult &result,
                      const bool include_meanings) {
    BrowserSearchResult output;
    if (include_meanings) {
        output.schema = "whitakers-words.analysis";
    }
    output.dataset_id = engine.dataset_id();
    output.query.text = result.multi_token_query
                            ? result.multi_token_query->original_utf8
                            : result.surface.original_utf8;
    output.query.normalized = result.multi_token_query
                                  ? result.multi_token_query->normalized_nfc
                                  : result.surface.normalized_nfc;
    output.status = status_name(result.status);

    if (result.status == words::QueryStatus::analyzed) {
        output.hits.reserve(result.analyses.size() +
                            result.compound_analyses.size() +
                            result.artificial_analyses.size());
        for (const auto &analysis : result.analyses) {
            output.hits.push_back(
                browser_hit(engine.database(), result.surface, analysis,
                            include_meanings));
        }
        for (const auto &analysis : result.compound_analyses) {
            output.hits.push_back(
                browser_hit(engine.database(), result.surface, analysis,
                            include_meanings));
        }
        for (const auto &artificial : result.artificial_analyses) {
            std::visit(
                [&](const auto &analysis) {
                    BrowserSearchHit hit;
                    hit.has_lexeme = false;
                    hit.part_of_speech = "numeral";
                    hit.morphology.kind = "numeral";
                    hit.artificial = true;
                    hit.artificial_method = "roman-numeral";
                    hit.artificial_value = analysis.value;
                    hit.artificial_well_formed = analysis.well_formed;
                    if (include_meanings) {
                        hit.has_meaning = true;
                        hit.meaning = std::to_string(analysis.value);
                        hit.meaning.append(analysis.well_formed
                                               ? " as a Roman numeral"
                                               : " as an ill-formed Roman numeral");
                    }
                    append_derivation(hit, analysis.derivation);
                    output.hits.push_back(std::move(hit));
                },
                artificial);
        }
        canonicalize_hits(output.hits);
    }

    output.diagnostics.reserve(result.diagnostics.size());
    std::ranges::transform(
        result.diagnostics, std::back_inserter(output.diagnostics),
        [](const words::Diagnostic &diagnostic) {
            return BrowserDiagnostic{diagnostic.code, diagnostic.severity,
                                     diagnostic.part_of_speech};
        });

    if (result.two_word_suggestion) {
        BrowserSearchSuggestion suggestion;
        suggestion.split_at = result.two_word_suggestion->logical_split;
        suggestion.classification =
            result.two_word_suggestion->both_contain_numeral
                ? "number-pair"
                : "unconstrained";
        suggestion.segments.reserve(
            result.two_word_suggestion->segments.size());
        for (const auto &segment : result.two_word_suggestion->segments) {
            BrowserSearchSegment projected;
            projected.text = segment.surface.normalized_nfc;
            projected.hits.reserve(segment.analyses.size());
            for (const auto &analysis : segment.analyses) {
                projected.hits.push_back(
                    browser_hit(engine.database(), segment.surface, analysis,
                                include_meanings));
            }
            canonicalize_hits(projected.hits);
            suggestion.segments.push_back(std::move(projected));
        }
        output.suggestions.push_back(std::move(suggestion));
    }
    return output;
}

class BrowserAnalysisEngine final {
  public:
    [[nodiscard]] LoadResult load_database(emscripten::val bytes,
                                           std::string dataset_id) {
        try {
            const auto uint8_array = emscripten::val::global("Uint8Array");
            if (!bytes.instanceof (uint8_array)) {
                return failure("invalid-database-buffer",
                               "database must be a Uint8Array", 0U);
            }

            const auto byte_length = bytes["byteLength"].as<double>();
            if (byte_length < 0.0 ||
                byte_length >
                    static_cast<double>(std::numeric_limits<std::uint32_t>::max())) {
                return failure("database-too-large",
                               "database exceeds the WebAssembly API limit", 0U);
            }
            const auto size = static_cast<std::uint32_t>(byte_length);
            std::vector<std::byte> database_image(size);
            if (!database_image.empty()) {
                auto destination = emscripten::val(emscripten::typed_memory_view(
                    database_image.size(),
                    reinterpret_cast<std::uint8_t *>(database_image.data())));
                destination.call<void>("set", bytes);
            }

            auto candidate = words::Engine::create(
                std::move(database_image),
                words::EngineConfig{std::move(dataset_id)});
            if (!candidate) {
                return failure(candidate.error().code,
                               candidate.error().message, size);
            }

            // WHY: replacement happens only after complete WWDB validation,
            // so a failed reload cannot destroy a working browser session.
            engine_ = std::move(*candidate);
            database_bytes_ = size;
            return LoadResult{true, {}, {}, size, database_kind()};
        } catch (const std::bad_alloc &) {
            return failure("out-of-memory",
                           "not enough WebAssembly memory for the database",
                           0U);
        } catch (const std::exception &error) {
            return failure("database-load-failure", error.what(), 0U);
        }
    }

    [[nodiscard]] bool ready() const noexcept { return engine_ != nullptr; }

    [[nodiscard]] std::string dataset_id() const {
        require_ready();
        return std::string{engine_->dataset_id()};
    }

    [[nodiscard]] std::uint32_t database_bytes() const noexcept {
        return database_bytes_;
    }

    [[nodiscard]] std::string database_kind() const {
        require_ready();
        return std::string{
            engine_->database().content() == words::DatabaseContent::full
                ? full_database_kind
                : search_database_kind};
    }

    [[nodiscard]] BrowserSearchResult analyze(const std::string &utf8,
                                              const bool two_words) const {
        require_ready();
        if (!engine_->supports_full_analysis()) {
            throw std::logic_error{
                "analysis requires a full WWDB with meanings"};
        }
        const auto options = analysis_options(two_words);
        const auto result = engine_->analyze_text(utf8, options);
        return browser_search_result(*engine_, result, true);
    }

    [[nodiscard]] BrowserSearchResult search(const std::string &utf8,
                                             const bool two_words) const {
        require_ready();
        const auto options = analysis_options(two_words);
        const auto result = engine_->analyze_text(utf8, options);
        return browser_search_result(*engine_, result, false);
    }

    void reset() noexcept {
        engine_.reset();
        database_bytes_ = 0U;
    }

  private:
    [[nodiscard]] static LoadResult failure(std::string code,
                                            std::string message,
                                            const std::uint32_t size) {
        return LoadResult{false, std::move(code), std::move(message), size,
                          {}};
    }

    [[nodiscard]] static words::AnalysisOptions
    analysis_options(const bool two_words) noexcept {
        return words::AnalysisOptions{
            two_words ? words::TwoWordsMode::legacy_first_match
                      : words::TwoWordsMode::disabled};
    }

    void require_ready() const {
        if (!engine_) {
            // WHY: querying an unloaded wrapper is API misuse rather than a
            // Latin diagnostic and must not masquerade as analysis-v1 data.
            throw std::logic_error("analysis engine has no loaded database");
        }
    }

    std::unique_ptr<const words::Engine> engine_;
    std::uint32_t database_bytes_{};
};

} // namespace

EMSCRIPTEN_BINDINGS(words_analysis_engine) {
    // WHY: registered vectors keep the WebAssembly boundary typed and let the
    // high-level wrapper copy-and-release their Embind handles explicitly.
    emscripten::register_vector<std::string>("VectorString");
    emscripten::register_vector<std::uint32_t>("VectorUint32");

    emscripten::value_object<LoadResult>("LoadResult")
        .field("ok", &LoadResult::ok)
        .field("code", &LoadResult::code)
        .field("message", &LoadResult::message)
        .field("databaseBytes", &LoadResult::database_bytes)
        .field("databaseKind", &LoadResult::database_kind);

    emscripten::value_object<BrowserQuery>("SearchQuery")
        .field("text", &BrowserQuery::text)
        .field("normalized", &BrowserQuery::normalized)
        .field("mode", &BrowserQuery::mode);

    emscripten::value_object<BrowserDiagnostic>("SearchDiagnostic")
        .field("code", &BrowserDiagnostic::code)
        .field("severity", &BrowserDiagnostic::severity)
        .field("partOfSpeech", &BrowserDiagnostic::part_of_speech);

    emscripten::value_object<BrowserMorphology>("SearchMorphology")
        .field("kind", &BrowserMorphology::kind)
        .field("declension", &BrowserMorphology::declension)
        .field("conjugation", &BrowserMorphology::conjugation)
        .field("variant", &BrowserMorphology::variant)
        .field("case", &BrowserMorphology::grammatical_case)
        .field("number", &BrowserMorphology::number)
        .field("gender", &BrowserMorphology::gender)
        .field("degree", &BrowserMorphology::degree)
        .field("numeralType", &BrowserMorphology::numeral_type)
        .field("tense", &BrowserMorphology::tense)
        .field("voice", &BrowserMorphology::voice)
        .field("mood", &BrowserMorphology::mood)
        .field("person", &BrowserMorphology::person)
        .field("governs", &BrowserMorphology::governs);

    emscripten::value_object<BrowserLexicalFlags>("SearchLexicalFlags")
        .field("dictionary", &BrowserLexicalFlags::dictionary)
        .field("entryId", &BrowserLexicalFlags::entry_id)
        .field("partOfSpeech", &BrowserLexicalFlags::part_of_speech)
        .field("declension", &BrowserLexicalFlags::declension)
        .field("conjugation", &BrowserLexicalFlags::conjugation)
        .field("variant", &BrowserLexicalFlags::variant)
        .field("gender", &BrowserLexicalFlags::gender)
        .field("nounKind", &BrowserLexicalFlags::noun_kind)
        .field("pronounKind", &BrowserLexicalFlags::pronoun_kind)
        .field("degree", &BrowserLexicalFlags::degree)
        .field("numeralType", &BrowserLexicalFlags::numeral_type)
        .field("numeralValue", &BrowserLexicalFlags::numeral_value)
        .field("verbKind", &BrowserLexicalFlags::verb_kind)
        .field("governs", &BrowserLexicalFlags::governs)
        .field("age", &BrowserLexicalFlags::age)
        .field("subject", &BrowserLexicalFlags::subject)
        .field("geography", &BrowserLexicalFlags::geography)
        .field("frequency", &BrowserLexicalFlags::frequency)
        .field("source", &BrowserLexicalFlags::source);

    emscripten::value_object<BrowserRuleFlags>("SearchRuleFlags")
        .field("present", &BrowserRuleFlags::present)
        .field("age", &BrowserRuleFlags::age)
        .field("frequency", &BrowserRuleFlags::frequency);

    emscripten::value_object<BrowserSearchHit>("ResolvedSearchHit")
        .field("hasLexeme", &BrowserSearchHit::has_lexeme)
        .field("lexemeId", &BrowserSearchHit::lexeme_id)
        .field("hasRule", &BrowserSearchHit::has_rule)
        .field("ruleId", &BrowserSearchHit::rule_id)
        .field("addonIds", &BrowserSearchHit::addon_ids)
        .field("rewriteIds", &BrowserSearchHit::rewrite_ids)
        .field("scoreFlags", &BrowserSearchHit::score_flags)
        .field("lemma", &BrowserSearchHit::lemma)
        .field("hasMeaning", &BrowserSearchHit::has_meaning)
        .field("meaning", &BrowserSearchHit::meaning)
        .field("partOfSpeech", &BrowserSearchHit::part_of_speech)
        .field("morphology", &BrowserSearchHit::morphology)
        .field("lexical", &BrowserSearchHit::lexical)
        .field("rule", &BrowserSearchHit::rule)
        .field("compound", &BrowserSearchHit::compound)
        .field("compoundConstruction",
               &BrowserSearchHit::compound_construction)
        .field("compoundAuxiliary", &BrowserSearchHit::compound_auxiliary)
        .field("artificial", &BrowserSearchHit::artificial)
        .field("artificialMethod", &BrowserSearchHit::artificial_method)
        .field("artificialValue", &BrowserSearchHit::artificial_value)
        .field("artificialWellFormed",
               &BrowserSearchHit::artificial_well_formed);
    emscripten::register_vector<BrowserSearchHit>("VectorResolvedSearchHit");

    emscripten::value_object<BrowserSearchSegment>("ResolvedSearchSegment")
        .field("text", &BrowserSearchSegment::text)
        .field("hits", &BrowserSearchSegment::hits);
    emscripten::register_vector<BrowserSearchSegment>(
        "VectorResolvedSearchSegment");

    emscripten::value_object<BrowserSearchSuggestion>(
        "ResolvedSearchSuggestion")
        .field("method", &BrowserSearchSuggestion::method)
        .field("splitAt", &BrowserSearchSuggestion::split_at)
        .field("classification", &BrowserSearchSuggestion::classification)
        .field("segments", &BrowserSearchSuggestion::segments);
    emscripten::register_vector<BrowserSearchSuggestion>(
        "VectorResolvedSearchSuggestion");
    emscripten::register_vector<BrowserDiagnostic>("VectorSearchDiagnostic");

    emscripten::value_object<BrowserSearchResult>("ResolvedSearchResult")
        .field("schema", &BrowserSearchResult::schema)
        .field("schemaVersion", &BrowserSearchResult::schema_version)
        .field("datasetId", &BrowserSearchResult::dataset_id)
        .field("query", &BrowserSearchResult::query)
        .field("status", &BrowserSearchResult::status)
        .field("hits", &BrowserSearchResult::hits)
        .field("diagnostics", &BrowserSearchResult::diagnostics)
        .field("suggestions", &BrowserSearchResult::suggestions);

    emscripten::class_<BrowserAnalysisEngine>("AnalysisEngine")
        .constructor<>()
        .function("loadDatabase", &BrowserAnalysisEngine::load_database)
        .function("ready", &BrowserAnalysisEngine::ready)
        .function("datasetId", &BrowserAnalysisEngine::dataset_id)
        .function("databaseBytes", &BrowserAnalysisEngine::database_bytes)
        .function("databaseKind", &BrowserAnalysisEngine::database_kind)
        .function("analyze", &BrowserAnalysisEngine::analyze)
        .function("search", &BrowserAnalysisEngine::search)
        .function("reset", &BrowserAnalysisEngine::reset);
}
