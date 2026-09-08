#include "words/projection.hpp"

#include "words/artificial.hpp"
#include "words/semantics.hpp"

#include <array>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

namespace words {
namespace {

[[nodiscard]] constexpr AnalysisDictionaryOrder
dictionary_order(const DictionaryKind dictionary) noexcept {
    return dictionary == DictionaryKind::unique
               ? AnalysisDictionaryOrder::unique
               : AnalysisDictionaryOrder::general;
}

[[nodiscard]] DerivationOrder derivation_order(const Database &database,
                                               const DictionaryKind dictionary,
                                               const DerivationIR &derivation) {
    if (derivation.rewritten_form &&
        !derivation.rewritten_form->steps().empty()) {
        return database.rewrite(derivation.rewritten_form->steps().front())
                           .kind == RewriteKind::syncope
                   ? DerivationOrder::syncope
                   : DerivationOrder::orthographic;
    }
    if (dictionary == DictionaryKind::unique) {
        return DerivationOrder::unique;
    }
    return derivation.count == 0U ? DerivationOrder::regular
                                  : DerivationOrder::derived;
}

[[nodiscard]] MorphologyOrderKey
morphology_order_key(const Morphology &morphology) {
    MorphologyOrderKey key;
    std::visit(
        [&key](const auto &value) {
            using Value = std::remove_cvref_t<decltype(value)>;
            const auto number = [](const std::uint8_t ordinal) {
                return ordinal == 0U ? std::string{} : std::to_string(ordinal);
            };
            const auto token = [](const std::string_view token_value) {
                return std::string{token_value};
            };
            if constexpr (std::is_same_v<Value, NounMorphology> ||
                          std::is_same_v<Value, PronounMorphology>) {
                key = MorphologyOrderKey{
                    number(value.declension), number(value.variant),
                    token(case_name(value.grammatical_case)),
                    token(number_name(value.number)),
                    token(gender_name(value.gender))};
            } else if constexpr (std::is_same_v<Value, AdjectiveMorphology>) {
                key =
                    MorphologyOrderKey{number(value.declension),
                                       number(value.variant),
                                       token(case_name(value.grammatical_case)),
                                       token(number_name(value.number)),
                                       token(gender_name(value.gender)),
                                       token(degree_name(value.degree))};
            } else if constexpr (std::is_same_v<Value, NumeralMorphology>) {
                key = MorphologyOrderKey{
                    number(value.declension),
                    number(value.variant),
                    token(case_name(value.grammatical_case)),
                    token(number_name(value.number)),
                    token(gender_name(value.gender)),
                    token(numeral_type_name(value.numeral_type))};
            } else if constexpr (std::is_same_v<Value, AdverbMorphology>) {
                key = MorphologyOrderKey{token(degree_name(value.degree))};
            } else if constexpr (std::is_same_v<Value, VerbMorphology>) {
                key =
                    MorphologyOrderKey{number(value.conjugation),
                                       number(value.variant),
                                       token(tense_name(value.tense)),
                                       token(voice_name(value.voice)),
                                       token(mood_name(value.mood)),
                                       number(std::to_underlying(value.person)),
                                       token(number_name(value.number))};
            } else if constexpr (std::is_same_v<Value, ParticipleMorphology>) {
                key =
                    MorphologyOrderKey{number(value.conjugation),
                                       number(value.variant),
                                       token(case_name(value.grammatical_case)),
                                       token(number_name(value.number)),
                                       token(gender_name(value.gender)),
                                       token(tense_name(value.tense)),
                                       token(voice_name(value.voice))};
            } else if constexpr (std::is_same_v<Value, SupineMorphology>) {
                key = MorphologyOrderKey{
                    number(value.conjugation), number(value.variant),
                    token(case_name(value.grammatical_case)),
                    token(number_name(value.number)),
                    token(gender_name(value.gender))};
            } else if constexpr (std::is_same_v<Value, PrepositionMorphology>) {
                key = MorphologyOrderKey{token(case_name(value.governs))};
            } else if constexpr (!std::is_same_v<Value, InvariableMorphology>) {
                static_assert(sizeof(Value) == 0U,
                              "new morphology requires an ordering key");
            }
        },
        morphology);
    return key;
}

} // namespace

AnalysisOrderKey analysis_order_key(const Database &database,
                                    const SurfaceForm &surface,
                                    const AnalysisIR &analysis) {
    const auto &lexeme = database.lexeme(analysis.lexeme);
    const auto stem =
        analysis.derivation.rewritten_form
            ? std::string_view{analysis.derivation.rewritten_form->stem}
            : surface.slice(analysis.stem);
    const auto ending =
        analysis.derivation.rewritten_form
            ? std::string_view{analysis.derivation.rewritten_form->ending}
            : surface.slice(analysis.ending);
    return AnalysisOrderKey{
        .dictionary = dictionary_order(lexeme.dictionary),
        .dictionary_entry = lexeme.dictionary_entry + 1U,
        .part_of_speech = std::string{morphology_part_name(
            analysis.morphology, lexeme.part_of_speech)},
        .stem_key = analysis.stem_key,
        .stem = std::string{stem},
        .ending = std::string{ending},
        .morphology = morphology_order_key(analysis.morphology),
        .derivation_order =
            derivation_order(database, lexeme.dictionary, analysis.derivation),
        .source_derivation = analysis.derivation,
        .auxiliary_derivation = {},
        .compound_kind = CompoundKind::unknown,
        .auxiliary = {},
        .artificial_value = 0U,
        .artificial_malformed = false,
    };
}

AnalysisOrderKey analysis_order_key(const Database &database,
                                    const CompoundAnalysisIR &analysis) {
    const auto &lexeme = database.lexeme(analysis.lexeme);
    std::string stem =
        analysis.kind == CompoundKind::iri ? "SUPINE + " : "PPL+";
    stem.append(analysis.auxiliary);
    return AnalysisOrderKey{
        .dictionary = dictionary_order(lexeme.dictionary),
        .dictionary_entry = lexeme.dictionary_entry + 1U,
        .part_of_speech = std::string{lexical_part_name(PartOfSpeech::verb)},
        .stem_key = 0U,
        .stem = std::move(stem),
        .ending = {},
        .morphology = morphology_order_key(Morphology{analysis.morphology}),
        .derivation_order = DerivationOrder::compound,
        .source_derivation = analysis.source_derivation,
        .auxiliary_derivation = analysis.auxiliary_derivation,
        .compound_kind = analysis.kind,
        .auxiliary = analysis.auxiliary,
        .artificial_value = 0U,
        .artificial_malformed = false,
    };
}

AnalysisOrderKey analysis_order_key(const RomanNumeralIR &analysis) {
    return AnalysisOrderKey{
        .dictionary = AnalysisDictionaryOrder::roman_numeral,
        .dictionary_entry = 0U,
        .part_of_speech = std::string{lexical_part_name(PartOfSpeech::numeral)},
        .stem_key = 0U,
        .stem = {},
        .ending = {},
        .morphology =
            morphology_order_key(Morphology{roman_numeral_morphology()}),
        .derivation_order = DerivationOrder::regular,
        .source_derivation = analysis.derivation,
        .auxiliary_derivation = {},
        .compound_kind = CompoundKind::unknown,
        .auxiliary = {},
        .artificial_value = analysis.value,
        .artificial_malformed = !analysis.well_formed,
    };
}

} // namespace words
