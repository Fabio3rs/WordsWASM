#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace words {

template <class Tag> class Id final {
  public:
    using value_type = std::uint32_t;

    constexpr Id() noexcept = default;
    explicit constexpr Id(value_type value) noexcept : value_{value} {}

    [[nodiscard]] constexpr value_type value() const noexcept { return value_; }
    auto operator<=>(const Id &) const = default;

  private:
    value_type value_{};
};

using LexemeId = Id<struct LexemeIdTag>;
using RuleId = Id<struct RuleIdTag>;
using StringId = Id<struct StringIdTag>;
using AddonId = Id<struct AddonIdTag>;
using SuffixStringId = Id<struct SuffixStringIdTag>;
using SuffixMeaningId = Id<struct SuffixMeaningIdTag>;
using PrefixStringId = Id<struct PrefixStringIdTag>;
using PrefixMeaningId = Id<struct PrefixMeaningIdTag>;
using TackonStringId = Id<struct TackonStringIdTag>;
using TackonMeaningId = Id<struct TackonMeaningIdTag>;
using RewriteId = Id<struct RewriteIdTag>;
using RewriteStringId = Id<struct RewriteStringIdTag>;
using RewriteMeaningId = Id<struct RewriteMeaningIdTag>;

enum class DictionaryKind : std::uint8_t {
    general,
    unique,
};

enum class AddonKind : std::uint8_t {
    unknown = 0,
    prefix = 1,
    suffix = 2,
    tickon = 3,
    tackon = 4,
    packon = 5,
};

enum class RewriteKind : std::uint8_t {
    syncope = 1,
    orthographic = 2,
};

enum class RewriteScope : std::uint8_t {
    initial = 1,
    internal = 2,
    final = 3,
};

enum class RewriteOperation : std::uint8_t {
    literal = 1,
    slur = 2,
    double_consonant = 3,
};

enum class RewriteStage : std::uint8_t {
    main = 1,
    early = 2,
    fallback = 3,
};

enum class RewriteConstraint : std::uint8_t {
    none = 0,
    eo_verb = 1,
    adjective_iis = 2,
};

enum class PartOfSpeech : std::uint8_t {
    unknown = 0,
    noun = 1,
    pronoun = 2,
    pack = 3,
    adjective = 4,
    numeral = 5,
    adverb = 6,
    verb = 7,
    participle = 8,
    supine = 9,
    preposition = 10,
    conjunction = 11,
    interjection = 12,
    tackon = 13,
    prefix = 14,
    suffix = 15,
};

enum class Gender : std::uint8_t {
    unknown = 0,
    masculine = 1,
    feminine = 2,
    neuter = 3,
    common = 4,
};

// Closed lexical domains copied from the Ada WORDS types.  Their ordinals
// intentionally match the WWDB wire codes, but consumers must use the
// semantic types rather than depending on that representation detail.
enum class NounKind : std::uint8_t {
    unknown = 0,
    singular_only = 1,
    plural_only = 2,
    abstract = 3,
    group = 4,
    proper_name = 5,
    person = 6,
    thing = 7,
    locale = 8,
    place = 9,
};

enum class Age : std::uint8_t {
    unknown = 0,
    archaic = 1,
    early = 2,
    classical = 3,
    late = 4,
    later = 5,
    medieval = 6,
    scholarly = 7,
    modern = 8,
};

enum class SubjectArea : std::uint8_t {
    unknown = 0,
    agriculture = 1,
    biological_medical = 2,
    drama_arts = 3,
    ecclesiastic = 4,
    grammar_literature = 5,
    legal_government = 6,
    poetic = 7,
    science_philosophy = 8,
    technical = 9,
    military = 10,
    mythology = 11,
};

enum class Geography : std::uint8_t {
    unknown = 0,
    africa = 1,
    britain = 2,
    china = 3,
    scandinavia = 4,
    egypt = 5,
    france_gaul = 6,
    germany = 7,
    greece = 8,
    italy_rome = 9,
    india = 10,
    balkans = 11,
    netherlands = 12,
    persia = 13,
    near_east = 14,
    russia = 15,
    spain_iberia = 16,
    eastern_europe = 17,
};

enum class LexicalFrequency : std::uint8_t {
    unknown = 0,
    very_frequent = 1,
    frequent = 2,
    common = 3,
    lesser = 4,
    uncommon = 5,
    very_rare = 6,
    inscription = 7,
    graffiti = 8,
    pliny = 9,
};

// WORDS reuses the same legacy storage codes for a different inflection-rule
// vocabulary.  Keeping a separate type prevents lexical and rule frequency
// from being mixed accidentally.
enum class RuleFrequency : std::uint8_t {
    unknown = 0,
    most_frequent = 1,
    sometimes = 2,
    uncommon = 3,
    infrequent = 4,
    rare = 5,
    very_rare = 6,
    inscription = 7,
    reserved_m = 8,
    reserved_n = 9,
};

enum class Source : std::uint8_t {
    unknown = 0,
    source_a = 1,
    beeson = 2,
    cassells = 3,
    adams_latin_sexual_vocabulary = 4,
    stelten_ecclesiastical_latin = 5,
    deferrari_aquinas = 6,
    gildersleeve_lodge = 7,
    collatinus = 8,
    leverett = 9,
    bracton = 10,
    calepinus_novus = 11,
    lewis_elementary_latin_dictionary = 12,
    latham_medieval_word_list = 13,
    lynn_nelson = 14,
    oxford_latin_dictionary = 15,
    souter = 16,
    other_dictionaries = 17,
    plater_white = 18,
    lewis_short = 19,
    found_in_translation = 20,
    source_u = 21,
    saxonis_vademecum = 22,
    whitaker = 23,
    temporary = 24,
    user_submitted = 25,
};

enum class GrammaticalCase : std::uint8_t {
    unknown = 0,
    nominative = 1,
    vocative = 2,
    genitive = 3,
    locative = 4,
    dative = 5,
    ablative = 6,
    accusative = 7,
};

enum class GrammaticalNumber : std::uint8_t {
    unknown = 0,
    singular = 1,
    plural = 2,
};

enum class Person : std::uint8_t {
    unknown = 0,
    first = 1,
    second = 2,
    third = 3,
};

enum class Degree : std::uint8_t {
    unknown = 0,
    positive = 1,
    comparative = 2,
    superlative = 3,
};

enum class PronounKind : std::uint8_t {
    unknown = 0,
    personal = 1,
    relative = 2,
    reflexive = 3,
    demonstrative = 4,
    interrogative = 5,
    indefinite = 6,
    adjectival = 7,
};

enum class NumeralType : std::uint8_t {
    unknown = 0,
    cardinal = 1,
    ordinal = 2,
    distributive = 3,
    adverbial = 4,
};

enum class Tense : std::uint8_t {
    unknown = 0,
    present = 1,
    imperfect = 2,
    future = 3,
    perfect = 4,
    pluperfect = 5,
    future_perfect = 6,
};

enum class Voice : std::uint8_t {
    unknown = 0,
    active = 1,
    passive = 2,
};

enum class Mood : std::uint8_t {
    unknown = 0,
    indicative = 1,
    subjunctive = 2,
    imperative = 3,
    infinitive = 4,
    participle = 5,
};

enum class VerbKind : std::uint8_t {
    unknown = 0,
    to_be = 1,
    compound_of_to_be = 2,
    governs_genitive = 3,
    governs_dative = 4,
    governs_ablative = 5,
    transitive = 6,
    intransitive = 7,
    impersonal = 8,
    deponent = 9,
    semideponent = 10,
    perfect_definite = 11,
};

enum class VowelQuantity : std::uint8_t {
    unknown = 0,
    short_vowel = 1,
    long_vowel = 2,
};

enum class QuantityMatch : std::uint8_t {
    unspecified,
    exact,
    unknown,
};

struct QuantityMask final {
    std::uint32_t known{};
    std::uint32_t long_vowel{};
};

enum class QueryStatus : std::uint8_t {
    analyzed,
    unknown,
    error,
};

enum class TwoWordsMode : std::uint8_t {
    disabled,
    legacy_first_match,
};

enum class CompoundKind : std::uint8_t {
    unknown = 0,
    finite_sum = 1,
    esse = 2,
    fuisse = 3,
    iri = 4,
};

struct SurfaceRange final {
    std::uint32_t begin{};
    std::uint32_t count{};
};

struct SurfaceForm final {
    std::string original_utf8;
    std::string normalized_nfc;
    // One byte per logical letter while preserving i/j and u/v. Rewrite rules
    // need that distinction even though dictionary lookup intentionally folds
    // it.
    std::string orthography_ascii;
    std::string lookup_ascii;
    std::vector<VowelQuantity> quantities;
    std::vector<std::uint32_t> nfc_byte_offsets;

    [[nodiscard]] std::string_view slice(SurfaceRange range) const noexcept;
};

struct CandidateIR final {
    RuleId rule;
    SurfaceRange stem;
    SurfaceRange ending;
};

struct NounMorphology final {
    std::uint8_t declension{};
    std::uint8_t variant{};
    GrammaticalCase grammatical_case{GrammaticalCase::unknown};
    GrammaticalNumber number{GrammaticalNumber::unknown};
    Gender gender{Gender::unknown};
};

struct AdjectiveMorphology final {
    std::uint8_t declension{};
    std::uint8_t variant{};
    GrammaticalCase grammatical_case{GrammaticalCase::unknown};
    GrammaticalNumber number{GrammaticalNumber::unknown};
    Gender gender{Gender::unknown};
    Degree degree{Degree::unknown};
};

struct PronounMorphology final {
    std::uint8_t declension{};
    std::uint8_t variant{};
    GrammaticalCase grammatical_case{GrammaticalCase::unknown};
    GrammaticalNumber number{GrammaticalNumber::unknown};
    Gender gender{Gender::unknown};
};

struct NumeralMorphology final {
    std::uint8_t declension{};
    std::uint8_t variant{};
    GrammaticalCase grammatical_case{GrammaticalCase::unknown};
    GrammaticalNumber number{GrammaticalNumber::unknown};
    Gender gender{Gender::unknown};
    NumeralType numeral_type{NumeralType::unknown};
};

struct AdverbMorphology final {
    Degree degree{Degree::unknown};
};

struct VerbMorphology final {
    std::uint8_t conjugation{};
    std::uint8_t variant{};
    Tense tense{Tense::unknown};
    Voice voice{Voice::unknown};
    Mood mood{Mood::unknown};
    Person person{Person::unknown};
    GrammaticalNumber number{GrammaticalNumber::unknown};
};

struct ParticipleMorphology final {
    std::uint8_t conjugation{};
    std::uint8_t variant{};
    GrammaticalCase grammatical_case{GrammaticalCase::unknown};
    GrammaticalNumber number{GrammaticalNumber::unknown};
    Gender gender{Gender::unknown};
    Tense tense{Tense::unknown};
    Voice voice{Voice::unknown};
};

struct SupineMorphology final {
    std::uint8_t conjugation{};
    std::uint8_t variant{};
    GrammaticalCase grammatical_case{GrammaticalCase::unknown};
    GrammaticalNumber number{GrammaticalNumber::unknown};
    Gender gender{Gender::unknown};
};

struct PrepositionMorphology final {
    GrammaticalCase governs{GrammaticalCase::unknown};
};

struct InvariableMorphology final {};

using Morphology =
    std::variant<NounMorphology, PronounMorphology, AdjectiveMorphology,
                 NumeralMorphology, AdverbMorphology, VerbMorphology,
                 ParticipleMorphology, SupineMorphology, PrepositionMorphology,
                 InvariableMorphology>;

struct RewrittenFormIR final {
    // Orthography may recover a form that still needs one syncope pass. The
    // Ada pipeline never recursively schedules arbitrary rewrites, so two IDs
    // express the real bound without a per-analysis vector allocation.
    std::array<RewriteId, 2> rules{};
    std::uint8_t count{};
    std::uint8_t leading_addon_count{};
    std::string stem;
    std::string ending;

    [[nodiscard]] std::span<const RewriteId> steps() const noexcept {
        return std::span<const RewriteId>{rules}.first(
            std::min<std::size_t>(count, rules.size()));
    }
};

struct DerivationIR final {
    // The legacy engine admits at most one prefix, one suffix and one tackon.
    // A fixed-capacity path keeps the common regular analysis allocation-free.
    std::array<AddonId, 3> addon_ids{};
    std::uint8_t count{};
    std::optional<RewrittenFormIR> rewritten_form;

    [[nodiscard]] std::span<const AddonId> steps() const noexcept {
        return std::span<const AddonId>{addon_ids}.first(
            std::min<std::size_t>(count, addon_ids.size()));
    }
};

struct AnalysisIR final {
    LexemeId lexeme;
    // UNIQUES carries a complete analysis and therefore has no inflection
    // rule.  Optionality models that domain fact without reserving a fake ID.
    std::optional<RuleId> rule;
    std::uint8_t stem_key{};
    SurfaceRange stem;
    SurfaceRange ending;
    Morphology morphology;
    QuantityMatch quantity_match{QuantityMatch::unspecified};
    DerivationIR derivation;
};

struct CompoundAnalysisIR final {
    // The persistent identity belongs to the participle/supine that licensed
    // the construction.  The resulting verb form is computed by the Latin
    // grammar and therefore does not manufacture a WWDB inflection rule.
    LexemeId lexeme;
    std::optional<RuleId> source_rule;
    VerbMorphology morphology;
    DerivationIR source_derivation;
    DerivationIR auxiliary_derivation;
    CompoundKind kind{CompoundKind::finite_sum};
    Tense source_tense{Tense::unknown};
    Voice source_voice{Voice::unknown};
    std::string auxiliary;
};

struct WordSegmentIR final {
    // Each side is lexed independently because its analysis ranges are local
    // to that side.  Owning the two successful surfaces keeps those ranges
    // valid without rewriting offsets into the speculative parent spelling.
    SurfaceForm surface;
    std::vector<AnalysisIR> analyses;
};

struct TwoWordSuggestionIR final {
    // The legacy heuristic accepts only its first successful split.  A fixed
    // pair expresses that real bound and prevents this recovery hint from
    // becoming an unbounded phrase parser by accident.
    std::uint32_t logical_split{};
    std::array<WordSegmentIR, 2> segments;
    bool both_contain_numeral{};
};

struct RomanNumeralIR final {
    std::uint32_t value{};
    bool well_formed{};
    SurfaceRange stem;
    DerivationIR derivation;
};

using ArtificialAnalysisIR = std::variant<RomanNumeralIR>;

struct Diagnostic final {
    std::string code;
    std::string severity;
    std::string part_of_speech;
};

struct MultiTokenQueryIR final {
    std::string original_utf8;
    std::string normalized_nfc;
};

struct QueryResult final {
    SurfaceForm surface;
    // Single-word analysis continues to use SurfaceForm directly.  Only the
    // bounded two-token API allocates these strings, keeping ranges in the IR
    // anchored to the first token while exposing the complete public query.
    std::optional<MultiTokenQueryIR> multi_token_query;
    QueryStatus status{QueryStatus::unknown};
    std::vector<AnalysisIR> analyses;
    std::vector<CompoundAnalysisIR> compound_analyses;
    // Two_Words is deliberately a suggestion rather than a lexical hit: the
    // Ada documentation warns that mechanically successful splits are often
    // false.  Keeping it separate lets status remain unknown and prevents
    // search consumers from treating both segments as one confirmed lexeme.
    std::optional<TwoWordSuggestionIR> two_word_suggestion;
    // Query-dependent analyses have no stable dataset identity.  Keeping
    // them outside the lexical vector prevents accidental sentinel IDs from
    // leaking into lookup, sorting, or the compact search contract.
    std::vector<ArtificialAnalysisIR> artificial_analyses;
    std::vector<Diagnostic> diagnostics;
};

} // namespace words
