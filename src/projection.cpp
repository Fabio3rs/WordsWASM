#include "words/projection.hpp"

#include "whitakers-words/poc/latin-unicode/latin_utf8.hpp"
#include "words/artificial.hpp"
#include "words/lexer.hpp"
#include "words/semantics.hpp"

#include <algorithm>
#include <array>
#include <functional>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace words {
namespace {

using poc::latin_unicode::EncodedScalar;
using poc::latin_unicode::LatinGlyph;
using poc::latin_unicode::LatinQuantity;

[[nodiscard]] constexpr bool is_vowel(const char value) noexcept {
    return value == 'a' || value == 'e' || value == 'i' || value == 'o' ||
           value == 'u' || value == 'y';
}

[[nodiscard]] constexpr char lookup_letter(const char value) noexcept {
    if (value == 'j') {
        return 'i';
    }
    if (value == 'v') {
        return 'u';
    }
    return value;
}

[[nodiscard]] bool lookup_prefix_matches(const std::string_view stored,
                                         const std::string_view recognized) {
    return stored.size() <= recognized.size() &&
           std::ranges::equal(stored, recognized.substr(0U, stored.size()), {},
                              lookup_letter, std::identity{});
}

[[nodiscard]] constexpr LatinQuantity
latin_quantity(const VowelQuantity value) noexcept {
    if (value == VowelQuantity::short_vowel) {
        return LatinQuantity::short_vowel;
    }
    if (value == VowelQuantity::long_vowel) {
        return LatinQuantity::long_vowel;
    }
    return LatinQuantity::unknown;
}

void append_encoded(std::string &output, const EncodedScalar &scalar) {
    output.append(scalar.bytes.data(), scalar.byte_count);
}

[[nodiscard]] std::string
render_quantities(const SurfaceForm &surface,
                  const std::span<const VowelQuantity> quantities) {
    if (surface.orthography_ascii.size() != quantities.size()) {
        return surface.normalized_nfc;
    }
    std::string output;
    output.reserve(surface.normalized_nfc.size() + quantities.size());
    for (std::size_t index{}; index < quantities.size(); ++index) {
        const auto encoded = poc::latin_unicode::encode_latin_glyph_nfc(
            LatinGlyph{.base = surface.orthography_ascii[index],
                       .quantity = latin_quantity(quantities[index])});
        if (!encoded) {
            output = surface.normalized_nfc;
            return output; // Fix -Werror,-Wnrvo
        }
        append_encoded(output, (*encoded)[0]);
        append_encoded(output, (*encoded)[1]);
    }
    return output;
}

void apply_mask(const QuantityMask mask, const std::size_t segment_size,
                const std::size_t offset, const QuantityOrigin origin,
                const SurfaceForm &recognized,
                std::vector<VowelQuantity> &database_quantities,
                std::vector<ResolvedQuantityPosition> &positions) {
    constexpr auto mask_bits = std::numeric_limits<std::uint32_t>::digits;
    for (std::size_t relative{};
         relative < segment_size && relative < mask_bits; ++relative) {
        const auto bit = std::uint32_t{1U} << relative;
        const auto index = offset + relative;
        if ((mask.known & bit) == 0U ||
            index >= recognized.orthography_ascii.size() ||
            !is_vowel(recognized.orthography_ascii[index])) {
            continue;
        }
        const auto quantity = (mask.long_vowel & bit) != 0U
                                  ? VowelQuantity::long_vowel
                                  : VowelQuantity::short_vowel;
        database_quantities[index] = quantity;
        positions.push_back(ResolvedQuantityPosition{
            .index = static_cast<std::uint32_t>(index),
            .quantity = quantity,
            .origin = origin,
        });
    }
}

[[nodiscard]] std::optional<QuantityMask>
consensus_stem_quantity(const Database &database, const AnalysisIR &analysis,
                        const std::string_view recognized_stem) {
    const auto &lexeme = database.lexeme(analysis.lexeme);
    std::size_t longest{};
    std::vector<std::uint8_t> slots;
    for (std::uint8_t slot{}; slot < lexeme.stems.size(); ++slot) {
        const auto stored = database.stem_string(lexeme.stems[slot]);
        if (stored.empty() || !lookup_prefix_matches(stored, recognized_stem)) {
            continue;
        }
        if (stored.size() > longest) {
            longest = stored.size();
            slots.clear();
        }
        if (stored.size() == longest) {
            slots.push_back(slot);
        }
    }
    if (slots.empty()) {
        return std::nullopt;
    }

    QuantityMask consensus;
    std::uint32_t conflicts{};
    for (const auto slot : slots) {
        const auto candidate = database.stem_quantity(analysis.lexeme, slot);
        conflicts |= (consensus.long_vowel ^ candidate.long_vowel) &
                     consensus.known & candidate.known;
        consensus.known |= candidate.known;
        consensus.long_vowel |= candidate.long_vowel;
    }
    // Repeated identical stems are common in DICTLINE. Missing evidence in
    // one slot is not disagreement; an actual long/short conflict is.
    consensus.known &= ~conflicts;
    consensus.long_vowel &= consensus.known;
    return consensus;
}

[[nodiscard]] std::optional<SurfaceForm>
lex_segment(const std::string_view value) {
    if (value.empty()) {
        SurfaceForm empty;
        return empty;
    }
    auto parsed = LatinLexer{}.lex(value);
    if (!parsed) {
        return std::nullopt;
    }
    return std::move(*parsed);
}

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

ResolvedForm unquantified_form(std::string stem, const std::uint8_t stem_key,
                               std::string ending, std::string recognized) {
    return ResolvedForm{
        .stem = std::move(stem),
        .stem_key = stem_key,
        .ending = std::move(ending),
        .recognized = recognized,
        .display = std::move(recognized),
        .quantity = {},
    };
}

ResolvedForm resolved_form(const Database &database, const SurfaceForm &surface,
                           const AnalysisIR &analysis,
                           const bool include_suffix_quantity) {
    auto form =
        unquantified_form(analysis.derivation.rewritten_form
                              ? analysis.derivation.rewritten_form->stem
                              : std::string{surface.slice(analysis.stem)},
                          analysis.stem_key,
                          analysis.derivation.rewritten_form
                              ? analysis.derivation.rewritten_form->ending
                              : std::string{surface.slice(analysis.ending)},
                          {});
    form.recognized = form.stem + form.ending;
    form.display = form.recognized;

    // UNIQUES entries and rule-less analyses do not carry alignable inflection
    // evidence. Returning an explicit `none` is safer than borrowing quantities
    // from a merely similar dictionary spelling.
    const auto &lexeme = database.lexeme(analysis.lexeme);
    if (lexeme.dictionary == DictionaryKind::unique || !analysis.rule) {
        return form;
    }

    const auto recognized = lex_segment(form.recognized);
    const auto stem = lex_segment(form.stem);
    const auto ending = lex_segment(form.ending);
    if (!recognized || !stem || !ending ||
        recognized->quantities.size() !=
            stem->quantities.size() + ending->quantities.size()) {
        return form;
    }

    std::vector<VowelQuantity> database_quantities(
        recognized->quantities.size(), VowelQuantity::unknown);
    std::size_t lexical_stem_size = stem->quantities.size();
    const SuffixRule *quantity_suffix = nullptr;
    for (const auto addon_id : analysis.derivation.steps()) {
        if (!include_suffix_quantity) {
            break;
        }
        if (database.addon_kind(addon_id) != AddonKind::suffix) {
            continue;
        }
        const auto &suffix = database.suffix(addon_id);
        const auto fix = database.suffix_string(suffix.fix);
        if (fix.size() <= stem->lookup_ascii.size() &&
            lookup_prefix_matches(
                fix, std::string_view{stem->lookup_ascii}.substr(
                         stem->lookup_ascii.size() - fix.size()))) {
            quantity_suffix = &suffix;
            lexical_stem_size -= fix.size();
        }
        break;
    }
    if (lexical_stem_size != 0U) {
        if (const auto mask = consensus_stem_quantity(database, analysis,
                                                      std::string_view{stem->lookup_ascii}.substr(0U, lexical_stem_size))) {
            apply_mask(*mask, lexical_stem_size, 0U, QuantityOrigin::stem,
                       *recognized, database_quantities,
                       form.quantity.positions);
        }
    }
    if (quantity_suffix != nullptr) {
        apply_mask(quantity_suffix->quantity,
                   stem->quantities.size() - lexical_stem_size,
                   lexical_stem_size, QuantityOrigin::suffix, *recognized,
                   database_quantities, form.quantity.positions);
    }

    const auto stored_ending =
        database.ending_string(database.rule(*analysis.rule).ending);
    if (ending->lookup_ascii.size() == stored_ending.size() &&
        lookup_prefix_matches(stored_ending, ending->lookup_ascii)) {
        apply_mask(database.inflection_quantity(*analysis.rule),
                   ending->quantities.size(), stem->quantities.size(),
                   QuantityOrigin::ending, *recognized, database_quantities,
                   form.quantity.positions);
    }

    if (form.quantity.positions.empty()) {
        return form;
    }

    form.quantity.annotated =
        render_quantities(*recognized, database_quantities);
    const auto vowel_count = static_cast<std::size_t>(
        std::ranges::count_if(recognized->orthography_ascii, is_vowel));
    form.quantity.coverage = form.quantity.positions.size() == vowel_count
                                 ? QuantityCoverage::complete
                                 : QuantityCoverage::partial;

    auto display_quantities = recognized->quantities;
    for (const auto &position : form.quantity.positions) {
        display_quantities[position.index] = position.quantity;
    }
    form.display = render_quantities(*recognized, display_quantities);
    return form;
}

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
