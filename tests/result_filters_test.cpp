#include "../src/result_filters.hpp"
#include "words/semantics.hpp"

#include <gtest/gtest.h>

namespace words::client {
namespace {

JsonDocument candidate(const JsonDocument &reasons, const int id) {
    return JsonDocument{{"id", id}, {"assessment", {
        {"whitakerTrim", {{"reasons", reasons}}},
        {"notices", {"source-disagreement"}},
    }}};
}

JsonDocument document(const char *key, const JsonDocument &values) {
    return JsonDocument{{"status", "analyzed"}, {key, values},
                        {"diagnostics", JsonDocument::array()}};
}

TEST(ResultFiltersTest, EveryExistingReasonAndNoOp) {
    for (std::size_t index = 0; index < whitaker_trim_reason_count; ++index) {
        const auto name = whitaker_trim_reason_name(
            static_cast<WhitakerTrimReason>(index));
        const auto filters = parse_trim_filters(name);
        ASSERT_TRUE(filters);
        for (const auto *key : {"hits", "analyses"}) {
            auto result = document(key, JsonDocument::array({
                candidate(JsonDocument::array(), 1),
                candidate(JsonDocument::array({name}), 2),
                candidate(JsonDocument::array(), 3),
            }));
            const auto original = result;
            filter_result(result, {});
            EXPECT_EQ(result.dump(), original.dump());
            filter_result(result, *filters);
            ASSERT_EQ(result.at(key).size(), 2U);
            EXPECT_EQ(result.at(key).at(0), original.at(key).at(0));
            EXPECT_EQ(result.at(key).at(1), original.at(key).at(2));
            EXPECT_TRUE(result.at("diagnostics").empty());
        }
    }
}

TEST(ResultFiltersTest, NestedListsAndLocalExhaustion) {
    const auto filters = parse_trim_filters("deponent-active-form");
    ASSERT_TRUE(filters);
    for (const auto *key : {"hits", "analyses"}) {
        const auto hidden = candidate({"invalid-imperative-person", "deponent-active-form"}, 1);
        const auto visible = candidate(JsonDocument::array(), 2);
        auto result = document(key, JsonDocument::array({hidden}));
        result["diagnostics"].push_back({{"code", "existing"}});
        result["tokens"] = JsonDocument::array({
            document(key, JsonDocument::array({hidden})),
            document(key, JsonDocument::array()),
            document(key, JsonDocument::array({visible})),
        });
        result["suggestions"] = JsonDocument::array({
            {{"segments", {{{key, {hidden}}}, {{key, {visible}}}}}},
            {{"segments", {{{key, {hidden, visible}}}, {{key, {visible}}}}}},
        });
        filter_result(result, *filters);
        EXPECT_EQ(result.at("status"), "analyzed");
        EXPECT_TRUE(result.at(key).empty());
        ASSERT_EQ(result.at("diagnostics").size(), 2U);
        EXPECT_EQ(result.at("diagnostics").back().at("code"), "all-analyses-filtered");
        EXPECT_EQ(result.at("tokens").size(), 3U);
        EXPECT_EQ(result.at("tokens").at(0).at("diagnostics").size(), 1U);
        EXPECT_TRUE(result.at("tokens").at(1).at("diagnostics").empty());
        EXPECT_EQ(result.at("suggestions").size(), 1U);
        const auto once = result;
        filter_result(result, *filters);
        EXPECT_EQ(result, once);
    }
}

TEST(ResultFiltersTest, ParsesOnlyKnownReasonsAndDeduplicates) {
    EXPECT_TRUE(parse_trim_filters("none")->exclude_whitaker_trim_reasons.empty());
    const auto parsed = parse_trim_filters("deponent-active-form,deponent-active-form,invalid-imperative-person");
    ASSERT_TRUE(parsed);
    EXPECT_EQ(parsed->exclude_whitaker_trim_reasons.size(), 2U);
    for (const auto *invalid : {"", "all", "none,deponent-active-form", "deponent-active-form,", "invented"}) {
        EXPECT_FALSE(parse_trim_filters(invalid));
    }
}

} // namespace
} // namespace words::client
