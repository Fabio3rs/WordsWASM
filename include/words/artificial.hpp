#pragma once

#include "words/model.hpp"

#include <optional>
#include <string_view>

namespace words {

enum class RomanRecognition : std::uint8_t {
    strict,
    permissive,
};

[[nodiscard]] std::optional<RomanNumeralIR>
analyze_roman_numeral(std::string_view normalized_word,
                      RomanRecognition recognition) noexcept;

} // namespace words
