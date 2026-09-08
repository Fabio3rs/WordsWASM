#pragma once

#include "words/model.hpp"

#include <optional>
#include <string_view>

namespace words {

enum class RomanRecognition : std::uint8_t {
    strict,
    permissive,
};

[[nodiscard]] constexpr NumeralMorphology roman_numeral_morphology() noexcept {
    return NumeralMorphology{
        .declension = 2U,
        .variant = 0U,
        .grammatical_case = GrammaticalCase::unknown,
        .number = GrammaticalNumber::unknown,
        .gender = Gender::unknown,
        .numeral_type = NumeralType::cardinal,
    };
}

[[nodiscard]] std::optional<RomanNumeralIR>
analyze_roman_numeral(std::string_view normalized_word,
                      RomanRecognition recognition) noexcept;

} // namespace words
