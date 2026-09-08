#include "words/artificial.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <optional>
#include <string_view>

namespace words {
namespace {

struct RomanDigit final {
    char symbol;
    std::uint32_t value;
};

constexpr std::array roman_digits{
    RomanDigit{.symbol = 'm', .value = 1000U},
    RomanDigit{.symbol = 'd', .value = 500U},
    RomanDigit{.symbol = 'c', .value = 100U},
    RomanDigit{.symbol = 'l', .value = 50U},
    RomanDigit{.symbol = 'x', .value = 10U},
    RomanDigit{.symbol = 'v', .value = 5U},
    RomanDigit{.symbol = 'i', .value = 1U},
};

[[nodiscard]] constexpr std::uint32_t
roman_digit_value(const char value) noexcept {
    for (const auto digit : roman_digits) {
        if (digit.symbol == value) {
            return digit.value;
        }
    }
    return 0U;
}

[[nodiscard]] bool only_roman_digits(const std::string_view word) noexcept {
    return !word.empty() && std::ranges::all_of(word, [](const char value) {
        return roman_digit_value(value) != 0U;
    });
}

[[nodiscard]] bool consume_repeated(const std::string_view word,
                                    std::size_t &cursor, const char digit,
                                    const std::size_t maximum) noexcept {
    std::size_t count = 0U;
    while (cursor < word.size() && word[cursor] == digit) {
        ++cursor;
        ++count;
    }
    return count <= maximum;
}

[[nodiscard]] bool consume_pair(const std::string_view word,
                                std::size_t &cursor,
                                const std::string_view pair) noexcept {
    if (!word.substr(cursor).starts_with(pair)) {
        return false;
    }
    cursor += pair.size();
    return true;
}

[[nodiscard]] bool consume_decimal_place(const std::string_view word,
                                         std::size_t &cursor, const char one,
                                         const char five,
                                         const char ten) noexcept {
    const std::array subtractive_five{one, five};
    const std::array subtractive_ten{one, ten};
    if (consume_pair(
            word, cursor,
            std::string_view{subtractive_ten.data(), subtractive_ten.size()}) ||
        consume_pair(word, cursor,
                     std::string_view{subtractive_five.data(),
                                      subtractive_five.size()})) {
        return true;
    }
    if (cursor < word.size() && word[cursor] == five) {
        ++cursor;
    }
    return consume_repeated(word, cursor, one, 4U);
}

[[nodiscard]] bool well_formed_roman(const std::string_view word) noexcept {
    std::size_t cursor = 0U;

    // Whitaker accepts the older additive spellings IIII/VIIII alongside
    // IV/IX.  Four repeats per decimal place reproduce that documented
    // dialect while still rejecting the looser medieval fallback.
    if (!consume_repeated(word, cursor, 'm', 4U) ||
        !consume_decimal_place(word, cursor, 'c', 'd', 'm') ||
        !consume_decimal_place(word, cursor, 'x', 'l', 'c') ||
        !consume_decimal_place(word, cursor, 'i', 'v', 'x')) {
        return false;
    }
    return cursor == word.size();
}

[[nodiscard]] std::optional<std::uint32_t>
loose_roman_value(const std::string_view word) noexcept {
    std::uint64_t total = roman_digit_value(word.back());
    std::uint32_t decremented_from = roman_digit_value(word.back());
    for (std::size_t cursor = word.size() - 1U; cursor > 0U; --cursor) {
        const auto value = roman_digit_value(word[cursor - 1U]);
        const auto next = roman_digit_value(word[cursor]);
        if (value < next || (value == next && value < decremented_from)) {
            if (total < value) {
                return std::nullopt;
            }
            total -= value;
            if (value < next) {
                decremented_from = next;
            }
        } else {
            total += value;
            if (value > next) {
                decremented_from = next;
            }
        }
        if (total > std::numeric_limits<std::uint32_t>::max()) {
            return std::nullopt;
        }
    }
    if (total == 0U) {
        return std::nullopt;
    }
    return static_cast<std::uint32_t>(total);
}

} // namespace

std::optional<RomanNumeralIR>
analyze_roman_numeral(const std::string_view normalized_word,
                      const RomanRecognition recognition) noexcept {
    if (!only_roman_digits(normalized_word)) {
        return std::nullopt;
    }
    const auto well_formed = well_formed_roman(normalized_word);
    if (!well_formed && recognition == RomanRecognition::strict) {
        return std::nullopt;
    }
    const auto value = loose_roman_value(normalized_word);
    if (!value) {
        return std::nullopt;
    }
    return RomanNumeralIR{.value = *value,
                          .well_formed = well_formed,
                          .stem = {},
                          .derivation = {}};
}

} // namespace words
