#include "words/lexer.hpp"

#include <gtest/gtest.h>

#include <string>

namespace words {

TEST(LatinLexerTest, KeepsDistinctSurfaceAndLookupRepresentations) {
    const LatinLexer lexer;
    const auto result = lexer.lex("JŪVĔNIS");
    ASSERT_TRUE(result) << result.error().message;
    EXPECT_EQ(result->original_utf8, "JŪVĔNIS");
    EXPECT_EQ(result->normalized_nfc, "jūvĕnis");
    EXPECT_EQ(result->orthography_ascii, "juvenis");
    EXPECT_EQ(result->lookup_ascii, "iuuenis");
    ASSERT_EQ(result->quantities.size(), 7U);
    EXPECT_EQ(result->quantities[1], VowelQuantity::long_vowel);
    EXPECT_EQ(result->quantities[3], VowelQuantity::short_vowel);
}

TEST(LatinLexerTest, CanonicalizesDecomposedMacronAndSlicesByLogicalLetter) {
    const LatinLexer lexer;
    const auto result = lexer.lex("puella\xCC\x84");
    ASSERT_TRUE(result) << result.error().message;
    EXPECT_EQ(result->normalized_nfc, "puellā");
    EXPECT_EQ(result->lookup_ascii, "puella");
    EXPECT_EQ(result->slice({5U, 1U}), "ā");
    EXPECT_EQ(result->slice({0U, 5U}), "puell");
}

TEST(LatinLexerTest, RejectsInvalidUtf8) {
    const LatinLexer lexer;
    const std::string invalid{"\xC3\x28", 2};
    const auto result = lexer.lex(invalid);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, "invalid-utf8");
}

TEST(LatinLexerTest, RejectsConflictingQuantityMarks) {
    const LatinLexer lexer;
    const auto result = lexer.lex("a\xCC\x84\xCC\x86");
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, "invalid-vowel-quantity");
}

TEST(LatinLexerTest, RejectsUnsupportedDiacritics) {
    const LatinLexer lexer;
    const auto result = lexer.lex("á");
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, "unsupported-character");
}

} // namespace words
