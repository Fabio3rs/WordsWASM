#include "words/artificial.hpp"

#include <gtest/gtest.h>

#include <array>
#include <string_view>

namespace words {

TEST(ArtificialTest, AcceptsWhitakersAdditiveRomanDialect) {
    constexpr std::array<std::pair<std::string_view, std::uint32_t>, 8>
        fixtures{{
            {"iv", 4U},
            {"iiii", 4U},
            {"viiii", 9U},
            {"xl", 40U},
            {"lxxxx", 90U},
            {"cm", 900U},
            {"dcccc", 900U},
            {"mmmm", 4000U},
        }};
    for (const auto &[word, value] : fixtures) {
        const auto result =
            analyze_roman_numeral(word, RomanRecognition::strict);
        ASSERT_TRUE(result.has_value()) << word;
        EXPECT_EQ(result->value, value) << word;
        EXPECT_TRUE(result->well_formed) << word;
    }
}

TEST(ArtificialTest, SeparatesStrictAndPermissiveRomanRecognition) {
    constexpr std::array<std::pair<std::string_view, std::uint32_t>, 7>
        fixtures{{
            {"iiiii", 5U},
            {"vx", 5U},
            {"il", 49U},
            {"vix", 14U},
            {"xcl", 140U},
            {"mim", 1999U},
            {"mmmmm", 5000U},
        }};
    for (const auto &[word, value] : fixtures) {
        EXPECT_FALSE(
            analyze_roman_numeral(word, RomanRecognition::strict).has_value())
            << word;
        const auto fallback =
            analyze_roman_numeral(word, RomanRecognition::permissive);
        ASSERT_TRUE(fallback.has_value()) << word;
        EXPECT_EQ(fallback->value, value) << word;
        EXPECT_FALSE(fallback->well_formed) << word;
    }
    EXPECT_FALSE(analyze_roman_numeral("mixup", RomanRecognition::permissive)
                     .has_value());
}

} // namespace words
