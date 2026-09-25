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

} // namespace words
