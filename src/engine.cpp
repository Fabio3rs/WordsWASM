#include "words/engine.hpp"
#include "words/artificial.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <variant>

namespace words {
namespace {

constexpr std::string_view primary_enclitic{"que"};
constexpr std::string_view esse_auxiliary{"esse"};
constexpr std::string_view fuisse_auxiliary{"fuisse"};
constexpr std::string_view iri_auxiliary{"iri"};
constexpr std::string_view dataset_id_prefix{"sha256:"};
constexpr std::size_t sha256_hex_digit_count{64U};

constexpr std::uint8_t indeclinable_paradigm{9U};
// LexemeRecord stores the legacy conjugation digit in its shared declension
// field for verbs. Keep that representation detail at the comparison boundary.
constexpr std::uint8_t sum_lexeme_conjugation{5U};
constexpr std::uint8_t sum_lexeme_variant{1U};
constexpr std::uint8_t eo_conjugation{6U};
constexpr std::uint8_t eo_variant{1U};

constexpr std::size_t morphology_key_width{12U};
using MorphologyKey = std::array<std::uint8_t, morphology_key_width>;
enum class MorphologyKeyKind : std::uint8_t {
    noun,
    pronoun,
    adjective,
    numeral,
    adverb,
    verb,
    participle,
    supine,
    preposition,
    invariable,
};

constexpr std::size_t maximum_ending_length{7U};
constexpr std::size_t ending_length_count{maximum_ending_length + 1U};

constexpr std::array<std::string_view, 11> two_words_common_prefixes{
    "dis", "ex", "in",  "per",   "prae",  "pro",
    "re",  "si", "sub", "super", "trans",
};

[[nodiscard]] bool is_lower_hex(const char value) noexcept {
    return (value >= '0' && value <= '9') || (value >= 'a' && value <= 'f');
}

[[nodiscard]] bool coarse_part_matches(const PartOfSpeech lexical,
                                       const PartOfSpeech inflected) noexcept {
    if (lexical == inflected) {
        return true;
    }
    if (lexical == PartOfSpeech::verb &&
        (inflected == PartOfSpeech::participle ||
         inflected == PartOfSpeech::supine)) {
        return true;
    }
    return false;
}

[[nodiscard]] bool paradigm_matches(const LexemeRecord &lexeme,
                                    const InflectionRule &rule) noexcept {
    if (rule.declension == 0U && rule.variant == 0U) {
        return lexeme.declension != indeclinable_paradigm;
    }
    return rule.declension == lexeme.declension &&
           (rule.variant == 0U || rule.variant == lexeme.variant);
}

[[nodiscard]] bool stem_key_matches(const StemReference &stem,
                                    const InflectionRule &rule) noexcept {
    return stem.stem_key == rule.stem_key ||
           (stem.stem_key == 0U &&
            (rule.stem_key == 1U || rule.stem_key == 2U));
}

[[nodiscard]] bool
transformed_key_matches(const std::uint8_t target_key,
                        const InflectionRule &rule) noexcept {
    return target_key == rule.stem_key ||
           (target_key == 0U && (rule.stem_key == 1U || rule.stem_key == 2U));
}

[[nodiscard]] bool gender_matches(const Gender lexical,
                                  const Gender inflected) noexcept {
    return inflected == Gender::unknown || inflected == lexical ||
           (inflected == Gender::common &&
            (lexical == Gender::masculine || lexical == Gender::feminine));
}

[[nodiscard]] bool degree_matches(const Degree lexical,
                                  const Degree inflected) noexcept {
    return lexical == Degree::unknown || inflected == Degree::unknown ||
           lexical == inflected;
}

[[nodiscard]] constexpr std::pair<std::uint8_t, std::uint8_t>
public_verb_paradigm(const std::uint8_t conjugation,
                     const std::uint8_t variant) noexcept {
    // WHY: (3,4) is the legacy internal key for fourth-conjugation verbs.
    // List_Sweep publishes it as (4,1), so the typed IR must not leak the
    // storage key merely because it no longer passes through that Ada array.
    return conjugation == 3U && variant == 4U
               ? std::pair<std::uint8_t, std::uint8_t>{4U, 1U}
               : std::pair<std::uint8_t, std::uint8_t>{conjugation, variant};
}

[[nodiscard]] Degree adjective_degree(const LexemeRecord &lexeme,
                                      const StemReference &stem) noexcept {
    if (lexeme.adjective_degree != Degree::unknown) {
        return lexeme.adjective_degree;
    }
    switch (stem.stem_key) {
    case 0:
    case 1:
    case 2:
        return Degree::positive;
    case 3:
        return Degree::comparative;
    case 4:
        return Degree::superlative;
    default:
        return Degree::unknown;
    }
}

[[nodiscard]] NumeralType numeral_type(const LexemeRecord &lexeme,
                                       const StemReference &stem) noexcept {
    if (lexeme.numeral_type != NumeralType::unknown) {
        return lexeme.numeral_type;
    }
    switch (stem.stem_key) {
    case 1:
        return NumeralType::cardinal;
    case 2:
        return NumeralType::ordinal;
    case 3:
        return NumeralType::distributive;
    case 4:
        return NumeralType::adverbial;
    default:
        return NumeralType::unknown;
    }
}

[[nodiscard]] Degree adverb_degree(const LexemeRecord &lexeme,
                                   const StemReference &stem) noexcept {
    if (lexeme.adverb_degree != Degree::unknown) {
        return lexeme.adverb_degree;
    }
    switch (stem.stem_key) {
    case 1:
        return Degree::positive;
    case 2:
        return Degree::comparative;
    case 3:
        return Degree::superlative;
    default:
        return Degree::unknown;
    }
}

[[nodiscard]] Degree
transformed_adjective_degree(const SuffixRule &suffix) noexcept {
    if (suffix.target_degree != Degree::unknown) {
        return suffix.target_degree;
    }
    switch (suffix.target_key) {
    case 0:
    case 1:
    case 2:
        return Degree::positive;
    case 3:
        return Degree::comparative;
    case 4:
        return Degree::superlative;
    default:
        return Degree::unknown;
    }
}

[[nodiscard]] NumeralType
transformed_numeral_type(const SuffixRule &suffix) noexcept {
    if (suffix.target_numeral_type != NumeralType::unknown) {
        return suffix.target_numeral_type;
    }
    switch (suffix.target_key) {
    case 1:
        return NumeralType::cardinal;
    case 2:
        return NumeralType::ordinal;
    case 3:
        return NumeralType::distributive;
    case 4:
        return NumeralType::adverbial;
    default:
        return NumeralType::unknown;
    }
}

[[nodiscard]] Degree
transformed_adverb_degree(const SuffixRule &suffix) noexcept {
    if (suffix.target_degree != Degree::unknown) {
        return suffix.target_degree;
    }
    switch (suffix.target_key) {
    case 1:
        return Degree::positive;
    case 2:
        return Degree::comparative;
    case 3:
        return Degree::superlative;
    default:
        return Degree::unknown;
    }
}

[[nodiscard]] bool wildcard_prefix_accepts(const PartOfSpeech part) noexcept {
    return part == PartOfSpeech::noun || part == PartOfSpeech::adjective ||
           part == PartOfSpeech::adverb || part == PartOfSpeech::verb;
}

[[nodiscard]] bool prefix_root_matches(const PrefixRule &prefix,
                                       const PartOfSpeech part) noexcept {
    return prefix.root == part || (prefix.root == PartOfSpeech::unknown &&
                                   wildcard_prefix_accepts(part));
}

[[nodiscard]] bool
transformed_paradigm_matches(const SuffixRule &suffix,
                             const InflectionRule &rule) noexcept {
    if (rule.declension == 0U && rule.variant == 0U) {
        return suffix.target_declension != indeclinable_paradigm;
    }
    return rule.declension == suffix.target_declension &&
           (rule.variant == 0U || rule.variant == suffix.target_variant);
}

[[nodiscard]] bool suffix_root_matches(const LexemeRecord &lexeme,
                                       const StemReference &stem,
                                       const SuffixRule &suffix) noexcept {
    const auto part_matches = suffix.root == PartOfSpeech::unknown ||
                              suffix.root == lexeme.part_of_speech ||
                              (lexeme.part_of_speech == PartOfSpeech::pack &&
                               suffix.root == PartOfSpeech::pronoun);
    const auto key_matches =
        suffix.root_key == 0U || suffix.root_key == stem.stem_key ||
        (stem.stem_key == 0U &&
         (lexeme.part_of_speech == PartOfSpeech::noun ||
          lexeme.part_of_speech == PartOfSpeech::adjective ||
          lexeme.part_of_speech == PartOfSpeech::verb) &&
         (suffix.root_key == 1U || suffix.root_key == 2U));
    return part_matches && key_matches;
}

[[nodiscard]] bool has_quantity(const SurfaceForm &surface) noexcept {
    return std::ranges::any_of(surface.quantities,
                               [](const VowelQuantity quantity) {
                                   return quantity != VowelQuantity::unknown;
                               });
}

[[nodiscard]] std::optional<QuantityMatch>
candidate_quantity_match(const Database &database, const SurfaceForm &surface,
                         const CandidateIR &candidate,
                         const StemReference &stem,
                         const bool surface_has_quantity) noexcept {
    if (!surface_has_quantity) {
        return QuantityMatch::unspecified;
    }

    const auto stem_quantity =
        database.stem_quantity(stem.lexeme, stem.lexical_slot);
    const auto ending_quantity = database.inflection_quantity(candidate.rule);
    bool has_unknown_evidence{};
    for (std::size_t index = 0; index < surface.quantities.size(); ++index) {
        const auto observed = surface.quantities[index];
        if (observed == VowelQuantity::unknown) {
            continue;
        }

        QuantityMask expected;
        std::size_t relative = std::numeric_limits<std::size_t>::max();
        if (index >= candidate.stem.begin &&
            index < candidate.stem.begin + candidate.stem.count) {
            expected = stem_quantity;
            relative = index - candidate.stem.begin;
        } else if (index >= candidate.ending.begin &&
                   index < candidate.ending.begin + candidate.ending.count) {
            expected = ending_quantity;
            relative = index - candidate.ending.begin;
        }

        if (relative >= std::numeric_limits<std::uint32_t>::digits ||
            (expected.known & (std::uint32_t{1U} << relative)) == 0U) {
            has_unknown_evidence = true;
            continue;
        }
        const auto expected_long =
            (expected.long_vowel & (std::uint32_t{1U} << relative)) != 0U;
        const auto observed_long = observed == VowelQuantity::long_vowel;
        if (expected_long != observed_long) {
            return std::nullopt;
        }
    }
    return has_unknown_evidence ? QuantityMatch::unknown : QuantityMatch::exact;
}

[[nodiscard]] MorphologyKey
morphology_key(const Morphology &morphology) noexcept {
    if (const auto *noun = std::get_if<NounMorphology>(&morphology)) {
        return {std::to_underlying(MorphologyKeyKind::noun),
                noun->declension,
                noun->variant,
                std::to_underlying(noun->grammatical_case),
                std::to_underlying(noun->number),
                std::to_underlying(noun->gender)};
    }
    if (const auto *pronoun = std::get_if<PronounMorphology>(&morphology)) {
        return {std::to_underlying(MorphologyKeyKind::pronoun),
                pronoun->declension,
                pronoun->variant,
                std::to_underlying(pronoun->grammatical_case),
                std::to_underlying(pronoun->number),
                std::to_underlying(pronoun->gender)};
    }
    if (const auto *adjective = std::get_if<AdjectiveMorphology>(&morphology)) {
        return {std::to_underlying(MorphologyKeyKind::adjective),
                adjective->declension,
                adjective->variant,
                std::to_underlying(adjective->grammatical_case),
                std::to_underlying(adjective->number),
                std::to_underlying(adjective->gender),
                std::to_underlying(adjective->degree)};
    }
    if (const auto *numeral = std::get_if<NumeralMorphology>(&morphology)) {
        return {std::to_underlying(MorphologyKeyKind::numeral),
                numeral->declension,
                numeral->variant,
                std::to_underlying(numeral->grammatical_case),
                std::to_underlying(numeral->number),
                std::to_underlying(numeral->gender),
                std::to_underlying(numeral->numeral_type)};
    }
    if (const auto *adverb = std::get_if<AdverbMorphology>(&morphology)) {
        return {std::to_underlying(MorphologyKeyKind::adverb),
                std::to_underlying(adverb->degree)};
    }
    if (const auto *verb = std::get_if<VerbMorphology>(&morphology)) {
        return {std::to_underlying(MorphologyKeyKind::verb),
                verb->conjugation,
                verb->variant,
                std::to_underlying(verb->tense),
                std::to_underlying(verb->voice),
                std::to_underlying(verb->mood),
                std::to_underlying(verb->person),
                std::to_underlying(verb->number)};
    }
    if (const auto *participle =
            std::get_if<ParticipleMorphology>(&morphology)) {
        return {std::to_underlying(MorphologyKeyKind::participle),
                participle->conjugation,
                participle->variant,
                std::to_underlying(participle->grammatical_case),
                std::to_underlying(participle->number),
                std::to_underlying(participle->gender),
                std::to_underlying(participle->tense),
                std::to_underlying(participle->voice)};
    }
    if (const auto *supine = std::get_if<SupineMorphology>(&morphology)) {
        return {std::to_underlying(MorphologyKeyKind::supine),
                supine->conjugation,
                supine->variant,
                std::to_underlying(supine->grammatical_case),
                std::to_underlying(supine->number),
                std::to_underlying(supine->gender)};
    }
    if (const auto *preposition =
            std::get_if<PrepositionMorphology>(&morphology)) {
        return {std::to_underlying(MorphologyKeyKind::preposition),
                std::to_underlying(preposition->governs)};
    }
    return {std::to_underlying(MorphologyKeyKind::invariable)};
}

struct EnumerationState final {
    bool unsupported{};
    PartOfSpeech unsupported_part{PartOfSpeech::unknown};

    void mark_unsupported(const PartOfSpeech part) noexcept {
        if (!unsupported) {
            unsupported = true;
            unsupported_part = part;
        }
    }
};

void append_regular_analyses(
    const Database &database, const SurfaceForm &surface,
    const CandidateIR &candidate, const std::span<const StemReference> stems,
    const bool surface_has_quantity, const DerivationIR &derivation,
    std::vector<AnalysisIR> &output, EnumerationState &state) {
    const auto &rule = database.rule(candidate.rule);
    for (const auto &stem : stems) {
        const auto &lexeme = database.lexeme(stem.lexeme);
        if (!coarse_part_matches(lexeme.part_of_speech, rule.part_of_speech) ||
            !stem_key_matches(stem, rule) || !paradigm_matches(lexeme, rule)) {
            continue;
        }
        const auto quantity_match = candidate_quantity_match(
            database, surface, candidate, stem, surface_has_quantity);
        if (!quantity_match) {
            continue;
        }
        if (rule.part_of_speech == PartOfSpeech::noun &&
            lexeme.part_of_speech == PartOfSpeech::noun) {
            if (!gender_matches(lexeme.gender, rule.gender)) {
                continue;
            }
            output.push_back(AnalysisIR{
                .lexeme = stem.lexeme,
                .rule = candidate.rule,
                .stem_key = rule.stem_key,
                .stem = candidate.stem,
                .ending = candidate.ending,
                .morphology =
                    NounMorphology{.declension = lexeme.declension,
                                   .variant = lexeme.variant,
                                   .grammatical_case = rule.grammatical_case,
                                   .number = rule.number,
                                   .gender = lexeme.gender},
                .quantity_match = *quantity_match,
                .derivation = derivation,
                .assessment = {},
            });
            continue;
        }
        if (rule.part_of_speech == PartOfSpeech::adjective &&
            lexeme.part_of_speech == PartOfSpeech::adjective) {
            if (!degree_matches(lexeme.adjective_degree,
                                rule.adjective_degree)) {
                continue;
            }
            output.push_back(AnalysisIR{
                .lexeme = stem.lexeme,
                .rule = candidate.rule,
                .stem_key = rule.stem_key,
                .stem = candidate.stem,
                .ending = candidate.ending,
                .morphology =
                    AdjectiveMorphology{
                        .declension = lexeme.declension,
                        .variant = lexeme.variant,
                        .grammatical_case = rule.grammatical_case,
                        .number = rule.number,
                        .gender = rule.gender,
                        .degree = adjective_degree(lexeme, stem)},
                .quantity_match = *quantity_match,
                .derivation = derivation,
                .assessment = {},
            });
            continue;
        }
        if (rule.part_of_speech == PartOfSpeech::pronoun &&
            lexeme.part_of_speech == PartOfSpeech::pronoun) {
            if (lexeme.declension == 1U) {
                continue; // The dedicated qu-/cu-/aliqu- path owns this key.
            }
            output.push_back(AnalysisIR{
                .lexeme = stem.lexeme,
                .rule = candidate.rule,
                .stem_key = rule.stem_key,
                .stem = candidate.stem,
                .ending = candidate.ending,
                .morphology =
                    PronounMorphology{.declension = lexeme.declension,
                                      .variant = lexeme.variant,
                                      .grammatical_case = rule.grammatical_case,
                                      .number = rule.number,
                                      .gender = rule.gender},
                .quantity_match = *quantity_match,
                .derivation = derivation,
                .assessment = {},
            });
            continue;
        }
        if (rule.part_of_speech == PartOfSpeech::numeral &&
            lexeme.part_of_speech == PartOfSpeech::numeral) {
            output.push_back(AnalysisIR{
                .lexeme = stem.lexeme,
                .rule = candidate.rule,
                .stem_key = rule.stem_key,
                .stem = candidate.stem,
                .ending = candidate.ending,
                .morphology =
                    NumeralMorphology{.declension = lexeme.declension,
                                      .variant = lexeme.variant,
                                      .grammatical_case = rule.grammatical_case,
                                      .number = rule.number,
                                      .gender = rule.gender,
                                      .numeral_type =
                                          numeral_type(lexeme, stem)},
                .quantity_match = *quantity_match,
                .derivation = derivation,
                .assessment = {},
            });
            continue;
        }
        if (rule.part_of_speech == PartOfSpeech::adverb &&
            lexeme.part_of_speech == PartOfSpeech::adverb) {
            if (!degree_matches(lexeme.adverb_degree, rule.adjective_degree)) {
                continue;
            }
            output.push_back(AnalysisIR{
                .lexeme = stem.lexeme,
                .rule = candidate.rule,
                .stem_key = rule.stem_key,
                .stem = candidate.stem,
                .ending = candidate.ending,
                .morphology = AdverbMorphology{adverb_degree(lexeme, stem)},
                .quantity_match = *quantity_match,
                .derivation = derivation,
                .assessment = {},
            });
            continue;
        }
        if (lexeme.part_of_speech == PartOfSpeech::verb &&
            rule.part_of_speech == PartOfSpeech::verb) {
            const auto [conjugation, variant] =
                public_verb_paradigm(lexeme.declension, lexeme.variant);
            output.push_back(AnalysisIR{
                .lexeme = stem.lexeme,
                .rule = candidate.rule,
                .stem_key = rule.stem_key,
                .stem = candidate.stem,
                .ending = candidate.ending,
                .morphology = VerbMorphology{.conjugation = conjugation,
                                             .variant = variant,
                                             .tense = rule.tense,
                                             .voice = rule.voice,
                                             .mood = rule.mood,
                                             .person = rule.person,
                                             .number = rule.number},
                .quantity_match = *quantity_match,
                .derivation = derivation,
                .assessment = {},
            });
            continue;
        }
        if (lexeme.part_of_speech == PartOfSpeech::verb &&
            rule.part_of_speech == PartOfSpeech::participle) {
            output.push_back(AnalysisIR{
                .lexeme = stem.lexeme,
                .rule = candidate.rule,
                .stem_key = rule.stem_key,
                .stem = candidate.stem,
                .ending = candidate.ending,
                .morphology =
                    ParticipleMorphology{.conjugation = lexeme.declension,
                                         .variant = lexeme.variant,
                                         .grammatical_case =
                                             rule.grammatical_case,
                                         .number = rule.number,
                                         .gender = rule.gender,
                                         .tense = rule.tense,
                                         .voice = rule.voice},
                .quantity_match = *quantity_match,
                .derivation = derivation,
                .assessment = {},
            });
            continue;
        }
        if (lexeme.part_of_speech == PartOfSpeech::verb &&
            rule.part_of_speech == PartOfSpeech::supine) {
            output.push_back(AnalysisIR{
                .lexeme = stem.lexeme,
                .rule = candidate.rule,
                .stem_key = rule.stem_key,
                .stem = candidate.stem,
                .ending = candidate.ending,
                .morphology =
                    SupineMorphology{.conjugation = lexeme.declension,
                                     .variant = lexeme.variant,
                                     .grammatical_case = rule.grammatical_case,
                                     .number = rule.number,
                                     .gender = rule.gender},
                .quantity_match = *quantity_match,
                .derivation = derivation,
                .assessment = {},
            });
            continue;
        }
        if (rule.part_of_speech == PartOfSpeech::preposition &&
            lexeme.part_of_speech == PartOfSpeech::preposition) {
            if (lexeme.governs != rule.grammatical_case) {
                continue;
            }
            output.push_back(AnalysisIR{
                .lexeme = stem.lexeme,
                .rule = candidate.rule,
                .stem_key = rule.stem_key,
                .stem = candidate.stem,
                .ending = candidate.ending,
                .morphology = PrepositionMorphology{rule.grammatical_case},
                .quantity_match = *quantity_match,
                .derivation = derivation,
                .assessment = {},
            });
            continue;
        }
        if ((rule.part_of_speech == PartOfSpeech::conjunction ||
             rule.part_of_speech == PartOfSpeech::interjection) &&
            lexeme.part_of_speech == rule.part_of_speech) {
            output.push_back(AnalysisIR{
                .lexeme = stem.lexeme,
                .rule = candidate.rule,
                .stem_key = rule.stem_key,
                .stem = candidate.stem,
                .ending = candidate.ending,
                .morphology = InvariableMorphology{},
                .quantity_match = *quantity_match,
                .derivation = derivation,
                .assessment = {},
            });
            continue;
        }
        state.mark_unsupported(rule.part_of_speech);
    }
}

[[nodiscard]] std::optional<DerivationIR>
append_derivation(const DerivationIR &base,
                  const std::span<const AddonId> additions) noexcept {
    if (base.count + additions.size() > base.addon_ids.size()) {
        return std::nullopt;
    }
    auto result = base;
    for (const auto id : additions) {
        result.addon_ids.at(result.count) = id;
        ++result.count;
    }
    return result;
}

[[nodiscard]] std::string_view
candidate_stem(const SurfaceForm &surface, const SurfaceRange stem) noexcept {
    return std::string_view{surface.lookup_ascii}.substr(stem.begin,
                                                         stem.count);
}

class CandidateRange final {
  public:
    struct EndingMatch final {
        SurfaceRange stem;
        SurfaceRange ending;
        std::span<const RuleId> rules;

        [[nodiscard]] CandidateIR candidate(const RuleId rule) const noexcept {
            return CandidateIR{
                .rule = rule,
                .stem = stem,
                .ending = ending,
            };
        }
    };

    explicit CandidateRange(const SurfaceRange word) noexcept : word_{word} {}

    void append(const std::size_t stem_size, const std::size_t ending_size,
                const std::span<const RuleId> rules) {
        if (rules.empty()) {
            return;
        }
        const auto stem_count = static_cast<std::uint32_t>(stem_size);
        groups_[group_count_++] = EndingMatch{
            .stem = SurfaceRange{.begin = word_.begin, .count = stem_count},
            .ending =
                SurfaceRange{
                    .begin = word_.begin + stem_count,
                    .count = static_cast<std::uint32_t>(ending_size),
                },
            .rules = rules,
        };
    }

    [[nodiscard]] std::span<const EndingMatch>
    groups() const noexcept WORDS_LIFETIMEBOUND {
        return std::span<const EndingMatch>{groups_}.first(group_count_);
    }

  private:
    SurfaceRange word_;
    std::array<EndingMatch, ending_length_count> groups_{};
    std::size_t group_count_{};
};

[[nodiscard]] char normalized_affix_char(char value) noexcept {
    if (value >= 'A' && value <= 'Z') {
        value = static_cast<char>(value - 'A' + 'a');
    }
    if (value == 'j') {
        return 'i';
    }
    if (value == 'v') {
        return 'u';
    }
    return value;
}

[[nodiscard]] bool affix_equal(const std::string_view left,
                               const std::string_view right) noexcept {
    return std::ranges::equal(left, right,
                              [](const char first, const char second) {
                                  return normalized_affix_char(first) ==
                                         normalized_affix_char(second);
                              });
}

[[nodiscard]] bool prefix_matches(const Database &database,
                                  const PrefixRule &prefix,
                                  const std::string_view stem) {
    const auto fix = database.prefix_string(prefix.fix);
    if (std::ranges::any_of(fix, [](const char value) {
            return value >= 'A' && value <= 'Z';
        })) {
        // WHY: uppercase V/X/II records describe Roman-number notation, not
        // productive lowercase Latin prefixes.  The legacy lowercased word
        // path never applies them; treating comparison as case-insensitive
        // invents arbitrary compounds such as x-anthus and x-servus.
        return false;
    }
    return stem.size() > fix.size() &&
           affix_equal(stem.substr(0, fix.size()), fix) &&
           (prefix.connector == '\0' || stem[fix.size()] == prefix.connector);
}

[[nodiscard]] bool suffix_matches(const Database &database,
                                  const SuffixRule &suffix,
                                  const std::string_view stem) {
    const auto fix = database.suffix_string(suffix.fix);
    if (stem.size() <= fix.size() ||
        !affix_equal(stem.substr(stem.size() - fix.size()), fix)) {
        return false;
    }
    return suffix.connector == '\0' ||
           stem[stem.size() - fix.size() - 1U] == suffix.connector;
}

void sort_unique_ids(std::vector<AddonId> &ids) {
    std::ranges::sort(ids);
    const auto duplicate = std::ranges::unique(ids).begin();
    ids.erase(duplicate, ids.end());
}

void add_matching_prefixes(const Database &database,
                           const std::string_view stem,
                           std::vector<AddonId> &ids) {
    const auto maximum = std::min(database.maximum_prefix_size(),
                                  stem.size() - (stem.empty() ? 0U : 1U));
    for (std::size_t size = 1; size <= maximum; ++size) {
        const auto matches = database.lookup_prefix(stem.substr(0, size));
        ids.insert(ids.end(), matches.begin(), matches.end());
    }
}

void add_matching_suffixes(const Database &database,
                           const std::string_view stem,
                           std::vector<AddonId> &ids) {
    const auto maximum = std::min(database.maximum_suffix_size(),
                                  stem.size() - (stem.empty() ? 0U : 1U));
    for (std::size_t size = 1; size <= maximum; ++size) {
        const auto matches =
            database.lookup_suffix(stem.substr(stem.size() - size));
        ids.insert(ids.end(), matches.begin(), matches.end());
    }
}

void append_prefix_analyses(const Database &database,
                            const SurfaceForm &surface,
                            const CandidateRange &candidates,
                            const bool surface_has_quantity,
                            const DerivationIR &initial_derivation,
                            std::vector<AnalysisIR> &output,
                            EnumerationState &state) {
    std::vector<AddonId> prefix_ids;
    for (const auto &group : candidates.groups()) {
        add_matching_prefixes(database, candidate_stem(surface, group.stem),
                              prefix_ids);
    }
    sort_unique_ids(prefix_ids);

    for (const auto prefix_id : prefix_ids) {
        const auto &prefix = database.prefix(prefix_id);
        const auto output_before = output.size();
        const auto unsupported_before = state.unsupported;
        const auto fix_size = database.prefix_string(prefix.fix).size();
        for (const auto &group : candidates.groups()) {
            const auto stem_text = candidate_stem(surface, group.stem);
            if (!prefix_matches(database, prefix, stem_text)) {
                continue;
            }
            const auto base_stems =
                database.lookup_stem(stem_text.substr(fix_size));
            for (const auto rule_id : group.rules) {
                auto projected_candidate = group.candidate(rule_id);
                projected_candidate.stem.begin +=
                    static_cast<std::uint32_t>(fix_size);
                projected_candidate.stem.count -=
                    static_cast<std::uint32_t>(fix_size);
                for (const auto &stem : base_stems) {
                    const auto &lexeme = database.lexeme(stem.lexeme);
                    if (!prefix_root_matches(prefix, lexeme.part_of_speech)) {
                        continue;
                    }
                    const std::array addition{prefix_id};
                    const auto derivation = append_derivation(
                        initial_derivation, std::span<const AddonId>{addition});
                    if (!derivation) {
                        continue;
                    }
                    append_regular_analyses(
                        database, surface, projected_candidate,
                        std::span<const StemReference>{&stem, 1U},
                        surface_has_quantity, *derivation, output, state);
                }
            }
        }
        if (output.size() != output_before ||
            state.unsupported != unsupported_before) {
            return; // The Ada scheduler accepts only the first prefix hit.
        }
    }
}

void append_suffix_semantics(
    const Database &database, const CandidateIR &candidate,
    const SuffixRule &suffix, const std::span<const StemReference> stems,
    const std::optional<AddonId> prefix_id, const QuantityMatch quantity_match,
    const DerivationIR &initial_derivation, std::vector<AnalysisIR> &output,
    EnumerationState &state) {
    const auto &rule = database.rule(candidate.rule);
    if (!coarse_part_matches(suffix.target, rule.part_of_speech) ||
        !transformed_key_matches(suffix.target_key, rule) ||
        !transformed_paradigm_matches(suffix, rule)) {
        return;
    }
    if (prefix_id) {
        const auto &prefix = database.prefix(*prefix_id);
        if (!prefix_root_matches(prefix, suffix.target)) {
            return;
        }
    }

    for (const auto &stem : stems) {
        const auto &lexeme = database.lexeme(stem.lexeme);
        if (!suffix_root_matches(lexeme, stem, suffix)) {
            continue;
        }
        if (database.suffix_string(suffix.fix) == "e" &&
            suffix.root == PartOfSpeech::adjective &&
            suffix.target == PartOfSpeech::adverb &&
            (lexeme.declension != 1U || lexeme.variant != 1U)) {
            // WHY: -e forms adverbs from first/second-declension adjectives.
            // Third-declension adjectives use the -iter family, while the
            // pronominal variant has its own irregular behavior. Accepting
            // key-zero compatibility stems here invents *forte and *sole.
            continue;
        }
        std::array<AddonId, 2> additions{};
        std::size_t addition_count = 0;
        if (prefix_id) {
            additions.at(addition_count++) = *prefix_id;
        }
        additions.at(addition_count++) = suffix.id;
        const auto derivation = append_derivation(
            initial_derivation,
            std::span<const AddonId>{additions}.first(addition_count));
        if (!derivation) {
            continue;
        }
        if (suffix.target == PartOfSpeech::noun &&
            rule.part_of_speech == PartOfSpeech::noun) {
            if (!gender_matches(suffix.target_gender, rule.gender)) {
                continue;
            }
            output.push_back(AnalysisIR{
                .lexeme = stem.lexeme,
                .rule = candidate.rule,
                .stem_key = rule.stem_key,
                .stem = candidate.stem,
                .ending = candidate.ending,
                .morphology =
                    NounMorphology{.declension = suffix.target_declension,
                                   .variant = suffix.target_variant,
                                   .grammatical_case = rule.grammatical_case,
                                   .number = rule.number,
                                   .gender = suffix.target_gender},
                .quantity_match = quantity_match,
                .derivation = *derivation,
                .assessment = {},
            });
            continue;
        }
        if (suffix.target == PartOfSpeech::adjective &&
            rule.part_of_speech == PartOfSpeech::adjective) {
            if (!degree_matches(suffix.target_degree, rule.adjective_degree)) {
                continue;
            }
            output.push_back(AnalysisIR{
                .lexeme = stem.lexeme,
                .rule = candidate.rule,
                .stem_key = rule.stem_key,
                .stem = candidate.stem,
                .ending = candidate.ending,
                .morphology =
                    AdjectiveMorphology{
                        .declension = suffix.target_declension,
                        .variant = suffix.target_variant,
                        .grammatical_case = rule.grammatical_case,
                        .number = rule.number,
                        .gender = rule.gender,
                        .degree = transformed_adjective_degree(suffix)},
                .quantity_match = quantity_match,
                .derivation = *derivation,
                .assessment = {},
            });
            continue;
        }
        if (suffix.target == PartOfSpeech::numeral &&
            rule.part_of_speech == PartOfSpeech::numeral) {
            output.push_back(AnalysisIR{
                .lexeme = stem.lexeme,
                .rule = candidate.rule,
                .stem_key = rule.stem_key,
                .stem = candidate.stem,
                .ending = candidate.ending,
                .morphology =
                    NumeralMorphology{.declension = suffix.target_declension,
                                      .variant = suffix.target_variant,
                                      .grammatical_case = rule.grammatical_case,
                                      .number = rule.number,
                                      .gender = rule.gender,
                                      .numeral_type =
                                          transformed_numeral_type(suffix)},
                .quantity_match = quantity_match,
                .derivation = *derivation,
                .assessment = {},
            });
            continue;
        }
        if (suffix.target == PartOfSpeech::adverb &&
            rule.part_of_speech == PartOfSpeech::adverb) {
            if (!degree_matches(suffix.target_degree, rule.adjective_degree)) {
                continue;
            }
            output.push_back(AnalysisIR{
                .lexeme = stem.lexeme,
                .rule = candidate.rule,
                .stem_key = rule.stem_key,
                .stem = candidate.stem,
                .ending = candidate.ending,
                .morphology =
                    AdverbMorphology{transformed_adverb_degree(suffix)},
                .quantity_match = quantity_match,
                .derivation = *derivation,
                .assessment = {},
            });
            continue;
        }
        if (suffix.target == PartOfSpeech::verb &&
            rule.part_of_speech == PartOfSpeech::verb) {
            const auto [conjugation, variant] = public_verb_paradigm(
                suffix.target_declension, suffix.target_variant);
            output.push_back(AnalysisIR{
                .lexeme = stem.lexeme,
                .rule = candidate.rule,
                .stem_key = rule.stem_key,
                .stem = candidate.stem,
                .ending = candidate.ending,
                .morphology = VerbMorphology{.conjugation = conjugation,
                                             .variant = variant,
                                             .tense = rule.tense,
                                             .voice = rule.voice,
                                             .mood = rule.mood,
                                             .person = rule.person,
                                             .number = rule.number},
                .quantity_match = quantity_match,
                .derivation = *derivation,
                .assessment = {},
            });
            continue;
        }
        if (suffix.target == PartOfSpeech::verb &&
            rule.part_of_speech == PartOfSpeech::participle) {
            output.push_back(AnalysisIR{
                .lexeme = stem.lexeme,
                .rule = candidate.rule,
                .stem_key = rule.stem_key,
                .stem = candidate.stem,
                .ending = candidate.ending,
                .morphology =
                    ParticipleMorphology{
                        .conjugation = suffix.target_declension,
                        .variant = suffix.target_variant,
                        .grammatical_case = rule.grammatical_case,
                        .number = rule.number,
                        .gender = rule.gender,
                        .tense = rule.tense,
                        .voice = rule.voice},
                .quantity_match = quantity_match,
                .derivation = *derivation,
                .assessment = {},
            });
            continue;
        }
        if (suffix.target == PartOfSpeech::verb &&
            rule.part_of_speech == PartOfSpeech::supine) {
            output.push_back(AnalysisIR{
                .lexeme = stem.lexeme,
                .rule = candidate.rule,
                .stem_key = rule.stem_key,
                .stem = candidate.stem,
                .ending = candidate.ending,
                .morphology =
                    SupineMorphology{.conjugation = suffix.target_declension,
                                     .variant = suffix.target_variant,
                                     .grammatical_case = rule.grammatical_case,
                                     .number = rule.number,
                                     .gender = rule.gender},
                .quantity_match = quantity_match,
                .derivation = *derivation,
                .assessment = {},
            });
            continue;
        }
        state.mark_unsupported(rule.part_of_speech);
    }
}

void append_suffix_analyses(
    const Database &database, const SurfaceForm &surface,
    const CandidateRange &candidates, const QuantityMatch quantity_match,
    const DerivationIR &initial_derivation, const bool allow_prefix_fallback,
    std::vector<AnalysisIR> &output, EnumerationState &state) {
    std::vector<AddonId> suffix_ids;
    for (const auto &group : candidates.groups()) {
        add_matching_suffixes(database, candidate_stem(surface, group.stem),
                              suffix_ids);
    }
    sort_unique_ids(suffix_ids);

    for (const auto suffix_id : suffix_ids) {
        const auto &suffix = database.suffix(suffix_id);
        if (!allow_prefix_fallback &&
            (database.suffix_string(suffix.fix) != "e" ||
             PartOfSpeech::adjective != suffix.root ||
             PartOfSpeech::adverb != suffix.target)) {
            // WHY: coexistence with a regular word is a narrow property of
            // the productive adjective-to-adverb -e rule.  Treating every
            // suffix as a concurrent grammar floods ordinary forms with
            // unrelated nominal and adjectival derivations.
            continue;
        }
        const auto suffix_size = database.suffix_string(suffix.fix).size();
        bool dictionary_hit = false;
        for (const auto &group : candidates.groups()) {
            const auto stem_text = candidate_stem(surface, group.stem);
            if (suffix_matches(database, suffix, stem_text) &&
                !database
                     .lookup_stem(
                         stem_text.substr(0, stem_text.size() - suffix_size))
                     .empty()) {
                dictionary_hit = true;
                break;
            }
        }

        if (dictionary_hit) {
            for (const auto &group : candidates.groups()) {
                const auto stem_text = candidate_stem(surface, group.stem);
                if (!suffix_matches(database, suffix, stem_text)) {
                    continue;
                }
                const auto base_stems = database.lookup_stem(
                    stem_text.substr(0, stem_text.size() - suffix_size));
                for (const auto rule_id : group.rules) {
                    append_suffix_semantics(database, group.candidate(rule_id),
                                            suffix, base_stems, std::nullopt,
                                            quantity_match, initial_derivation,
                                            output, state);
                }
            }
            continue;
        }

        if (!allow_prefix_fallback) {
            // WHY: a regular dictionary homograph may coexist with a suffix
            // licensed directly by ADDONS.LAT, but it must not activate the
            // broader prefix-plus-suffix recovery grammar as a side effect.
            continue;
        }

        std::vector<AddonId> prefix_ids;
        for (const auto &group : candidates.groups()) {
            const auto stem_text = candidate_stem(surface, group.stem);
            if (suffix_matches(database, suffix, stem_text)) {
                add_matching_prefixes(
                    database,
                    stem_text.substr(0, stem_text.size() - suffix_size),
                    prefix_ids);
            }
        }
        sort_unique_ids(prefix_ids);
        for (const auto prefix_id : prefix_ids) {
            const auto &prefix = database.prefix(prefix_id);
            const auto output_before = output.size();
            const auto unsupported_before = state.unsupported;
            const auto prefix_size = database.prefix_string(prefix.fix).size();
            for (const auto &group : candidates.groups()) {
                const auto stem_text = candidate_stem(surface, group.stem);
                if (!suffix_matches(database, suffix, stem_text)) {
                    continue;
                }
                const auto without_suffix =
                    stem_text.substr(0, stem_text.size() - suffix_size);
                if (!prefix_matches(database, prefix, without_suffix)) {
                    continue;
                }
                const auto base_stems =
                    database.lookup_stem(without_suffix.substr(prefix_size));
                for (const auto rule_id : group.rules) {
                    auto projected_candidate = group.candidate(rule_id);
                    projected_candidate.stem.begin +=
                        static_cast<std::uint32_t>(prefix_size);
                    projected_candidate.stem.count -=
                        static_cast<std::uint32_t>(prefix_size);
                    append_suffix_semantics(database, projected_candidate,
                                            suffix, base_stems, prefix_id,
                                            quantity_match, initial_derivation,
                                            output, state);
                }
            }
            if (output.size() != output_before ||
                state.unsupported != unsupported_before) {
                break; // One prefix per suffix path, matching Apply_Prefix.
            }
        }
    }
}

[[nodiscard]] CandidateRange enumerate_candidates(const Database &database,
                                                  const SurfaceForm &surface,
                                                  const SurfaceRange word) {
    const auto word_text =
        std::string_view{surface.lookup_ascii}.substr(word.begin, word.count);
    const auto maximum_ending =
        std::min(maximum_ending_length, word_text.size());

    CandidateRange candidates{word};
    for (std::size_t ending_size = maximum_ending;; --ending_size) {
        const auto stem_size = word_text.size() - ending_size;
        const auto ending_text = word_text.substr(stem_size);
        candidates.append(stem_size, ending_size,
                          database.lookup_ending(ending_text));
        if (ending_size == 0U) {
            break;
        }
    }
    return candidates;
}

void append_unique_analyses(const Database &database,
                            const SurfaceForm &surface, const SurfaceRange word,
                            const QuantityMatch quantity_match,
                            const DerivationIR &derivation,
                            std::vector<AnalysisIR> &output) {
    const auto text =
        std::string_view{surface.lookup_ascii}.substr(word.begin, word.count);
    for (const auto &unique : database.lookup_unique(text)) {
        auto morphology = unique.morphology;
        if (auto *pronoun = std::get_if<PronounMorphology>(&morphology);
            pronoun != nullptr && pronoun->declension == 1U) {
            // The legacy qu-/cu-pronoun path treats declension-one variants as
            // implementation keys and omits them from the public analysis.
            pronoun->variant = 0U;
        }
        // A UNIQUES row describes the whole observed form.  Keeping the stem
        // as the complete surface and the ending empty matches Ada and avoids
        // implying a decomposition that the source never asserted.
        output.push_back(AnalysisIR{
            .lexeme = unique.lexeme,
            .rule = std::nullopt,
            .stem_key = 0U,
            .stem = word,
            .ending =
                SurfaceRange{.begin = word.begin + word.count, .count = 0U},
            .morphology = morphology,
            .quantity_match = quantity_match,
            .derivation = derivation,
            .assessment = {},
        });
    }
}

void append_word_analyses(const Database &database, const SurfaceForm &surface,
                          const SurfaceRange word,
                          const QuantityMatch quantity_match,
                          const DerivationIR &initial_derivation,
                          const MorphologicalMechanisms &mechanisms,
                          std::vector<AnalysisIR> &output,
                          EnumerationState &state) {
    // At these call sites, pre-existing analyses are direct hits from UNIQUES
    // or the dedicated qu-pronoun path. They suppress invented prefix/suffix
    // paths but not regular homographs.
    const auto had_direct_analysis = !output.empty();
    const auto output_before = output.size();
    const auto candidates = enumerate_candidates(database, surface, word);
    const auto surface_has_quantity =
        quantity_match != QuantityMatch::unspecified;
    for (const auto &group : candidates.groups()) {
        const auto stems =
            database.lookup_stem(candidate_stem(surface, group.stem));
        for (const auto rule_id : group.rules) {
            append_regular_analyses(database, surface, group.candidate(rule_id),
                                    stems, surface_has_quantity,
                                    initial_derivation, output, state);
        }
    }
    const auto output_after_regular = output.size();
    const auto regular_hit = output_after_regular != output_before;
    const auto regular_has_adverb = std::ranges::any_of(
        std::span<const AnalysisIR>{output}.subspan(
            output_before, output_after_regular - output_before),
        [](const AnalysisIR &analysis) {
            return std::holds_alternative<AdverbMorphology>(
                analysis.morphology);
        });
    if (mechanisms.productive_derivations && mechanisms.prefixes &&
        !had_direct_analysis && !regular_hit && !state.unsupported) {
        append_prefix_analyses(database, surface, candidates,
                               surface_has_quantity, initial_derivation, output,
                               state);
    }
    const auto prefix_hit = output.size() != output_after_regular;
    if (mechanisms.productive_derivations && mechanisms.suffixes &&
        !had_direct_analysis && !prefix_hit && !regular_has_adverb &&
        !state.unsupported) {
        // WHY: Whitaker's suffix grammar enumerates valid derived homographs
        // even when the unsuffixed spelling already has regular analyses.
        // A stored adverb, UNIQUES and dedicated pronoun paths remain
        // suppressors, while another regular part of speech disables only the
        // speculative prefix-plus-suffix path.
        append_suffix_analyses(database, surface, candidates, quantity_match,
                               initial_derivation, !regular_hit, output, state);
    }
}

void add_matching_tackons(const Database &database, const std::string_view word,
                          const bool packons, std::vector<AddonId> &ids) {
    const auto maximum = std::min(database.maximum_tackon_size(),
                                  word.size() - (word.empty() ? 0U : 1U));
    for (std::size_t size = 1; size <= maximum; ++size) {
        const auto text = word.substr(word.size() - size);
        const auto matches = packons ? database.lookup_packon(text)
                                     : database.lookup_tackon(text);
        ids.insert(ids.end(), matches.begin(), matches.end());
    }
    sort_unique_ids(ids);
}

[[nodiscard]] bool has_addon_kind(const Database &database,
                                  const AnalysisIR &analysis,
                                  const AddonKind first,
                                  const AddonKind second = AddonKind::unknown) {
    return std::ranges::any_of(analysis.derivation.steps(),
                               [&](const AddonId id) {
                                   const auto kind = database.addon_kind(id);
                                   return kind == first || kind == second;
                               });
}

[[nodiscard]] bool is_plain_or_outer_tackon(const Database &database,
                                            const AnalysisIR &analysis) {
    if (analysis.derivation.count == 0U) {
        return true;
    }
    if (analysis.derivation.count != 1U) {
        return false;
    }
    // WHY: perfect contraction may occur inside an otherwise complete word
    // followed by one enclitic (imple-vissem-que).  Any other addon means the
    // recovered spelling still required productive lexical invention.
    return database.addon_kind(analysis.derivation.steps().front()) ==
           AddonKind::tackon;
}

[[nodiscard]] bool tackon_accepts(const TackonRule &tackon,
                                  const Morphology &morphology) noexcept {
    if (tackon.base == PartOfSpeech::unknown) {
        return true;
    }
    const auto paradigm_accepts = [&](const std::uint8_t declension,
                                      const std::uint8_t variant) {
        return (tackon.declension == 0U || tackon.declension == declension) &&
               (tackon.variant == 0U || tackon.variant == variant);
    };
    if (tackon.base == PartOfSpeech::noun) {
        const auto *noun = std::get_if<NounMorphology>(&morphology);
        return noun != nullptr &&
               paradigm_accepts(noun->declension, noun->variant);
    }
    if (tackon.base == PartOfSpeech::pronoun) {
        const auto *pronoun = std::get_if<PronounMorphology>(&morphology);
        return pronoun != nullptr &&
               paradigm_accepts(pronoun->declension, pronoun->variant);
    }
    if (tackon.base == PartOfSpeech::adjective) {
        const auto *adjective = std::get_if<AdjectiveMorphology>(&morphology);
        return adjective != nullptr &&
               paradigm_accepts(adjective->declension, adjective->variant);
    }
    return false;
}

void append_qu_pronoun_analyses(const Database &database,
                                const SurfaceForm &surface, SurfaceRange word,
                                QuantityMatch quantity_match,
                                const DerivationIR &derivation,
                                std::vector<AnalysisIR> &output);

void append_tackon_analyses(const Database &database,
                            const SurfaceForm &surface, const SurfaceRange word,
                            const QuantityMatch quantity_match,
                            const bool enclitics, const bool direct_hit,
                            const MorphologicalMechanisms &mechanisms,
                            std::vector<AnalysisIR> &output,
                            EnumerationState &state) {
    const auto word_text =
        std::string_view{surface.lookup_ascii}.substr(word.begin, word.count);
    std::vector<AddonId> ids;
    add_matching_tackons(database, word_text, false, ids);
    for (const auto id : ids) {
        const auto &tackon = database.tackon(id);
        if (tackon.enclitic != enclitics) {
            continue;
        }
        // Once the legacy parser already has a direct analysis, its enclitic
        // pass considers only the first historical entry, -que.  The other
        // three general enclitics are fallbacks for otherwise unknown forms.
        if (enclitics && direct_hit &&
            !affix_equal(database.tackon_string(tackon.fix),
                         primary_enclitic)) {
            continue;
        }
        const auto fix_size = database.tackon_string(tackon.fix).size();
        if (!affix_equal(word_text.substr(word_text.size() - fix_size),
                         database.tackon_string(tackon.fix))) {
            continue;
        }
        DerivationIR derivation;
        derivation.addon_ids.front() = id;
        derivation.count = 1U;
        std::vector<AnalysisIR> derived;
        EnumerationState local_state;
        append_unique_analyses(
            database, surface,
            SurfaceRange{.begin = word.begin,
                         .count =
                             word.count - static_cast<std::uint32_t>(fix_size)},
            quantity_match, derivation, derived);
        append_qu_pronoun_analyses(
            database, surface,
            SurfaceRange{.begin = word.begin,
                         .count =
                             word.count - static_cast<std::uint32_t>(fix_size)},
            quantity_match, derivation, derived);
        append_word_analyses(
            database, surface,
            SurfaceRange{.begin = word.begin,
                         .count =
                             word.count - static_cast<std::uint32_t>(fix_size)},
            quantity_match, derivation, mechanisms, derived, local_state);
        std::erase_if(derived, [&](const AnalysisIR &analysis) {
            return !tackon_accepts(tackon, analysis.morphology);
        });
        if (!derived.empty()) {
            output.insert(output.end(),
                          std::make_move_iterator(derived.begin()),
                          std::make_move_iterator(derived.end()));
            return; // Legacy accepts only the first successful tackon.
        }
        if (local_state.unsupported) {
            state = local_state;
            return;
        }
    }
}

[[nodiscard]] std::optional<RomanNumeralIR>
roman_numeral_with_tackon(const Database &database, const SurfaceForm &surface,
                          const SurfaceRange word) {
    const auto word_text =
        std::string_view{surface.lookup_ascii}.substr(word.begin, word.count);
    std::vector<AddonId> ids;
    add_matching_tackons(database, word_text, false, ids);
    for (const auto id : ids) {
        const auto &tackon = database.tackon(id);
        if (!tackon.enclitic || tackon.base != PartOfSpeech::unknown) {
            continue;
        }
        const auto fix = database.tackon_string(tackon.fix);
        if (!affix_equal(word_text.substr(word_text.size() - fix.size()),
                         fix)) {
            continue;
        }
        const auto base_size =
            word.count - static_cast<std::uint32_t>(fix.size());
        const auto base = std::string_view{surface.normalized_nfc}.substr(
            word.begin, base_size);
        const auto roman =
            analyze_roman_numeral(base, RomanRecognition::permissive);
        if (!roman) {
            continue;
        }
        auto result = *roman;
        // The Ada route reaches numerals through TRICKS_ENCLITIC, whose
        // deliberately cautious public label is “ill-formed” even when the
        // stripped base is independently valid.
        result.well_formed = false;
        result.stem = SurfaceRange{.begin = word.begin, .count = base_size};
        result.derivation.addon_ids.front() = id;
        result.derivation.count = 1U;
        return result;
    }
    return std::nullopt;
}

[[nodiscard]] bool packon_paradigm_accepts(const TackonRule &packon,
                                           const InflectionRule &rule,
                                           const LexemeRecord &lexeme) {
    const auto rule_matches =
        (packon.declension == 0U || packon.declension == rule.declension) &&
        (packon.variant == 0U || packon.variant == rule.variant);
    const auto lexeme_matches =
        lexeme.declension == rule.declension && lexeme.variant == rule.variant;
    return rule_matches && lexeme_matches;
}

void append_packon_analyses(const Database &database,
                            const SurfaceForm &surface, const SurfaceRange word,
                            const QuantityMatch quantity_match,
                            const DerivationIR &initial_derivation,
                            std::vector<AnalysisIR> &output) {
    const auto word_text =
        std::string_view{surface.lookup_ascii}.substr(word.begin, word.count);
    std::vector<AddonId> ids;
    add_matching_tackons(database, word_text, true, ids);
    for (const auto id : ids) {
        const auto &packon = database.tackon(id);
        const auto fix = database.tackon_string(packon.fix);
        if (!affix_equal(word_text.substr(word_text.size() - fix.size()),
                         fix)) {
            continue;
        }
        const SurfaceRange base_word{
            .begin = word.begin,
            .count = word.count - static_cast<std::uint32_t>(fix.size())};
        const auto base_text = std::string_view{surface.lookup_ascii}.substr(
            base_word.begin, base_word.count);
        if (base_text.size() < 3U ||
            (!base_text.starts_with("qu") && !base_text.starts_with("cu"))) {
            continue;
        }
        const std::array addition{id};
        const auto derivation = append_derivation(
            initial_derivation, std::span<const AddonId>{addition});
        if (!derivation) {
            continue;
        }
        const auto candidates =
            enumerate_candidates(database, surface, base_word);
        for (const auto &group : candidates.groups()) {
            std::span<const StemReference> stems;
            bool stems_loaded{};
            for (const auto rule_id : group.rules) {
                const auto &rule = database.rule(rule_id);
                if (rule.part_of_speech != PartOfSpeech::pronoun) {
                    continue;
                }
                if (!stems_loaded) {
                    stems = database.lookup_stem(
                        candidate_stem(surface, group.stem));
                    stems_loaded = true;
                }
                for (const auto &stem : stems) {
                    const auto &lexeme = database.lexeme(stem.lexeme);
                    if (lexeme.part_of_speech != PartOfSpeech::pack ||
                        !packon_paradigm_accepts(packon, rule, lexeme)) {
                        continue;
                    }
                    if (lexeme.required_packon != id) {
                        continue;
                    }
                    output.push_back(AnalysisIR{
                        .lexeme = stem.lexeme,
                        .rule = rule_id,
                        .stem_key = rule.stem_key,
                        .stem = group.stem,
                        .ending = group.ending,
                        .morphology =
                            PronounMorphology{.declension = rule.declension,
                                              .variant = 0U,
                                              .grammatical_case =
                                                  rule.grammatical_case,
                                              .number = rule.number,
                                              .gender = rule.gender},
                        .quantity_match = quantity_match,
                        .derivation = *derivation,
                        .assessment = {},
                    });
                }
            }
        }
    }
}

void append_qu_pronoun_analyses(const Database &database,
                                const SurfaceForm &surface,
                                const SurfaceRange word,
                                const QuantityMatch quantity_match,
                                const DerivationIR &derivation,
                                std::vector<AnalysisIR> &output) {
    const auto word_text =
        std::string_view{surface.lookup_ascii}.substr(word.begin, word.count);
    const auto qu_form = word_text.size() >= 3U && word_text.starts_with("qu");
    const auto cu_form = word_text.size() >= 3U && word_text.starts_with("cu");
    const auto aliqu_form =
        word_text.size() >= 6U && word_text.starts_with("aliqu");
    const auto alicu_form =
        word_text.size() >= 6U && word_text.starts_with("alicu");
    if (!qu_form && !cu_form && !aliqu_form && !alicu_form) {
        return;
    }
    const auto required_key = qu_form || aliqu_form ? 1U : 2U;
    const auto candidates = enumerate_candidates(database, surface, word);
    for (const auto &group : candidates.groups()) {
        std::span<const StemReference> stems;
        bool stems_loaded{};
        for (const auto rule_id : group.rules) {
            const auto &rule = database.rule(rule_id);
            if (rule.part_of_speech != PartOfSpeech::pronoun ||
                rule.stem_key != required_key) {
                continue;
            }
            if (!stems_loaded) {
                stems =
                    database.lookup_stem(candidate_stem(surface, group.stem));
                stems_loaded = true;
            }
            for (const auto &stem : stems) {
                const auto &lexeme = database.lexeme(stem.lexeme);
                if (lexeme.part_of_speech != PartOfSpeech::pronoun ||
                    lexeme.declension != 1U ||
                    lexeme.declension != rule.declension ||
                    lexeme.variant != rule.variant) {
                    continue;
                }
                output.push_back(AnalysisIR{
                    .lexeme = stem.lexeme,
                    .rule = rule_id,
                    .stem_key = rule.stem_key,
                    .stem = group.stem,
                    .ending = group.ending,
                    .morphology =
                        PronounMorphology{.declension = rule.declension,
                                          .variant = 0U,
                                          .grammatical_case =
                                              rule.grammatical_case,
                                          .number = rule.number,
                                          .gender = rule.gender},
                    .quantity_match = quantity_match,
                    .derivation = derivation,
                    .assessment = {},
                });
            }
        }
    }
}

void add_matching_tickons(const Database &database, const std::string_view word,
                          std::vector<AddonId> &ids) {
    const auto maximum = std::min(database.maximum_prefix_size(),
                                  word.size() - (word.empty() ? 0U : 1U));
    for (std::size_t size = 1; size <= maximum; ++size) {
        const auto matches = database.lookup_tickon(word.substr(0, size));
        ids.insert(ids.end(), matches.begin(), matches.end());
    }
    sort_unique_ids(ids);
}

void append_tickon_analyses(const Database &database,
                            const SurfaceForm &surface, const SurfaceRange word,
                            const QuantityMatch quantity_match,
                            std::vector<AnalysisIR> &output,
                            EnumerationState &state) {
    const auto word_text =
        std::string_view{surface.lookup_ascii}.substr(word.begin, word.count);
    std::vector<AddonId> ids;
    add_matching_tickons(database, word_text, ids);
    for (const auto id : ids) {
        const auto &tickon = database.prefix(id);
        if (!prefix_matches(database, tickon, word_text)) {
            continue;
        }
        const auto fix_size = database.prefix_string(tickon.fix).size();
        const SurfaceRange base_word{
            .begin = word.begin + static_cast<std::uint32_t>(fix_size),
            .count = word.count - static_cast<std::uint32_t>(fix_size)};
        const auto base_text = std::string_view{surface.lookup_ascii}.substr(
            base_word.begin, base_word.count);
        if (base_text.size() < 3U ||
            (!base_text.starts_with("qu") && !base_text.starts_with("cu"))) {
            continue;
        }
        DerivationIR derivation;
        derivation.addon_ids.front() = id;
        derivation.count = 1U;
        const auto before = output.size();
        append_qu_pronoun_analyses(database, surface, base_word, quantity_match,
                                   derivation, output);
        append_packon_analyses(database, surface, base_word, quantity_match,
                               derivation, output);
        if (output.size() != before || state.unsupported) {
            return; // Tickons, like ordinary prefixes, stop at first hit.
        }
    }
}

void sort_and_deduplicate_analyses(std::vector<AnalysisIR> &analyses) {
    const auto semantic_key = [](const AnalysisIR &analysis) {
        std::array<std::uint32_t, 2> rewrite_ids{};
        std::uint8_t rewrite_count{};
        if (analysis.derivation.rewritten_form) {
            rewrite_count = analysis.derivation.rewritten_form->count;
            std::ranges::transform(
                analysis.derivation.rewritten_form->rules, rewrite_ids.begin(),
                [](const RewriteId id) { return id.value(); });
        }
        const auto rewritten_stem =
            analysis.derivation.rewritten_form
                ? std::string_view{analysis.derivation.rewritten_form->stem}
                : std::string_view{};
        const auto rewritten_ending =
            analysis.derivation.rewritten_form
                ? std::string_view{analysis.derivation.rewritten_form->ending}
                : std::string_view{};
        return std::tuple{
            analysis.lexeme.value(),
            analysis.stem_key,
            analysis.stem.begin,
            analysis.stem.count,
            analysis.ending.begin,
            analysis.ending.count,
            morphology_key(analysis.morphology),
            std::to_underlying(analysis.quantity_match),
            analysis.derivation.count,
            analysis.derivation.addon_ids,
            rewrite_count,
            rewrite_ids,
            analysis.derivation.rewritten_form
                ? analysis.derivation.rewritten_form->leading_addon_count
                : 0U,
            rewritten_stem,
            rewritten_ending,
        };
    };
    std::ranges::sort(
        analyses, [&](const AnalysisIR &left, const AnalysisIR &right) {
            const auto left_key = semantic_key(left);
            const auto right_key = semantic_key(right);
            return left_key < right_key ||
                   (left_key == right_key && left.rule < right.rule);
        });
    const auto duplicates =
        std::ranges::unique(analyses, [&](const AnalysisIR &left,
                                          const AnalysisIR &right) {
            return semantic_key(left) == semantic_key(right);
        }).begin();
    analyses.erase(duplicates, analyses.end());
}

[[nodiscard]] bool is_finite_or_infinitive(const Mood mood) noexcept {
    return mood == Mood::indicative || mood == Mood::subjunctive ||
           mood == Mood::imperative || mood == Mood::infinitive;
}

[[nodiscard]] bool is_finite_mood(const Mood mood) noexcept {
    return mood == Mood::indicative || mood == Mood::subjunctive ||
           mood == Mood::imperative;
}

[[nodiscard]] bool
is_supported_imperative(const VerbMorphology &verb) noexcept {
    const auto present_second =
        verb.tense == Tense::present && verb.person == Person::second;
    const auto future_second_or_third =
        verb.tense == Tense::future && verb.person != Person::first;
    return present_second || future_second_or_third;
}

[[nodiscard]] bool
is_disallowed_deponent_active_form(const VerbMorphology &verb) noexcept {
    const auto future_infinitive =
        verb.mood == Mood::infinitive && verb.tense == Tense::future;
    return verb.voice == Voice::active && is_finite_or_infinitive(verb.mood) &&
           !future_infinitive;
}

void add_trim_reason_with_notices(const Database &database,
                                  const LexemeId lexeme,
                                  const WhitakerTrimReason reason,
                                  MorphologicalAssessmentIR &assessment) {
    assessment.whitaker_trim.add(reason);
    const auto notices = database.lookup_morphological_notices(lexeme, reason);
    for (const auto notice : all_morphological_notices) {
        if (notices.contains(notice)) {
            assessment.add_notice(notice);
        }
    }
}

[[nodiscard]] MorphologicalAssessmentIR
assess_morphology(const Database &database, const SurfaceForm &surface,
                  const AnalysisIR &analysis) {
    MorphologicalAssessmentIR assessment;
    const auto *verb = std::get_if<VerbMorphology>(&analysis.morphology);
    if (verb == nullptr) {
        return assessment;
    }

    const auto &lexeme = database.lexeme(analysis.lexeme);
    const auto stem =
        analysis.derivation.rewritten_form
            ? std::string_view{analysis.derivation.rewritten_form->stem}
            : surface.slice(analysis.stem);
    const auto ending_empty =
        analysis.derivation.rewritten_form
            ? analysis.derivation.rewritten_form->ending.empty()
            : analysis.ending.count == 0U;

    if (verb->conjugation == 3U && verb->variant == 1U &&
        verb->tense == Tense::present && verb->voice == Voice::active &&
        verb->mood == Mood::imperative && verb->person == Person::second &&
        verb->number == GrammaticalNumber::singular && ending_empty) {
        constexpr std::array<std::string_view, 4> licensed{"dic", "duc", "fac",
                                                           "fer"};
        const auto suffix = stem.size() < 3U ? std::string_view{}
                                             : stem.substr(stem.size() - 3U);
        if (!std::ranges::contains(licensed, suffix)) {
            assessment.whitaker_trim.add(
                WhitakerTrimReason::unsupported_short_imperative);
        }
    }

    if (verb->mood == Mood::imperative && !is_supported_imperative(*verb)) {
        assessment.whitaker_trim.add(
            WhitakerTrimReason::invalid_imperative_person);
    }

    if (lexeme.verb_kind == VerbKind::impersonal &&
        verb->person != Person::third) {
        assessment.whitaker_trim.add(
            WhitakerTrimReason::impersonal_non_third_person);
    }
    if (lexeme.verb_kind == VerbKind::impersonal &&
        is_finite_mood(verb->mood) &&
        verb->number == GrammaticalNumber::plural) {
        // Whitaker licenses third-person plural impersonal forms, while the
        // reviewed grammar describes finite impersonals as third singular.
        // Preserve the generated reading and expose the disagreement.
        assessment.add_notice(MorphologicalNotice::source_disagreement);
    }

    if (lexeme.verb_kind == VerbKind::deponent &&
        is_disallowed_deponent_active_form(*verb)) {
        assessment.whitaker_trim.add(WhitakerTrimReason::deponent_active_form);
    }

    if (lexeme.verb_kind == VerbKind::semideponent &&
        is_finite_mood(verb->mood)) {
        if (verb->voice == Voice::passive &&
            (verb->tense == Tense::present || verb->tense == Tense::imperfect ||
             verb->tense == Tense::future)) {
            constexpr auto trigger =
                WhitakerTrimReason::semideponent_passive_present_system;
            add_trim_reason_with_notices(database, analysis.lexeme, trigger,
                                         assessment);
        }
        if (verb->voice == Voice::active &&
            (verb->tense == Tense::perfect ||
             verb->tense == Tense::pluperfect ||
             verb->tense == Tense::future_perfect)) {
            constexpr auto trigger =
                WhitakerTrimReason::semideponent_active_perfect_system;
            add_trim_reason_with_notices(database, analysis.lexeme, trigger,
                                         assessment);
        }
    }
    return assessment;
}

void apply_whitaker_trim(const Database &database, const SurfaceForm &surface,
                         std::vector<AnalysisIR> &analyses) {
    for (auto &analysis : analyses) {
        analysis.assessment = assess_morphology(database, surface, analysis);
    }
}

void apply_whitaker_trim(const Database &database, QueryResult &result) {
    apply_whitaker_trim(database, result.surface, result.analyses);
}

struct LexicalBatch final {
    std::vector<AnalysisIR> analyses;
    EnumerationState state;
};

[[nodiscard]] LexicalBatch
analyze_lexical_surface(const Database &database, const SurfaceForm &surface,
                        const MorphologicalMechanisms &mechanisms) {
    LexicalBatch result;
    const auto logical_size = surface.lookup_ascii.size();
    const auto quantity_match = has_quantity(surface)
                                    ? QuantityMatch::unknown
                                    : QuantityMatch::unspecified;
    const SurfaceRange full_word{
        .begin = 0U, .count = static_cast<std::uint32_t>(logical_size)};

    append_unique_analyses(database, surface, full_word, quantity_match,
                           DerivationIR{}, result.analyses);
    if (mechanisms.tickons) {
        append_tickon_analyses(database, surface, full_word, quantity_match,
                               result.analyses, result.state);
    }
    if (!result.state.unsupported) {
        append_qu_pronoun_analyses(database, surface, full_word, quantity_match,
                                   DerivationIR{}, result.analyses);
    }
    if (!result.state.unsupported) {
        // WHY: tickons and packons express additional readings; they must not
        // hide an independently stored whole-word lexeme such as nequiquam.
        // Pre-existing results suppress only productive prefix/suffix fallback
        // inside append_word_analyses, not its regular dictionary lookup.
        append_word_analyses(database, surface, full_word, quantity_match,
                             DerivationIR{}, mechanisms, result.analyses,
                             result.state);
    }
    if (mechanisms.packons && !result.state.unsupported) {
        append_packon_analyses(database, surface, full_word, quantity_match,
                               DerivationIR{}, result.analyses);
    }
    if (mechanisms.tackons && !result.state.unsupported) {
        const auto output_before_tackons = result.analyses.size();
        const auto only_weak_suffixes =
            !result.analyses.empty() &&
            std::ranges::all_of(result.analyses,
                                [&](const AnalysisIR &analysis) {
                                    return has_addon_kind(database, analysis,
                                                          AddonKind::suffix);
                                });
        const auto direct_hit = std::ranges::any_of(
            result.analyses, [&](const AnalysisIR &analysis) {
                return !has_addon_kind(database, analysis, AddonKind::suffix);
            });
        append_tackon_analyses(database, surface, full_word, quantity_match,
                               true, direct_hit, mechanisms, result.analyses,
                               result.state);
        if (only_weak_suffixes &&
            result.analyses.size() != output_before_tackons) {
            // WHY: a direct base plus an enclitic is stronger than an
            // accidental suffix interpretation of the unsplit surface, as in
            // sola-ne.  Retaining both would preserve the original false hit.
            result.analyses.erase(
                result.analyses.begin(),
                result.analyses.begin() +
                    static_cast<std::ptrdiff_t>(output_before_tackons));
        }
    }
    if (mechanisms.tackons && result.analyses.empty() &&
        !result.state.unsupported) {
        append_tackon_analyses(database, surface, full_word, quantity_match,
                               false, false, mechanisms, result.analyses,
                               result.state);
    }
    if (!result.state.unsupported) {
        sort_and_deduplicate_analyses(result.analyses);
        if (has_quantity(surface)) {
            // WHY: a confirmed marked form is stronger evidence than a row
            // whose quantity is still absent from the gradually enriched DB.
            // Stable partitioning preserves every legacy tie inside each tier.
            std::ranges::stable_partition(
                result.analyses, [](const AnalysisIR &analysis) {
                    return analysis.quantity_match == QuantityMatch::exact;
                });
        }
    }
    return result;
}

[[nodiscard]] bool
is_legacy_common_prefix(const std::string_view value) noexcept {
    return std::ranges::find(two_words_common_prefixes, value) !=
           two_words_common_prefixes.end();
}

[[nodiscard]] bool is_ascii_titlecase(const std::string_view value) noexcept {
    return value.size() > 1U && value.front() >= 'A' && value.front() <= 'Z' &&
           value[1] >= 'a' && value[1] <= 'z';
}

[[nodiscard]] std::optional<TwoWordSuggestionIR>
analyze_two_words(const Database &database, const LatinLexer &lexer,
                  const SurfaceForm &surface, const AnalysisOptions &options) {
    constexpr std::size_t minimum_left = 2U;
    constexpr std::size_t minimum_right = 3U;
    const auto logical_size = surface.lookup_ascii.size();
    if (logical_size < minimum_left + minimum_right ||
        is_ascii_titlecase(surface.original_utf8)) {
        return std::nullopt;
    }

    // Two_Words is the last-chance splitter, not a recursive invocation of
    // the public engine.  Analyzing each side through the lexical core keeps
    // addons but excludes syncope, spelling tricks, numerals and Two_Words.
    for (std::size_t split = minimum_left;
         split + minimum_right <= logical_size; ++split) {
        if (is_legacy_common_prefix(
                std::string_view{surface.lookup_ascii}.substr(0U, split))) {
            continue;
        }
        const auto nfc_split = surface.nfc_byte_offsets.at(split);
        auto left_surface = lexer.lex(
            std::string_view{surface.normalized_nfc}.substr(0U, nfc_split));
        if (!left_surface) {
            continue;
        }
        auto left = analyze_lexical_surface(database, *left_surface,
                                            options.mechanisms);
        apply_whitaker_trim(database, *left_surface, left.analyses);
        if (left.state.unsupported || left.analyses.empty()) {
            continue;
        }

        auto right_surface = lexer.lex(
            std::string_view{surface.normalized_nfc}.substr(nfc_split));
        if (!right_surface) {
            continue;
        }
        auto right = analyze_lexical_surface(database, *right_surface,
                                             options.mechanisms);
        apply_whitaker_trim(database, *right_surface, right.analyses);
        if (right.state.unsupported || right.analyses.empty()) {
            continue;
        }

        const auto contains_numeral = [](const auto &analyses) {
            return std::ranges::any_of(analyses, [](const AnalysisIR &item) {
                return std::holds_alternative<NumeralMorphology>(
                    item.morphology);
            });
        };
        const bool both_contain_numeral =
            contains_numeral(left.analyses) && contains_numeral(right.analyses);
        return TwoWordSuggestionIR{
            .logical_split = static_cast<std::uint32_t>(split),
            .segments =
                std::array{
                    WordSegmentIR{.surface = std::move(*left_surface),
                                  .analyses = std::move(left.analyses)},
                    WordSegmentIR{.surface = std::move(*right_surface),
                                  .analyses = std::move(right.analyses)},
                },
            .both_contain_numeral = both_contain_numeral,
        };
    }
    return std::nullopt;
}

[[nodiscard]] bool rewrite_accepts(const AnalysisIR &analysis,
                                   const RewriteRule &rewrite) noexcept {
    if (rewrite.required_stem_key != 0U &&
        analysis.stem_key != rewrite.required_stem_key) {
        return false;
    }
    if (rewrite.required_part == PartOfSpeech::unknown) {
        return true;
    }
    if (rewrite.required_part == PartOfSpeech::verb) {
        if (!std::holds_alternative<VerbMorphology>(analysis.morphology)) {
            return false;
        }
    } else if (rewrite.required_part == PartOfSpeech::adjective) {
        if (!std::holds_alternative<AdjectiveMorphology>(analysis.morphology)) {
            return false;
        }
    } else if (rewrite.required_part != PartOfSpeech::unknown) {
        return false;
    }

    if (rewrite.constraint == RewriteConstraint::eo_verb) {
        const auto *verb = std::get_if<VerbMorphology>(&analysis.morphology);
        return verb != nullptr && verb->conjugation == eo_conjugation &&
               verb->variant == eo_variant;
    }
    if (rewrite.constraint == RewriteConstraint::adjective_iis) {
        const auto *adjective =
            std::get_if<AdjectiveMorphology>(&analysis.morphology);
        return adjective != nullptr && adjective->declension == 1U &&
               adjective->variant == 1U &&
               (adjective->grammatical_case == GrammaticalCase::dative ||
                adjective->grammatical_case == GrammaticalCase::ablative) &&
               adjective->number == GrammaticalNumber::plural;
    }
    return true;
}

struct RewriteAttempt final {
    const RewriteRule *rule{};
    std::size_t position{};
    std::size_t remove_count{};
    std::string replacement;
};

[[nodiscard]] std::string
replace_logical_surface(const SurfaceForm &surface, const std::size_t position,
                        const std::size_t remove_count,
                        const std::string_view replacement) {
    const auto first = surface.nfc_byte_offsets.at(position);
    const auto last = surface.nfc_byte_offsets.at(position + remove_count);
    std::string transformed;
    transformed.reserve(surface.normalized_nfc.size() - (last - first) +
                        replacement.size());
    transformed.append(surface.normalized_nfc, 0U, first);
    transformed.append(replacement);
    transformed.append(std::string_view{surface.normalized_nfc}.substr(last));
    return transformed;
}

[[nodiscard]] std::vector<RewriteAttempt>
rewrite_attempts(const Database &database, const std::string_view word,
                 const std::span<const RewriteRule *const> rules,
                 const OrthographyMode orthography =
                     OrthographyMode::classical_and_medieval) {
    std::vector<RewriteAttempt> attempts;
    for (const auto *const rule_pointer : rules) {
        const auto &rule = *rule_pointer;
        if (orthography == OrthographyMode::disabled ||
            (orthography == OrthographyMode::classical_only && rule.medieval)) {
            continue;
        }
        const auto before = database.rewrite_string(rule.before);
        if (rule.operation == RewriteOperation::slur) {
            if (before.size() < 2U || word.size() < before.size() + 2U) {
                continue;
            }
            const auto consonant = [](const char value) {
                constexpr std::string_view vowels{"aeiouy"};
                return !vowels.contains(value);
            };
            if (word.starts_with(before) && consonant(word.at(before.size()))) {
                std::string replacement{before.substr(0U, before.size() - 1U)};
                replacement.append(2U, word.at(before.size()));
                attempts.push_back(
                    {&rule, 0U, before.size() + 1U, std::move(replacement)});
            } else if (word.starts_with(
                           before.substr(0U, before.size() - 1U)) &&
                       word.at(before.size() - 1U) == word.at(before.size()) &&
                       consonant(word.at(before.size()))) {
                std::string replacement{before};
                replacement.push_back(word.at(before.size()));
                attempts.push_back(
                    {&rule, 0U, before.size() + 1U, std::move(replacement)});
            }
            continue;
        }
        if (rule.operation == RewriteOperation::double_consonant) {
            constexpr std::string_view vowels{"aeiouy"};
            for (std::size_t position = 1U; position + 1U < word.size();
                 ++position) {
                if (!vowels.contains(word.at(position)) &&
                    vowels.contains(word.at(position - 1U)) &&
                    vowels.contains(word.at(position + 1U))) {
                    attempts.push_back({&rule, position, 1U,
                                        std::string(2U, word.at(position))});
                }
            }
            continue;
        }
        if (word.size() <
            before.size() + rule.minimum_before + rule.minimum_after) {
            continue;
        }
        for (std::size_t position = rule.minimum_before;
             position + before.size() + rule.minimum_after <= word.size();
             ++position) {
            const auto scope_matches =
                rule.scope == RewriteScope::internal ||
                (rule.scope == RewriteScope::initial && position == 0U) ||
                (rule.scope == RewriteScope::final &&
                 position + before.size() == word.size());
            if (scope_matches && word.substr(position).starts_with(before)) {
                attempts.push_back(
                    {&rule, position, before.size(),
                     std::string{database.rewrite_string(rule.after)}});
            }
        }
    }
    std::ranges::sort(
        attempts, [](const RewriteAttempt &left, const RewriteAttempt &right) {
            if (left.rule->scan_reverse != right.rule->scan_reverse) {
                return left.rule->scan_reverse > right.rule->scan_reverse;
            }
            if (left.position != right.position) {
                return left.rule->scan_reverse ? left.position > right.position
                                               : left.position < right.position;
            }
            return left.rule->id < right.rule->id;
        });
    return attempts;
}

[[nodiscard]] std::vector<AnalysisIR>
analyze_syncope(const Database &database, const LatinLexer &lexer,
                const SurfaceForm &surface,
                const MorphologicalMechanisms &mechanisms) {
    const std::string_view word = surface.lookup_ascii;
    for (const auto group :
         database.rewrite_groups(RewriteKind::syncope, RewriteStage::main)) {
        for (const auto &attempt :
             rewrite_attempts(database, word, database.rewrite_rules(group))) {
            const auto &rewrite = *attempt.rule;
            const auto transformed = replace_logical_surface(
                surface, attempt.position, attempt.remove_count,
                attempt.replacement);
            auto transformed_surface = lexer.lex(transformed);
            if (!transformed_surface) {
                continue;
            }
            auto batch = analyze_lexical_surface(database, *transformed_surface,
                                                 mechanisms);
            if (batch.state.unsupported) {
                continue;
            }
            std::erase_if(batch.analyses, [&](const AnalysisIR &analysis) {
                return !rewrite_accepts(analysis, rewrite) ||
                       !is_plain_or_outer_tackon(database, analysis);
            });
            if (batch.analyses.empty()) {
                continue;
            }
            for (auto &analysis : batch.analyses) {
                analysis.quantity_match = has_quantity(surface)
                                              ? QuantityMatch::unknown
                                              : QuantityMatch::unspecified;
                RewrittenFormIR rewritten;
                rewritten.rules.front() = rewrite.id;
                rewritten.positions.front() =
                    static_cast<std::uint32_t>(attempt.position);
                rewritten.remove_counts.front() =
                    static_cast<std::uint32_t>(attempt.remove_count);
                rewritten.observed.front() = surface.slice(
                    {.begin = static_cast<std::uint32_t>(attempt.position),
                     .count =
                         static_cast<std::uint32_t>(attempt.remove_count)});
                rewritten.replacements.front() = attempt.replacement;
                rewritten.count = 1U;
                rewritten.stem = transformed_surface->slice(analysis.stem);
                rewritten.ending = transformed_surface->slice(analysis.ending);
                analysis.derivation.rewritten_form = std::move(rewritten);
            }
            return batch.analyses;
        }
    }
    return {};
}

[[nodiscard]] bool prepend_rewrite(AnalysisIR &analysis,
                                   const RewriteAttempt &attempt,
                                   const SurfaceForm &source_surface,
                                   const SurfaceForm &transformed_surface) {
    if (!analysis.derivation.rewritten_form) {
        RewrittenFormIR rewritten;
        rewritten.rules.front() = attempt.rule->id;
        rewritten.positions.front() =
            static_cast<std::uint32_t>(attempt.position);
        rewritten.remove_counts.front() =
            static_cast<std::uint32_t>(attempt.remove_count);
        rewritten.observed.front() = source_surface.slice(
            {.begin = static_cast<std::uint32_t>(attempt.position),
             .count = static_cast<std::uint32_t>(attempt.remove_count)});
        rewritten.replacements.front() = attempt.replacement;
        rewritten.count = 1U;
        rewritten.stem = transformed_surface.slice(analysis.stem);
        rewritten.ending = transformed_surface.slice(analysis.ending);
        analysis.derivation.rewritten_form = std::move(rewritten);
        return true;
    }

    auto &rewritten = *analysis.derivation.rewritten_form;
    if (rewritten.count >= rewritten.rules.size()) {
        return false;
    }
    std::ranges::move_backward(
        std::span{rewritten.rules}.first(rewritten.count),
        rewritten.rules.begin() + static_cast<std::ptrdiff_t>(rewritten.count) +
            1);
    std::ranges::move_backward(
        std::span{rewritten.positions}.first(rewritten.count),
        rewritten.positions.begin() +
            static_cast<std::ptrdiff_t>(rewritten.count) + 1);
    std::ranges::move_backward(
        std::span{rewritten.remove_counts}.first(rewritten.count),
        rewritten.remove_counts.begin() +
            static_cast<std::ptrdiff_t>(rewritten.count) + 1);
    std::ranges::move_backward(
        std::span{rewritten.observed}.first(rewritten.count),
        rewritten.observed.begin() +
            static_cast<std::ptrdiff_t>(rewritten.count) + 1);
    std::ranges::move_backward(
        std::span{rewritten.replacements}.first(rewritten.count),
        rewritten.replacements.begin() +
            static_cast<std::ptrdiff_t>(rewritten.count) + 1);
    rewritten.rules.front() = attempt.rule->id;
    rewritten.positions.front() = static_cast<std::uint32_t>(attempt.position);
    rewritten.remove_counts.front() =
        static_cast<std::uint32_t>(attempt.remove_count);
    rewritten.observed.front() = source_surface.slice(
        {.begin = static_cast<std::uint32_t>(attempt.position),
         .count = static_cast<std::uint32_t>(attempt.remove_count)});
    rewritten.replacements.front() = attempt.replacement;
    ++rewritten.count;
    return true;
}

[[nodiscard]] std::vector<AnalysisIR>
analyze_orthography(const Database &database, const LatinLexer &lexer,
                    const SurfaceForm &surface, const RewriteStage stage,
                    const AnalysisOptions &options) {
    if (options.orthography == OrthographyMode::disabled) {
        return {};
    }

    const std::string_view word = surface.orthography_ascii;
    for (const auto group :
         database.rewrite_groups(RewriteKind::orthographic, stage)) {
        for (const auto &attempt :
             rewrite_attempts(database, word, database.rewrite_rules(group),
                              options.orthography)) {
            const auto transformed = replace_logical_surface(
                surface, attempt.position, attempt.remove_count,
                attempt.replacement);
            auto transformed_surface = lexer.lex(transformed);
            if (!transformed_surface) {
                continue;
            }

            auto batch = analyze_lexical_surface(database, *transformed_surface,
                                                 options.mechanisms);
            auto syncopated =
                options.mechanisms.syncope
                    ? analyze_syncope(database, lexer, *transformed_surface,
                                      options.mechanisms)
                    : std::vector<AnalysisIR>{};
            if (!syncopated.empty() && !batch.analyses.empty() &&
                std::ranges::all_of(batch.analyses,
                                    [](const AnalysisIR &analysis) {
                                        return analysis.derivation.count > 0U;
                                    })) {
                // As in the top-level pipeline, a validated perfect-system
                // recovery outranks productive addon guesses for the same
                // transformed spelling.
                batch.analyses.clear();
            }
            batch.analyses.insert(batch.analyses.end(),
                                  std::make_move_iterator(syncopated.begin()),
                                  std::make_move_iterator(syncopated.end()));

            const auto &rewrite = *attempt.rule;
            std::erase_if(batch.analyses, [&](const AnalysisIR &analysis) {
                // WHY: a spelling rewrite is evidence for a changed surface,
                // not permission to invent a productive prefix or suffix at
                // the same time.  Orthography-plus-syncope remains possible
                // because a rewrite is represented separately from addons;
                // orthography-plus-enclitic has its own bounded outer path.
                return !rewrite_accepts(analysis, rewrite) ||
                       analysis.derivation.count != 0U;
            });
            for (auto &analysis : batch.analyses) {
                analysis.quantity_match = has_quantity(surface)
                                              ? QuantityMatch::unknown
                                              : QuantityMatch::unspecified;
                if (!prepend_rewrite(analysis, attempt, surface,
                                     *transformed_surface)) {
                    analysis.derivation.rewritten_form.reset();
                }
            }
            std::erase_if(batch.analyses, [](const AnalysisIR &analysis) {
                return !analysis.derivation.rewritten_form.has_value();
            });
            if (!batch.analyses.empty()) {
                sort_and_deduplicate_analyses(batch.analyses);
                return batch.analyses;
            }
        }
    }
    return {};
}

[[nodiscard]] std::vector<AnalysisIR> analyze_orthography_with_tackon(
    const Database &database, const LatinLexer &lexer,
    const SurfaceForm &surface, const AnalysisOptions &options) {
    const std::string_view word = surface.lookup_ascii;
    std::vector<AddonId> ids;
    add_matching_tackons(database, word, false, ids);
    for (const auto id : ids) {
        const auto &tackon = database.tackon(id);
        const auto fix = database.tackon_string(tackon.fix);
        if (!tackon.enclitic || fix.size() >= word.size() ||
            !affix_equal(word.substr(word.size() - fix.size()), fix)) {
            continue;
        }

        const auto base_count = word.size() - fix.size();
        auto base = lexer.lex(surface.slice(
            {.begin = 0U, .count = static_cast<std::uint32_t>(base_count)}));
        if (!base) {
            return {};
        }
        auto analyses = analyze_orthography(database, lexer, *base,
                                            RewriteStage::early, options);
        if (analyses.empty()) {
            analyses = analyze_orthography(database, lexer, *base,
                                           RewriteStage::fallback, options);
        }
        for (auto &analysis : analyses) {
            auto &derivation = analysis.derivation;
            if (derivation.count >= derivation.addon_ids.size() ||
                !derivation.rewritten_form) {
                derivation.rewritten_form.reset();
                continue;
            }
            std::ranges::move_backward(
                std::span{derivation.addon_ids}.first(derivation.count),
                derivation.addon_ids.begin() +
                    static_cast<std::ptrdiff_t>(derivation.count) + 1);
            derivation.addon_ids.front() = id;
            ++derivation.count;
            derivation.rewritten_form->leading_addon_count = 1U;
            analysis.quantity_match = has_quantity(surface)
                                          ? QuantityMatch::unknown
                                          : QuantityMatch::unspecified;
        }
        std::erase_if(analyses, [](const AnalysisIR &analysis) {
            return !analysis.derivation.rewritten_form.has_value();
        });
        sort_and_deduplicate_analyses(analyses);
        return analyses; // Ada stops after the first matching enclitic.
    }
    return {};
}

[[nodiscard]] constexpr bool
allows_compound(const BoundaryFlag boundary) noexcept {
    constexpr auto blockers =
        BoundaryFlag::comma | BoundaryFlag::semicolon | BoundaryFlag::colon |
        BoundaryFlag::period | BoundaryFlag::question |
        BoundaryFlag::exclamation | BoundaryFlag::apostrophe |
        BoundaryFlag::dash | BoundaryFlag::other_punctuation;
    return !has_any_flag(boundary, blockers);
}

[[nodiscard]] const AnalysisIR *
finite_sum_morphology(const Database &database, const QueryResult &auxiliary) {
    const AnalysisIR *selected{};
    for (const auto &analysis : auxiliary.analyses) {
        const auto *verb = std::get_if<VerbMorphology>(&analysis.morphology);
        if (verb == nullptr || (verb->mood != Mood::indicative &&
                                verb->mood != Mood::subjunctive)) {
            continue;
        }
        const auto &lexeme = database.lexeme(analysis.lexeme);
        if (lexeme.part_of_speech != PartOfSpeech::verb ||
            lexeme.declension != sum_lexeme_conjugation ||
            lexeme.variant != sum_lexeme_variant) {
            continue;
        }
        const auto key = [](const VerbMorphology &value) {
            // Is_Sum scans indicative before subjunctive and then tense,
            // number and person.  Keeping that order matters for ambiguous
            // forms such as fueris and fuerunt.
            return std::tuple{std::to_underlying(value.mood),
                              std::to_underlying(value.tense),
                              std::to_underlying(value.number), value.person};
        };
        if (selected == nullptr ||
            key(*verb) < key(std::get<VerbMorphology>(selected->morphology))) {
            selected = &analysis;
        }
    }
    return selected;
}

[[nodiscard]] Tense finite_compound_tense(const Tense auxiliary) noexcept {
    if (auxiliary == Tense::present || auxiliary == Tense::perfect) {
        return Tense::perfect;
    }
    if (auxiliary == Tense::imperfect || auxiliary == Tense::pluperfect) {
        return Tense::pluperfect;
    }
    if (auxiliary == Tense::future) {
        return Tense::future_perfect;
    }
    return Tense::unknown;
}

[[nodiscard]] CompoundKind
compound_kind_from_auxiliary(const std::string_view auxiliary,
                             const bool has_finite_sum) noexcept {
    if (auxiliary == esse_auxiliary) {
        return CompoundKind::esse;
    }
    if (auxiliary == fuisse_auxiliary) {
        return CompoundKind::fuisse;
    }
    if (auxiliary == iri_auxiliary) {
        return CompoundKind::iri;
    }
    return has_finite_sum ? CompoundKind::finite_sum : CompoundKind::unknown;
}

[[nodiscard]] Tense
infinitive_compound_tense(const ParticipleMorphology &participle,
                          const CompoundKind kind) noexcept {
    if (participle.tense != Tense::future) {
        return participle.tense;
    }
    if (kind == CompoundKind::fuisse) {
        return Tense::perfect;
    }
    return participle.voice == Voice::active ? Tense::future : Tense::present;
}

[[nodiscard]] bool
is_compound_participle(const ParticipleMorphology &participle) noexcept {
    return (participle.tense == Tense::perfect &&
            participle.voice == Voice::passive) ||
           (participle.tense == Tense::future &&
            (participle.voice == Voice::active ||
             participle.voice == Voice::passive));
}

void append_compound(const AnalysisIR &source, const CompoundKind kind,
                     const Tense source_tense, const Voice source_voice,
                     const std::string_view auxiliary,
                     const DerivationIR &auxiliary_derivation,
                     const VerbMorphology morphology,
                     std::vector<CompoundAnalysisIR> &output) {
    auto assessment = source.assessment;
    if (kind == CompoundKind::finite_sum && source_tense == Tense::future &&
        source_voice == Voice::active && morphology.voice == Voice::passive) {
        // Preserve Whitaker's observable passive label, but mark its conflict
        // with the active future periphrastic described by the grammar.
        assessment.add_notice(MorphologicalNotice::source_disagreement);
    }
    output.push_back(CompoundAnalysisIR{
        .lexeme = source.lexeme,
        .source_rule = source.rule,
        .morphology = morphology,
        .source_derivation = source.derivation,
        .auxiliary_derivation = auxiliary_derivation,
        .kind = kind,
        .source_tense = source_tense,
        .source_voice = source_voice,
        .auxiliary = std::string{auxiliary},
        .assessment = assessment,
    });
}

[[nodiscard]] bool analyze_compound(const Database &database,
                                    QueryResult &result,
                                    const QueryResult &auxiliary) {
    const auto auxiliary_word = auxiliary.surface.lookup_ascii;
    const auto *finite_analysis = finite_sum_morphology(database, auxiliary);
    const auto *finite =
        finite_analysis == nullptr
            ? nullptr
            : std::get_if<VerbMorphology>(&finite_analysis->morphology);
    const auto kind =
        compound_kind_from_auxiliary(auxiliary_word, finite != nullptr);

    for (const auto &analysis : result.analyses) {
        if (const auto *participle =
                std::get_if<ParticipleMorphology>(&analysis.morphology)) {
            if (!is_compound_participle(*participle)) {
                continue;
            }

            VerbMorphology morphology{.conjugation = participle->conjugation,
                                      .variant = participle->variant};
            bool accepts{};
            if (kind == CompoundKind::finite_sum && (finite != nullptr) &&
                participle->grammatical_case == GrammaticalCase::nominative &&
                participle->number == finite->number) {
                accepts = true;
                morphology.tense = participle->tense == Tense::perfect
                                       ? finite_compound_tense(finite->tense)
                                       : finite->tense;
                // The historical constructor emits passive here even for a
                // future-active participle.  Preserve that observable result
                // until a separately versioned semantic correction exists.
                morphology.voice = Voice::passive;
                morphology.mood = finite->mood;
                morphology.person = finite->person;
                morphology.number = finite->number;
            } else if (kind == CompoundKind::esse ||
                       kind == CompoundKind::fuisse) {
                accepts = kind == CompoundKind::esse ||
                          participle->tense == Tense::future;
                if (accepts) {
                    morphology.tense =
                        infinitive_compound_tense(*participle, kind);
                    morphology.voice = participle->voice;
                    morphology.mood = Mood::infinitive;
                }
            }
            if (!accepts) {
                continue;
            }
            append_compound(analysis, kind, participle->tense,
                            participle->voice, auxiliary.surface.normalized_nfc,
                            finite_analysis == nullptr
                                ? DerivationIR{}
                                : finite_analysis->derivation,
                            morphology, result.compound_analyses);
            continue;
        }

        const auto *supine =
            std::get_if<SupineMorphology>(&analysis.morphology);
        if (kind != CompoundKind::iri || supine == nullptr ||
            supine->grammatical_case != GrammaticalCase::accusative) {
            continue;
        }
        const auto [conjugation, variant] =
            public_verb_paradigm(supine->conjugation, supine->variant);
        append_compound(analysis, kind, Tense::unknown, Voice::unknown,
                        auxiliary.surface.normalized_nfc, DerivationIR{},
                        VerbMorphology{.conjugation = conjugation,
                                       .variant = variant,
                                       .tense = Tense::future,
                                       .voice = Voice::passive,
                                       .mood = Mood::infinitive,
                                       .person = Person::unknown,
                                       .number = GrammaticalNumber::unknown},
                        result.compound_analyses);
    }

    // Compound hypotheses supplement the independent first-token analyses.
    // Keeping the lexical vector intact is both lossless and observationally
    // pure when lookahead fails; each compound already records its own source
    // rule and derivation.
    return !result.compound_analyses.empty();
}

[[nodiscard]] IndependentTokenAnalysisIR
independent_token(const QueryResult &result) {
    return IndependentTokenAnalysisIR{
        .surface = result.surface,
        .status = result.status,
        .analyses = result.analyses,
        .artificial_analyses = result.artificial_analyses,
        .diagnostics = result.diagnostics,
    };
}

[[nodiscard]] IndependentTokenAnalysisIR
independent_token(QueryResult &&result) noexcept {
    return IndependentTokenAnalysisIR{
        .surface = std::move(result.surface),
        .status = result.status,
        .analyses = std::move(result.analyses),
        .artificial_analyses = std::move(result.artificial_analyses),
        .diagnostics = std::move(result.diagnostics),
    };
}

void annotate_period_abbreviation_conflict(const TextToken &token,
                                           QueryResult &result) {
    constexpr std::string_view roman_abbreviation_letters{"ACDLM"};
    if (!has_any_flag(token.boundary_after.flags, BoundaryFlag::period) ||
        token.text.size() != 1U ||
        !roman_abbreviation_letters.contains(token.text.front())) {
        return;
    }

    const auto has_noun_abbreviation =
        std::ranges::any_of(result.analyses, [](const AnalysisIR &analysis) {
            const auto *noun =
                std::get_if<NounMorphology>(&analysis.morphology);
            return noun != nullptr && noun->declension == 9U &&
                   noun->variant == 8U;
        });
    if (!has_noun_abbreviation) {
        return;
    }

    for (auto &artificial : result.artificial_analyses) {
        std::visit(
            [](auto &analysis) {
                analysis.assessment.add_notice(
                    MorphologicalNotice::source_disagreement);
            },
            artificial);
    }
}

void preserve_independent_tokens(QueryResult &result, QueryResult &&auxiliary) {
    result.independent_tokens.clear();
    result.independent_tokens.reserve(2U);
    result.independent_tokens.push_back(independent_token(result));
    result.independent_tokens.push_back(
        independent_token(std::move(auxiliary)));
}

[[nodiscard]] bool try_compound_query(const Database &database,
                                      const std::string_view original_utf8,
                                      QueryResult &result,
                                      QueryResult &auxiliary) {
    if (result.status == QueryStatus::error ||
        auxiliary.status == QueryStatus::error ||
        !analyze_compound(database, result, auxiliary)) {
        return false;
    }

    std::string normalized = result.surface.normalized_nfc;
    normalized.push_back(' ');
    normalized.append(auxiliary.surface.normalized_nfc);
    result.multi_token_query = MultiTokenQueryIR{
        .original_utf8 = std::string{original_utf8},
        .normalized_nfc = std::move(normalized),
    };
    preserve_independent_tokens(result, std::move(auxiliary));
    result.artificial_analyses.clear();
    result.status = QueryStatus::analyzed;
    result.diagnostics.clear();
    return true;
}

} // namespace

std::uint64_t
Engine::dataset_fingerprint(const std::string_view dataset_id) noexcept {
    if (dataset_id.empty()) {
        return 0U;
    }

    constexpr std::uint64_t offset_basis = 14'695'981'039'346'656'037ULL;
    constexpr std::uint64_t prime = 1'099'511'628'211ULL;
    std::uint64_t fingerprint = offset_basis;
    for (const char value : dataset_id) {
        fingerprint ^= static_cast<unsigned char>(value);
        fingerprint *= prime;
    }
    // Zero denotes anonymous mode and must not be produced by a named dataset.
    return fingerprint == 0U ? 1U : fingerprint;
}

bool valid_dataset_id(const std::string_view value) noexcept {
    return value.empty() ||
           (value.size() == dataset_id_prefix.size() + sha256_hex_digit_count &&
            value.starts_with(dataset_id_prefix) &&
            std::ranges::all_of(value.substr(dataset_id_prefix.size()),
                                is_lower_hex));
}

std::expected<std::unique_ptr<const Engine>, LoadError>
Engine::create(std::vector<std::byte> database_image, EngineConfig config) {
    if (!valid_dataset_id(config.dataset_id)) {
        return std::unexpected(
            LoadError{.code = "invalid-dataset-id",
                      .message = "datasetId must be empty or sha256: followed "
                                 "by 64 lowercase hex digits"});
    }
    auto database = Database::load_poc(std::move(database_image));
    if (!database) {
        return std::unexpected(std::move(database.error()));
    }
    return std::unique_ptr<const Engine>{
        new Engine{std::move(*database), std::move(config)}};
}

QueryResult Engine::analyze(const std::string_view utf8,
                            const AnalysisOptions options) const {
    return analyze(TextToken{.text = utf8,
                             .byte_begin = 0U,
                             .byte_end = utf8.size(),
                             .boundary_after = {}},
                   options);
}

QueryResult Engine::analyze(const TextToken &token,
                            const AnalysisOptions options) const {
    const auto utf8 = token.text;
    auto lexed = lexer_.lex(utf8);
    if (!lexed) {
        QueryResult result{dataset_identity_};
        result.options = options;
        result.surface.original_utf8.assign(utf8);
        result.status = QueryStatus::error;
        result.diagnostics.push_back({.code = lexed.error().code,
                                      .severity = DiagnosticSeverity::error,
                                      .part_of_speech = std::nullopt});
        return result;
    }

    QueryResult result{dataset_identity_};
    result.options = options;
    result.surface = std::move(*lexed);
    const auto finish = [&]() -> QueryResult {
        apply_whitaker_trim(*database_, result);
        annotate_period_abbreviation_conflict(token, result);
        return std::move(result);
    };
    const auto logical_size = result.surface.lookup_ascii.size();
    const SurfaceRange full_word{
        .begin = 0U, .count = static_cast<std::uint32_t>(logical_size)};

    // A valid numeral is an independent reading and therefore coexists with
    // lexical homographs such as mi and di.  The permissive medieval reading
    // is delayed until every lexical path has failed below.
    if (const auto roman = analyze_roman_numeral(result.surface.normalized_nfc,
                                                 RomanRecognition::strict)) {
        auto direct_roman = *roman;
        direct_roman.stem = full_word;
        result.artificial_analyses.emplace_back(direct_roman);
    }

    auto lexical =
        analyze_lexical_surface(*database_, result.surface, options.mechanisms);
    auto enumeration = lexical.state;
    result.analyses = std::move(lexical.analyses);

    const auto has_unmodified_analysis =
        std::ranges::any_of(result.analyses, [](const AnalysisIR &analysis) {
            return analysis.derivation.count == 0U &&
                   !analysis.derivation.rewritten_form.has_value();
        });
    if (options.orthography != OrthographyMode::disabled &&
        !enumeration.unsupported && !has_unmodified_analysis &&
        result.artificial_analyses.empty()) {
        auto early = analyze_orthography(*database_, lexer_, result.surface,
                                         RewriteStage::early, options);
        if (!early.empty()) {
            // SLURY precedes productive prefix guesses in Ada. A validated
            // spelling recovery therefore replaces, rather than accompanies,
            // results whose entire provenance is a fallback addon path.
            result.analyses.clear();
            result.analyses = std::move(early);
        }
    }

    const auto contains_to_be =
        std::ranges::any_of(result.analyses, [&](const AnalysisIR &analysis) {
            const auto &lexeme = database_->lexeme(analysis.lexeme);
            return std::holds_alternative<VerbMorphology>(
                       analysis.morphology) &&
                   lexeme.declension == sum_lexeme_conjugation &&
                   lexeme.variant == sum_lexeme_variant;
        });
    const auto has_direct_tackon =
        std::ranges::any_of(result.analyses, [&](const AnalysisIR &analysis) {
            return analysis.derivation.count == 1U &&
                   has_addon_kind(*database_, analysis, AddonKind::tackon) &&
                   !analysis.derivation.rewritten_form.has_value();
        });
    if (options.mechanisms.syncope && !enumeration.unsupported &&
        !contains_to_be && !has_direct_tackon) {
        // WHY: a directly recognized base followed by one enclitic is already
        // a complete lexical explanation.  Running syncope afterwards can
        // replace it with unrelated perfect stems (mixti-que -> meio/mingo).
        auto syncopated = analyze_syncope(*database_, lexer_, result.surface,
                                          options.mechanisms);
        if (!syncopated.empty()) {
            // Productive addons are fallback guesses in Word(), while a
            // validated perfect-system syncope is the stronger legacy path.
            // Direct dictionary and UNIQUE homographs still coexist.
            if (!result.analyses.empty() &&
                std::ranges::all_of(result.analyses,
                                    [](const AnalysisIR &analysis) {
                                        return analysis.derivation.count > 0U;
                                    })) {
                result.analyses.clear();
            }
            result.analyses.insert(result.analyses.end(),
                                   std::make_move_iterator(syncopated.begin()),
                                   std::make_move_iterator(syncopated.end()));
            sort_and_deduplicate_analyses(result.analyses);
        }
    }

    const auto has_strong_lexical_analysis =
        std::ranges::any_of(result.analyses, [&](const AnalysisIR &analysis) {
            // A validated suffix derivation is already a successful Word()
            // path in Ada.  It must block Tricks_Enclitic just like a direct
            // dictionary hit; otherwise a broad spelling rule can replace a
            // real form such as anat-icul-us-que with an accidental lexeme.
            // A tackon over a merely productive prefix remains weak: SLURY
            // must still be able to prefer op-pon-o-que over ob-pon-o-que.
            return analysis.derivation.rewritten_form.has_value() ||
                   analysis.derivation.count == 0U ||
                   has_addon_kind(*database_, analysis, AddonKind::suffix) ||
                   has_addon_kind(*database_, analysis, AddonKind::packon) ||
                   (has_addon_kind(*database_, analysis, AddonKind::tackon) &&
                    !has_addon_kind(*database_, analysis, AddonKind::prefix));
        });
    if (options.mechanisms.tackons &&
        options.orthography != OrthographyMode::disabled &&
        !enumeration.unsupported && !has_strong_lexical_analysis &&
        result.artificial_analyses.empty()) {
        auto with_tackon = analyze_orthography_with_tackon(
            *database_, lexer_, result.surface, options);
        if (!with_tackon.empty()) {
            result.analyses = std::move(with_tackon);
        }
    }

    if (enumeration.unsupported) {
        result.analyses.clear();
        result.status = QueryStatus::error;
        result.diagnostics.push_back(
            {.code = DiagnosticCode::unsupported_part_of_speech,
             .severity = DiagnosticSeverity::error,
             .part_of_speech = enumeration.unsupported_part});
    } else if (result.analyses.empty() && result.artificial_analyses.empty()) {
        if (const auto roman = roman_numeral_with_tackon(
                *database_, result.surface, full_word)) {
            result.artificial_analyses.emplace_back(*roman);
            result.status = QueryStatus::analyzed;
            return finish();
        }
        // A string made only of Roman digits belongs to the artificial
        // grammar. Letting broad medieval spelling rules run first creates
        // accidental addon readings such as II- + f for IIV.
        if (const auto roman = analyze_roman_numeral(
                result.surface.normalized_nfc, RomanRecognition::permissive)) {
            auto fallback_roman = *roman;
            fallback_roman.stem = full_word;
            result.artificial_analyses.emplace_back(fallback_roman);
            result.status = QueryStatus::analyzed;
            return finish();
        }
        if (options.orthography != OrthographyMode::disabled) {
            result.analyses =
                analyze_orthography(*database_, lexer_, result.surface,
                                    RewriteStage::fallback, options);
        }
        if (!result.analyses.empty()) {
            result.status = QueryStatus::analyzed;
            return finish();
        }
        if (options.two_words == TwoWordsMode::legacy_first_match) {
            result.two_word_suggestion =
                analyze_two_words(*database_, lexer_, result.surface, options);
        }
        result.status = QueryStatus::unknown;
        result.diagnostics.push_back({.code = DiagnosticCode::unknown_word,
                                      .severity = DiagnosticSeverity::info,
                                      .part_of_speech = std::nullopt});
        if (result.two_word_suggestion) {
            result.diagnostics.push_back(
                {.code = DiagnosticCode::two_words_suggestion,
                 .severity = DiagnosticSeverity::warning,
                 .part_of_speech = std::nullopt});
        }
    } else {
        result.status = QueryStatus::analyzed;
    }
    return finish();
}

QueryResult Engine::analyze_text(const std::string_view utf8,
                                 const AnalysisOptions options) const {
    TextTokenCursor cursor{utf8};
    const auto first = cursor.next();
    if (first && cursor.peek() == nullptr) {
        auto result = analyze(*first, options);
        if (first->text != utf8) {
            result.multi_token_query = MultiTokenQueryIR{
                .original_utf8 = std::string{utf8},
                .normalized_nfc = result.surface.normalized_nfc};
        }
        return result;
    }

    const auto second = cursor.next();
    if (!first || !second || cursor.peek() != nullptr) {
        QueryResult result{dataset_identity_};
        result.options = options;
        result.surface.original_utf8.assign(utf8);
        result.multi_token_query = MultiTokenQueryIR{
            .original_utf8 = std::string{utf8}, .normalized_nfc = {}};
        result.status = QueryStatus::error;
        result.diagnostics.push_back(
            {.code = DiagnosticCode::unsupported_token_count,
             .severity = DiagnosticSeverity::error,
             .part_of_speech = std::nullopt});
        return result;
    }

    auto result = analyze(*first, options);
    auto auxiliary = analyze(*second, options);
    std::string normalized = result.surface.normalized_nfc;
    normalized.push_back(' ');
    normalized.append(auxiliary.surface.normalized_nfc);
    result.multi_token_query =
        MultiTokenQueryIR{.original_utf8 = std::string{utf8},
                          .normalized_nfc = std::move(normalized)};

    if (result.status == QueryStatus::error ||
        auxiliary.status == QueryStatus::error) {
        result.analyses.clear();
        result.artificial_analyses.clear();
        result.status = QueryStatus::error;
        if (result.diagnostics.empty()) {
            result.diagnostics = std::move(auxiliary.diagnostics);
        }
        return result;
    }

    if (!options.mechanisms.verbal_compounds ||
        !allows_compound(first->boundary_after.flags) ||
        !analyze_compound(*database_, result, auxiliary)) {
        result.analyses.clear();
        result.artificial_analyses.clear();
        result.status = QueryStatus::error;
        result.diagnostics = {{.code = DiagnosticCode::unsupported_multi_token,
                               .severity = DiagnosticSeverity::error,
                               .part_of_speech = std::nullopt}};
        return result;
    }
    preserve_independent_tokens(result, std::move(auxiliary));
    result.status = QueryStatus::analyzed;
    result.diagnostics.clear();
    return result;
}

std::vector<QueryResult>
Engine::analyze_line(const std::string_view utf8,
                     const AnalysisOptions options) const {
    TextTokenCursor cursor{utf8};
    auto current_token = cursor.next();
    if (!current_token) {
        return {analyze_text(utf8, options)};
    }
    if (cursor.peek() == nullptr) {
        return {analyze(*current_token, options)};
    }

    std::vector<QueryResult> results;
    auto current = analyze(*current_token, options);
    while (const auto *peeked = cursor.peek()) {
        const auto next_token = *peeked;
        auto next = analyze(next_token, options);
        static_cast<void>(cursor.next());

        const auto compound_text =
            utf8.substr(current_token->byte_begin,
                        next_token.byte_end - current_token->byte_begin);
        if (options.mechanisms.verbal_compounds &&
            allows_compound(current_token->boundary_after.flags) &&
            try_compound_query(*database_, compound_text, current, next)) {
            results.push_back(std::move(current));
            current_token = cursor.next();
            if (!current_token) {
                return results;
            }
            current = analyze(*current_token, options);
            continue;
        }

        results.push_back(std::move(current));
        current = std::move(next);
        current_token = next_token;
    }
    results.push_back(std::move(current));
    return results;
}

} // namespace words
