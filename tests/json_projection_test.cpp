#include "test_support.hpp"

#include "words/json.hpp"
#include "words/projection.hpp"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <ranges>
#include <string_view>

namespace words {
using Json = nlohmann::ordered_json;

TEST(ProjectionTest, ProjectsQuantityOfDerivedAdverbialSuffix) {
    const auto &database = test::engine().database();
    for (const auto word : {"sancte", "improbe", "perfide", "sanctē"}) {
        const auto result = test::engine().analyze(word);
        const auto document = Json::parse(analysis_json_v4(test::engine(), result));
        bool found{};
        for (std::size_t index = 0; index < result.analyses.size(); ++index) {
            const auto &analysis = result.analyses[index];
            if (!std::holds_alternative<AdverbMorphology>(analysis.morphology) ||
                std::ranges::none_of(analysis.derivation.steps(), [&](const AddonId id) {
                    return database.addon_kind(id) == AddonKind::suffix &&
                           database.suffix_string(database.suffix(id).fix) == "e";
                })) {
                continue;
            }
            found = true;
            const auto form = resolved_form(database, result.surface, analysis);
            EXPECT_TRUE(form.display.ends_with("ē")) << word;
            EXPECT_TRUE(std::ranges::any_of(
                form.quantity.positions, [&](const ResolvedQuantityPosition &position) {
                    return position.index ==
                               (word == std::string_view{"sancte"} ||
                                        word == std::string_view{"sanctē"}
                                    ? 5U
                                    : 6U) &&
                           position.quantity == VowelQuantity::long_vowel &&
                           quantity_origin_name(position.origin) == "suffix";
                })) << word;
        }
        EXPECT_TRUE(found) << word;
        EXPECT_EQ(document.at("analyses").size(), result.analyses.size());
        EXPECT_TRUE(std::ranges::any_of(document.at("analyses"),
            [](const Json &item) {
                if (item.at("partOfSpeech") != "adverb" ||
                    item.at("derivation").at("method") != "derived") {
                    return false;
                }
                return std::ranges::any_of(
                    item.at("form").at("quantity").at("positions"),
                    [](const Json &position) {
                        return position.at("origin") == "suffix" &&
                               position.at("quantity") == "long";
                    });
            })) << word;
    }

    const auto result = test::engine().analyze("sancte");
    EXPECT_TRUE(std::ranges::any_of(result.analyses, [&](const AnalysisIR &analysis) {
        if (!std::holds_alternative<AdjectiveMorphology>(analysis.morphology)) {
            return false;
        }
        const auto form = resolved_form(database, result.surface, analysis);
        return !form.display.ends_with("ē");
    }));
    const auto stable_v3 = Json::parse(analysis_json_v3(test::engine(), result));
    EXPECT_TRUE(std::ranges::all_of(stable_v3.at("analyses"), [](const Json &item) {
        return std::ranges::none_of(
            item.at("form").at("quantity").at("positions"),
            [](const Json &position) {
                return position.at("origin") == "suffix";
            });
    }));
}

TEST(ProjectionTest, KeepsUnverifiedInputQuantityOutOfCurrentDisplay) {
    const auto sancte = test::engine().analyze("sanctē");
    const auto current = Json::parse(analysis_json_v4(test::engine(), sancte));
    bool found_unknown{};
    bool found_adverb{};
    for (const auto &analysis : current.at("analyses")) {
        const auto &form = analysis.at("form");
        EXPECT_EQ(form.at("recognized"), "sanctē");
        if (analysis.at("partOfSpeech") == "adverb") {
            found_adverb = true;
            EXPECT_EQ(analysis.at("quantityMatch"), "exact");
            EXPECT_EQ(form.at("display"), "sanctē");
            EXPECT_EQ(form.at("quantity").at("annotated"), "sanctē");
        } else {
            found_unknown = true;
            EXPECT_EQ(analysis.at("quantityMatch"), "unknown");
            EXPECT_EQ(form.at("display"), "sancte");
            EXPECT_EQ(form.at("quantity").at("annotated"), nullptr);
        }
    }
    EXPECT_TRUE(found_unknown);
    EXPECT_TRUE(found_adverb);

    const auto search = Json::parse(search_json_v4(test::engine(), sancte));
    EXPECT_TRUE(std::ranges::any_of(search.at("hits"), [](const Json &hit) {
        return hit.at("quantityMatch") == "unknown" &&
               hit.at("form").at("recognized") == "sanctē" &&
               hit.at("form").at("display") == "sancte";
    }));

    // The published native v3 selector retains its established presentation.
    const auto stable = Json::parse(analysis_json_v3(test::engine(), sancte));
    EXPECT_TRUE(std::ranges::all_of(stable.at("analyses"),
        [](const Json &analysis) {
            return analysis.at("form").at("display") == "sanctē";
        }));
    const auto stable_search = Json::parse(search_json_v3(test::engine(), sancte));
    EXPECT_TRUE(std::ranges::all_of(stable_search.at("hits"),
        [](const Json &hit) {
            return hit.at("form").at("display") == "sanctē";
        }));

    const auto partially_known = test::engine().analyze("exērcitus");
    const auto partial = Json::parse(analysis_json_v4(test::engine(),
                                                      partially_known));
    ASSERT_FALSE(partial.at("analyses").empty());
    for (const auto &analysis : partial.at("analyses")) {
        EXPECT_EQ(analysis.at("quantityMatch"), "unknown");
        EXPECT_EQ(analysis.at("form").at("recognized"), "exērcitus");
        EXPECT_EQ(analysis.at("form").at("display"),
                  analysis.at("form").at("quantity").at("annotated"));
        EXPECT_FALSE(analysis.at("form").at("display")
                         .get<std::string>().starts_with("exērc"));
    }

    const auto compound = test::engine().analyze_text("amātūrus est");
    const auto compound_document =
        Json::parse(analysis_json_v4(test::engine(), compound));
    EXPECT_TRUE(std::ranges::any_of(compound_document.at("analyses"),
        [](const Json &analysis) {
            return analysis.at("derivation").at("method") == "compound" &&
                   analysis.at("form").at("recognized") == "amātūrus" &&
                   analysis.at("form").at("display") == "amaturus";
        }));
    const auto stable_compound =
        Json::parse(analysis_json_v3(test::engine(), compound));
    EXPECT_TRUE(std::ranges::any_of(stable_compound.at("analyses"),
        [](const Json &analysis) {
            return analysis.at("derivation").at("method") == "compound" &&
                   analysis.at("form").at("display") == "amātūrus";
        }));
}

TEST(ProjectionTest, KeepsKnownQuantityWhenRepeatedStemHasUnknownSlot) {
    constexpr auto army_dictionary_entry = 19'772U;
    const auto &database = test::engine().database();
    const auto result = test::engine().analyze("exercitus");
    const auto found =
        std::ranges::find_if(result.analyses, [&](const AnalysisIR &analysis) {
            return database.lexeme(analysis.lexeme).dictionary_entry + 1U ==
                   army_dictionary_entry;
        });
    ASSERT_NE(found, result.analyses.end());

    const auto &lexeme = database.lexeme(found->lexeme);
    ASSERT_EQ(database.stem_string(lexeme.stems[0]),
              database.stem_string(lexeme.stems[1]));
    // QUANTITIES.LAT records this spelling only for slot 1. Slot 2 remains
    // unknown, so it must not erase the recorded quantity.
    EXPECT_EQ(database.stem_quantity(found->lexeme, 0U).known, 1U << 5U);
    EXPECT_EQ(database.stem_quantity(found->lexeme, 1U).known, 0U);

    const auto form = resolved_form(database, result.surface, *found);
    EXPECT_EQ(form.display, "exercĭtŭs");
    EXPECT_TRUE(std::ranges::any_of(
        form.quantity.positions, [](const ResolvedQuantityPosition &position) {
            return position.index == 5U &&
                   position.quantity == VowelQuantity::short_vowel &&
                   position.origin == QuantityOrigin::stem;
        }));
}

TEST(ProjectionTest, DoesNotBorrowQuantityFromShorterStem) {
    constexpr auto adjective_dictionary_entry = 10'303U;
    const auto &database = test::engine().database();
    const auto result = test::engine().analyze("clarior");
    const auto found =
        std::ranges::find_if(result.analyses, [&](const AnalysisIR &analysis) {
            return database.lexeme(analysis.lexeme).dictionary_entry + 1U ==
                   adjective_dictionary_entry;
        });
    ASSERT_NE(found, result.analyses.end());

    const auto &lexeme = database.lexeme(found->lexeme);
    EXPECT_EQ(database.stem_string(lexeme.stems[0]), "clar");
    EXPECT_EQ(database.stem_string(lexeme.stems[2]), "clari");
    EXPECT_EQ(database.stem_quantity(found->lexeme, 0U).known, 1U << 2U);
    EXPECT_EQ(database.stem_quantity(found->lexeme, 2U).known, 0U);

    const auto form = resolved_form(database, result.surface, *found);
    EXPECT_EQ(form.stem, "clari");
    EXPECT_EQ(form.display, "clarior");
    EXPECT_TRUE(form.quantity.positions.empty());
    EXPECT_FALSE(form.quantity.annotated.has_value());
}

} // namespace words
