#include "words/semantics.hpp"

#include <array>
#include <cstddef>
#include <utility>
#include <variant>

namespace words {
namespace {

template <std::size_t Size>
[[nodiscard]] constexpr std::string_view
token(const std::uint8_t ordinal,
      const std::array<std::string_view, Size> &values) noexcept {
    return ordinal < values.size() ? values.at(ordinal) : std::string_view{};
}

template <class Enum, std::size_t Size>
[[nodiscard]] constexpr std::string_view
enum_token(const Enum value,
           const std::array<std::string_view, Size> &values) noexcept {
    return token(static_cast<std::uint8_t>(std::to_underlying(value)), values);
}

} // namespace

std::string_view age_name(const Age value) noexcept {
    constexpr std::array<std::string_view, 9> values{
        "",      "archaic",  "early",     "classical", "late",
        "later", "medieval", "scholarly", "modern"};
    return enum_token(value, values);
}

std::string_view lexical_frequency_name(const LexicalFrequency value) noexcept {
    constexpr std::array<std::string_view, 10> values{
        "",         "very-frequent", "frequent",    "common",   "lesser",
        "uncommon", "very-rare",     "inscription", "graffiti", "pliny"};
    return enum_token(value, values);
}

std::string_view rule_frequency_name(const RuleFrequency value) noexcept {
    constexpr std::array<std::string_view, 10> values{
        "",     "most-frequent", "sometimes",   "uncommon",   "infrequent",
        "rare", "very-rare",     "inscription", "reserved-m", "reserved-n"};
    return enum_token(value, values);
}

std::string_view subject_name(const SubjectArea value) noexcept {
    constexpr std::array<std::string_view, 12> values{"",
                                                      "agriculture",
                                                      "biological-medical",
                                                      "drama-arts",
                                                      "ecclesiastic",
                                                      "grammar-literature",
                                                      "legal-government",
                                                      "poetic",
                                                      "science-philosophy",
                                                      "technical",
                                                      "military",
                                                      "mythology"};
    return enum_token(value, values);
}

std::string_view geography_name(const Geography value) noexcept {
    constexpr std::array<std::string_view, 18> values{
        "",       "africa",       "britain",       "china",  "scandinavia",
        "egypt",  "france-gaul",  "germany",       "greece", "italy-rome",
        "india",  "balkans",      "netherlands",   "persia", "near-east",
        "russia", "spain-iberia", "eastern-europe"};
    return enum_token(value, values);
}

std::string_view source_name(const Source value) noexcept {
    constexpr std::array<std::string_view, 26> values{
        "",
        "source-a",
        "beeson",
        "cassells",
        "adams-latin-sexual-vocabulary",
        "stelten-ecclesiastical-latin",
        "deferrari-aquinas",
        "gildersleeve-lodge",
        "collatinus",
        "leverett",
        "bracton",
        "calepinus-novus",
        "lewis-elementary-latin-dictionary",
        "latham-medieval-word-list",
        "lynn-nelson",
        "oxford-latin-dictionary",
        "souter",
        "other-dictionaries",
        "plater-white",
        "lewis-short",
        "found-in-translation",
        "source-u",
        "saxonis-vademecum",
        "whitaker",
        "temporary",
        "user-submitted"};
    return enum_token(value, values);
}

std::string_view noun_kind_name(const NounKind value) noexcept {
    constexpr std::array<std::string_view, 10> values{
        "",       "singular-only", "plural-only", "abstract",
        "group",  "proper-name",   "person",      "thing",
        "locale", "place"};
    return enum_token(value, values);
}

std::string_view pronoun_kind_name(const PronounKind value) noexcept {
    constexpr std::array<std::string_view, 8> values{
        "",           "personal",      "relative",
        "reflexive",  "demonstrative", "interrogative",
        "indefinite", "adjectival"};
    return enum_token(value, values);
}

std::string_view gender_name(const Gender value) noexcept {
    constexpr std::array<std::string_view, 5> values{
        "", "masculine", "feminine", "neuter", "common"};
    return enum_token(value, values);
}

std::string_view case_name(const GrammaticalCase value) noexcept {
    constexpr std::array<std::string_view, 8> values{
        "",         "nominative", "vocative", "genitive",
        "locative", "dative",     "ablative", "accusative"};
    return enum_token(value, values);
}

std::string_view number_name(const GrammaticalNumber value) noexcept {
    constexpr std::array<std::string_view, 3> values{"", "singular", "plural"};
    return enum_token(value, values);
}

std::string_view degree_name(const Degree value) noexcept {
    constexpr std::array<std::string_view, 4> values{
        "", "positive", "comparative", "superlative"};
    return enum_token(value, values);
}

std::string_view numeral_type_name(const NumeralType value) noexcept {
    constexpr std::array<std::string_view, 5> values{
        "", "cardinal", "ordinal", "distributive", "adverbial"};
    return enum_token(value, values);
}

std::string_view tense_name(const Tense value) noexcept {
    constexpr std::array<std::string_view, 7> values{
        "",        "present",    "imperfect",     "future",
        "perfect", "pluperfect", "future-perfect"};
    return enum_token(value, values);
}

std::string_view voice_name(const Voice value) noexcept {
    constexpr std::array<std::string_view, 3> values{"", "active", "passive"};
    return enum_token(value, values);
}

std::string_view mood_name(const Mood value) noexcept {
    constexpr std::array<std::string_view, 6> values{
        "",           "indicative", "subjunctive",
        "imperative", "infinitive", "participle"};
    return enum_token(value, values);
}

std::string_view verb_kind_name(const VerbKind value) noexcept {
    constexpr std::array<std::string_view, 12> values{"",
                                                      "to-be",
                                                      "compound-of-to-be",
                                                      "governs-genitive",
                                                      "governs-dative",
                                                      "governs-ablative",
                                                      "transitive",
                                                      "intransitive",
                                                      "impersonal",
                                                      "deponent",
                                                      "semideponent",
                                                      "perfect-definite"};
    return enum_token(value, values);
}

std::string_view lexical_part_name(const PartOfSpeech value) noexcept {
    constexpr std::array<std::string_view, 16> values{
        "unknown",      "noun",    "pronoun",     "pronoun",
        "adjective",    "numeral", "adverb",      "verb",
        "verb",         "verb",    "preposition", "conjunction",
        "interjection", "tackon",  "prefix",      "suffix"};
    return enum_token(value, values);
}

std::string_view
morphology_part_name(const Morphology &morphology,
                     const PartOfSpeech invariable_part) noexcept {
    if (std::holds_alternative<NounMorphology>(morphology)) {
        return "noun";
    }
    if (std::holds_alternative<PronounMorphology>(morphology)) {
        return "pronoun";
    }
    if (std::holds_alternative<AdjectiveMorphology>(morphology)) {
        return "adjective";
    }
    if (std::holds_alternative<NumeralMorphology>(morphology)) {
        return "numeral";
    }
    if (std::holds_alternative<AdverbMorphology>(morphology)) {
        return "adverb";
    }
    if (std::holds_alternative<VerbMorphology>(morphology)) {
        return "verb";
    }
    if (std::holds_alternative<ParticipleMorphology>(morphology)) {
        return "participle";
    }
    if (std::holds_alternative<SupineMorphology>(morphology)) {
        return "supine";
    }
    if (std::holds_alternative<PrepositionMorphology>(morphology)) {
        return "preposition";
    }
    return invariable_part == PartOfSpeech::interjection ? "interjection"
                                                         : "conjunction";
}

std::string_view compound_kind_name(const CompoundKind value) noexcept {
    constexpr std::array<std::string_view, 5> values{"", "finite-sum", "esse",
                                                     "fuisse", "iri"};
    return enum_token(value, values);
}

std::string_view status_name(const QueryStatus value) noexcept {
    constexpr std::array<std::string_view, 3> values{"analyzed", "unknown",
                                                     "error"};
    return enum_token(value, values);
}

std::string_view diagnostic_code_name(const DiagnosticCode value) noexcept {
    constexpr std::array<std::string_view, 11> values{
        "empty-input",
        "input-too-large",
        "invalid-utf8",
        "invalid-vowel-quantity",
        "unicode-normalization-failed",
        "unsupported-character",
        "unsupported-part-of-speech",
        "unsupported-token-count",
        "unsupported-multi-token",
        "unknown-word",
        "two-words-suggestion",
    };
    return enum_token(value, values);
}

std::string_view
diagnostic_severity_name(const DiagnosticSeverity value) noexcept {
    constexpr std::array<std::string_view, 3> values{"info", "warning",
                                                     "error"};
    return enum_token(value, values);
}

std::string_view addon_kind_name(const AddonKind value) noexcept {
    constexpr std::array<std::string_view, 6> values{
        "", "prefix", "suffix", "tickon", "tackon", "packon"};
    return enum_token(value, values);
}

std::string_view rewrite_kind_name(const RewriteKind value) noexcept {
    constexpr std::array<std::string_view, 3> values{"", "syncope",
                                                     "orthographic"};
    return enum_token(value, values);
}

std::string_view rewrite_scope_name(const RewriteScope value) noexcept {
    constexpr std::array<std::string_view, 4> values{"", "initial",
                                                     "internal", "final"};
    return enum_token(value, values);
}

std::string_view
rewrite_operation_name(const RewriteOperation value) noexcept {
    constexpr std::array<std::string_view, 4> values{
        "", "literal", "slur", "double-consonant"};
    return enum_token(value, values);
}

std::string_view rewrite_stage_name(const RewriteStage value) noexcept {
    constexpr std::array<std::string_view, 4> values{"", "main", "early",
                                                     "fallback"};
    return enum_token(value, values);
}

std::string_view
whitaker_trim_reason_name(const WhitakerTrimReason value) noexcept {
    constexpr std::array<std::string_view, 6> values{
        "unsupported-short-imperative",
        "invalid-imperative-person",
        "impersonal-non-third-person",
        "deponent-active-form",
        "semideponent-passive-present-system",
        "semideponent-active-perfect-system",
    };
    return enum_token(value, values);
}

std::string_view
morphological_notice_name(const MorphologicalNotice value) noexcept {
    constexpr std::array<std::string_view, 3> values{
        "related-passive-usage-attested",
        "source-disagreement",
        "manual-review-recommended",
    };
    return enum_token(value, values);
}

std::string_view
whitaker_trim_mode_name(const WhitakerTrimMode value) noexcept {
    constexpr std::array<std::string_view, 2> values{"annotate", "filter"};
    return enum_token(value, values);
}

std::string_view orthography_mode_name(const OrthographyMode value) noexcept {
    constexpr std::array<std::string_view, 3> values{
        "disabled", "classical-only", "classical-and-medieval"};
    return enum_token(value, values);
}

std::string_view quantity_match_name(const QuantityMatch value) noexcept {
    constexpr std::array<std::string_view, 3> values{"unspecified", "exact",
                                                     "unknown"};
    return enum_token(value, values);
}

std::string normalized_meaning(const std::string_view meaning) {
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

} // namespace words
