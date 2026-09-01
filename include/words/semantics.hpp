#pragma once

#include "words/model.hpp"

#include <cstdint>
#include <string>
#include <string_view>

namespace words {

[[nodiscard]] std::string_view age_name(Age value) noexcept;
[[nodiscard]] std::string_view
lexical_frequency_name(LexicalFrequency value) noexcept;
[[nodiscard]] std::string_view
rule_frequency_name(RuleFrequency value) noexcept;
[[nodiscard]] std::string_view subject_name(SubjectArea value) noexcept;
[[nodiscard]] std::string_view geography_name(Geography value) noexcept;
[[nodiscard]] std::string_view source_name(Source value) noexcept;
[[nodiscard]] std::string_view noun_kind_name(NounKind value) noexcept;
[[nodiscard]] std::string_view pronoun_kind_name(PronounKind value) noexcept;
[[nodiscard]] std::string_view gender_name(Gender value) noexcept;
[[nodiscard]] std::string_view case_name(GrammaticalCase value) noexcept;
[[nodiscard]] std::string_view number_name(GrammaticalNumber value) noexcept;
[[nodiscard]] std::string_view degree_name(Degree value) noexcept;
[[nodiscard]] std::string_view numeral_type_name(NumeralType value) noexcept;
[[nodiscard]] std::string_view tense_name(Tense value) noexcept;
[[nodiscard]] std::string_view voice_name(Voice value) noexcept;
[[nodiscard]] std::string_view mood_name(Mood value) noexcept;
[[nodiscard]] std::string_view verb_kind_name(VerbKind value) noexcept;
[[nodiscard]] constexpr std::optional<GrammaticalCase>
governed_case(const VerbKind value) noexcept {
    if (value == VerbKind::governs_genitive) {
        return GrammaticalCase::genitive;
    }
    if (value == VerbKind::governs_dative) {
        return GrammaticalCase::dative;
    }
    if (value == VerbKind::governs_ablative) {
        return GrammaticalCase::ablative;
    }
    return std::nullopt;
}
[[nodiscard]] std::string_view lexical_part_name(PartOfSpeech value) noexcept;
[[nodiscard]] std::string_view compound_kind_name(CompoundKind value) noexcept;
[[nodiscard]] std::string_view status_name(QueryStatus value) noexcept;
[[nodiscard]] std::string_view addon_kind_name(AddonKind value) noexcept;
[[nodiscard]] std::string_view rewrite_kind_name(RewriteKind value) noexcept;
[[nodiscard]] std::string_view
quantity_match_name(QuantityMatch value) noexcept;
[[nodiscard]] std::string normalized_meaning(std::string_view meaning);

} // namespace words
