#include "test_support.hpp"

#include "words/database.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <ranges>

namespace words {

TEST(DatabaseTest, LoadsDensePocAndFindsRealData) {
    auto database = Database::load_dense_poc(test::read_database());
    ASSERT_TRUE(database) << database.error().message;
    EXPECT_FALSE((*database)->lookup_stem("puell").empty());
    EXPECT_FALSE((*database)->lookup_ending("ae").empty());
    EXPECT_FALSE((*database)->lookup_ending("").empty());
    EXPECT_TRUE((*database)->lookup_stem("zzzzzz").empty());
}

TEST(DatabaseTest, LoadsInflectionAndSparseStemQuantities) {
    constexpr auto first_declension_ablative_rule = RuleId{146U};
    constexpr auto first_declension_nominative_rule = RuleId{148U};
    constexpr std::uint32_t first_long_mal_entry = 26'263U;
    constexpr std::uint32_t last_long_mal_entry = 26'266U;
    constexpr std::uint32_t short_evil_entry = 26'267U;
    constexpr std::uint32_t short_bad_entry = 26'269U;

    auto database = Database::load_dense_poc(test::read_database());
    ASSERT_TRUE(database) << database.error().message;

    const auto ablative =
        (*database)->inflection_quantity(first_declension_ablative_rule);
    EXPECT_EQ(ablative.known, 1U);
    EXPECT_EQ(ablative.long_vowel, 1U);
    const auto nominative =
        (*database)->inflection_quantity(first_declension_nominative_rule);
    EXPECT_EQ(nominative.known, 1U);
    EXPECT_EQ(nominative.long_vowel, 0U);

    for (const auto &reference : (*database)->lookup_stem("mal")) {
        const auto entry =
            (*database)->lexeme(reference.lexeme).dictionary_entry + 1U;
        const auto quantity = (*database)->stem_quantity(
            reference.lexeme, reference.lexical_slot);
        if (entry >= first_long_mal_entry && entry <= last_long_mal_entry) {
            EXPECT_EQ(quantity.known, 2U) << entry;
            EXPECT_EQ(quantity.long_vowel, 2U) << entry;
        } else if (entry == short_evil_entry || entry == short_bad_entry) {
            EXPECT_EQ(quantity.known, 2U) << entry;
            EXPECT_EQ(quantity.long_vowel, 0U) << entry;
        }
    }
}

TEST(DatabaseTest, DecodesAdjectiveDegreePayloads) {
    auto database = Database::load_dense_poc(test::read_database());
    ASSERT_TRUE(database) << database.error().message;

    const auto has_lexical_degree = [&](const std::string_view stem,
                                        const Degree degree) {
        return std::ranges::any_of(
            (*database)->lookup_stem(stem),
            [&](const StemReference &reference) {
                const auto &lexeme = (*database)->lexeme(reference.lexeme);
                return lexeme.part_of_speech == PartOfSpeech::adjective &&
                       lexeme.adjective_degree == degree;
            });
    };
    EXPECT_TRUE(has_lexical_degree("pulcher", Degree::unknown));
    EXPECT_TRUE(has_lexical_degree("maxi", Degree::superlative));

    const auto has_positive_rule = std::ranges::any_of(
        (*database)->lookup_ending("us"), [&](const RuleId id) {
            const auto &rule = (*database)->rule(id);
            return rule.part_of_speech == PartOfSpeech::adjective &&
                   rule.adjective_degree == Degree::positive;
        });
    EXPECT_TRUE(has_positive_rule);
}

TEST(DatabaseTest, DecodesRemainingSemanticPayloads) {
    auto database = Database::load_dense_poc(test::read_database());
    ASSERT_TRUE(database) << database.error().message;

    const auto has_lexeme = [&](const std::string_view stem,
                                const auto predicate) {
        return std::ranges::any_of(
            (*database)->lookup_stem(stem),
            [&](const StemReference &reference) {
                return predicate((*database)->lexeme(reference.lexeme));
            });
    };
    EXPECT_TRUE(has_lexeme("un", [](const LexemeRecord &lexeme) {
        return lexeme.part_of_speech == PartOfSpeech::numeral &&
               lexeme.numeral_value == 1U;
    }));
    EXPECT_TRUE(has_lexeme("bene", [](const LexemeRecord &lexeme) {
        return lexeme.part_of_speech == PartOfSpeech::adverb &&
               lexeme.adverb_degree == Degree::unknown;
    }));
    EXPECT_TRUE(has_lexeme("am", [](const LexemeRecord &lexeme) {
        return lexeme.part_of_speech == PartOfSpeech::verb &&
               lexeme.declension == 1U;
    }));
    EXPECT_TRUE(has_lexeme("cum", [](const LexemeRecord &lexeme) {
        return lexeme.part_of_speech == PartOfSpeech::preposition &&
               lexeme.governs == GrammaticalCase::ablative;
    }));

    EXPECT_TRUE(std::ranges::any_of(
        (*database)->lookup_ending("o"), [&](const RuleId id) {
            const auto &rule = (*database)->rule(id);
            return rule.part_of_speech == PartOfSpeech::verb &&
                   rule.tense == Tense::present &&
                   rule.voice == Voice::active &&
                   rule.mood == Mood::indicative && rule.person == 1U;
        }));
}

TEST(DatabaseTest, LoadsAndIndexesUniqueAnalyses) {
    auto database = Database::load_dense_poc(test::read_database());
    ASSERT_TRUE(database) << database.error().message;

    const auto homographs = (*database)->lookup_unique("eadem");
    ASSERT_EQ(homographs.size(), 3U);
    EXPECT_TRUE(
        std::ranges::all_of(homographs, [&](const UniqueReference &reference) {
            const auto &lexeme = (*database)->lexeme(reference.lexeme);
            return lexeme.dictionary == DictionaryKind::unique &&
                   lexeme.part_of_speech == PartOfSpeech::pronoun &&
                   std::holds_alternative<PronounMorphology>(
                       reference.morphology);
        }));

    // The index uses the same i/j and u/v equivalence as Ada, so alternate
    // orthography reaches the stored form without duplicating keys.
    EXPECT_EQ((*database)->lookup_unique("mauis").size(), 1U);
    EXPECT_TRUE((*database)->lookup_unique("zzzzzz").empty());
}

TEST(DatabaseTest, LoadsTypedRewriteMicroRules) {
    const auto rules = test::engine().database().rewrites();
    ASSERT_EQ(rules.size(), 170U);
    EXPECT_EQ(
        std::ranges::count(rules, RewriteKind::syncope, &RewriteRule::kind),
        11U);
    EXPECT_EQ(std::ranges::count(rules, RewriteKind::orthographic,
                                 &RewriteRule::kind),
              159U);
    const auto &first = rules.front();
    EXPECT_EQ(first.id, RewriteId{0U});
    EXPECT_EQ(first.kind, RewriteKind::syncope);
    EXPECT_EQ(first.scope, RewriteScope::internal);
    EXPECT_EQ(first.priority, 0U);
    EXPECT_TRUE(first.scan_reverse);
    EXPECT_EQ(first.operation, RewriteOperation::literal);
    EXPECT_EQ(first.stage, RewriteStage::main);
    EXPECT_EQ(first.constraint, RewriteConstraint::none);
    EXPECT_EQ(first.required_part, PartOfSpeech::verb);
    EXPECT_EQ(first.required_stem_key, 3U);
    EXPECT_EQ(test::engine().database().rewrite_string(first.before), "ii");
    EXPECT_EQ(test::engine().database().rewrite_string(first.after), "ivi");
    EXPECT_EQ(test::engine().database().rewrite_string(first.name),
              "perfect-ivi-uncontracted");

    const auto slur = std::ranges::find_if(rules, [&](const RewriteRule &rule) {
        return test::engine().database().rewrite_string(rule.name) ==
               "orth-slur-ob";
    });
    ASSERT_NE(slur, rules.end());
    EXPECT_EQ(slur->operation, RewriteOperation::slur);
    EXPECT_EQ(slur->stage, RewriteStage::early);
}

TEST(DatabaseTest, LoadsAndIndexesSuffixRules) {
    auto database = Database::load_dense_poc(test::read_database());
    ASSERT_TRUE(database) << database.error().message;

    const auto suffixes = (*database)->lookup_suffix("icul");
    ASSERT_EQ(suffixes.size(), 3U);
    EXPECT_LE((*database)->maximum_suffix_size(), 8U);
    EXPECT_TRUE(std::ranges::all_of(suffixes, [&](const AddonId id) {
        const auto &suffix = (*database)->suffix(id);
        return suffix.root == PartOfSpeech::noun &&
               suffix.target == PartOfSpeech::noun &&
               (*database)->suffix_string(suffix.fix) == "icul";
    }));
}

TEST(DatabaseTest, LoadsAndIndexesPrefixRules) {
    auto database = Database::load_dense_poc(test::read_database());
    ASSERT_TRUE(database) << database.error().message;

    const auto prefixes = (*database)->lookup_prefix("archi");
    ASSERT_FALSE(prefixes.empty());
    EXPECT_LE((*database)->maximum_prefix_size(), 18U);
    EXPECT_TRUE(std::ranges::all_of(prefixes, [&](const AddonId id) {
        const auto &prefix = (*database)->prefix(id);
        return (*database)->addon_kind(id) == AddonKind::prefix &&
               prefix.root == PartOfSpeech::unknown &&
               prefix.target == PartOfSpeech::unknown &&
               (*database)->prefix_string(prefix.fix) == "archi";
    }));

    const auto assimilated = (*database)->lookup_prefix("ap");
    ASSERT_FALSE(assimilated.empty());
    EXPECT_TRUE(std::ranges::any_of(assimilated, [&](const AddonId id) {
        return (*database)->prefix(id).connector == 'p';
    }));
}

TEST(DatabaseTest, SeparatesTickonsTackonsAndPackons) {
    auto database = Database::load_dense_poc(test::read_database());
    ASSERT_TRUE(database) << database.error().message;

    const auto tickons = (*database)->lookup_tickon("ec");
    ASSERT_FALSE(tickons.empty());
    EXPECT_TRUE(std::ranges::all_of(tickons, [&](const AddonId id) {
        return (*database)->addon_kind(id) == AddonKind::tickon &&
               (*database)->prefix(id).root == PartOfSpeech::pack;
    }));

    const auto tackons = (*database)->lookup_tackon("que");
    ASSERT_FALSE(tackons.empty());
    EXPECT_TRUE(std::ranges::all_of(tackons, [&](const AddonId id) {
        const auto &tackon = (*database)->tackon(id);
        return (*database)->addon_kind(id) == AddonKind::tackon &&
               tackon.enclitic && !tackon.packon;
    }));

    const auto packons = (*database)->lookup_packon("dam");
    ASSERT_FALSE(packons.empty());
    EXPECT_TRUE(std::ranges::all_of(packons, [&](const AddonId id) {
        const auto &packon = (*database)->tackon(id);
        return (*database)->addon_kind(id) == AddonKind::packon &&
               packon.base == PartOfSpeech::pack && packon.packon;
    }));
}

TEST(DatabaseTest, RejectsWrongMagic) {
    auto bytes = test::read_database();
    bytes[0] = std::byte{'X'};
    const auto database = Database::load_dense_poc(std::move(bytes));
    ASSERT_FALSE(database);
    EXPECT_EQ(database.error().code, "invalid-magic");
}

TEST(DatabaseTest, RejectsUnsupportedProfile) {
    auto bytes = test::read_database();
    bytes[20] = std::byte{3};
    const auto database = Database::load_dense_poc(std::move(bytes));
    ASSERT_FALSE(database);
    EXPECT_EQ(database.error().code, "unsupported-profile");
}

TEST(DatabaseTest, RejectsTruncation) {
    auto bytes = test::read_database();
    bytes.resize(20U);
    const auto database = Database::load_dense_poc(std::move(bytes));
    ASSERT_FALSE(database);
    EXPECT_EQ(database.error().code, "truncated-database");
}

TEST(DatabaseTest, RejectsPayloadCorruption) {
    auto bytes = test::read_database();
    bytes.back() ^= std::byte{1};
    const auto database = Database::load_dense_poc(std::move(bytes));
    ASSERT_FALSE(database);
    EXPECT_EQ(database.error().code, "checksum-mismatch");
}

} // namespace words
