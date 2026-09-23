#include "result_filters.hpp"
#include "words/semantics.hpp"

#include <algorithm>
#include <optional>

namespace words::client {
namespace {

bool filter_list(JsonDocument &values, const ResultFilters &filters) {
    const bool had_values = !values.empty();
    auto &array = values.get_ref<JsonDocument::array_t &>();
    std::erase_if(array, [&](const JsonDocument &analysis) {
        const auto &reasons = analysis.at("assessment")
                                 .at("whitakerTrim").at("reasons");
        MorphologicalAssessmentIR assessment;
        for (const auto &value : reasons) {
            const auto name = value.template get<std::string_view>();
            for (std::size_t index{}; index < whitaker_trim_reason_count;
                 ++index) {
                const auto reason = static_cast<WhitakerTrimReason>(index);
                if (whitaker_trim_reason_name(reason) == name) {
                    assessment.whitaker_trim.add(reason);
                    break;
                }
            }
        }
        return excludes(assessment, filters);
    });
    return had_values && values.empty();
}

} // namespace

std::expected<ResultFilters, std::string>
parse_trim_filters(std::string_view value) {
    ResultFilters filters;
    if (value == "none") {
        return filters;
    }
    for (;;) {
        const auto separator = value.find(',');
        const auto name = value.substr(0, separator);
        std::optional<WhitakerTrimReason> selected;
        for (std::size_t index = 0; index < whitaker_trim_reason_count; ++index) {
            const auto reason = static_cast<WhitakerTrimReason>(index);
            if (whitaker_trim_reason_name(reason) == name) {
                selected = reason;
                break;
            }
        }
        if (!selected) {
            return std::unexpected("invalid trim filter: " + std::string{name});
        }
        if (!std::ranges::contains(filters.exclude_whitaker_trim_reasons,
                                   *selected)) {
            filters.exclude_whitaker_trim_reasons.push_back(*selected);
        }
        if (separator == std::string_view::npos) {
            return filters;
        }
        value.remove_prefix(separator + 1U);
    }
}

bool excludes(const MorphologicalAssessmentIR &assessment,
              const ResultFilters &filters) noexcept {
    return std::ranges::any_of(filters.exclude_whitaker_trim_reasons,
                               [&](const WhitakerTrimReason reason) {
                                   return std::ranges::contains(
                                       assessment.whitaker_trim.values(), reason);
                               });
}

void filter_result(JsonDocument &document, const ResultFilters &filters) {
    if (filters.exclude_whitaker_trim_reasons.empty()) {
        return;
    }
    const auto *key = document.contains("analyses") ? "analyses" : "hits";
    if (filter_list(document.at(key), filters)) {
        document.at("diagnostics").push_back(JsonDocument{
            {"code", "all-analyses-filtered"},
            {"severity", "info"},
            {"parameters", JsonDocument::object()},
        });
    }
    if (document.contains("tokens")) {
        for (auto &token : document.at("tokens")) {
            filter_result(token, filters);
        }
    }
    if (document.contains("suggestions")) {
        auto &suggestions = document.at("suggestions")
                                .get_ref<JsonDocument::array_t &>();
        std::erase_if(suggestions, [&](JsonDocument &suggestion) {
            bool empty_segment = false;
            for (auto &segment : suggestion.at("segments")) {
                auto &values = segment.at(key);
                static_cast<void>(filter_list(values, filters));
                empty_segment = empty_segment || values.empty();
            }
            return empty_segment;
        });
    }
}

} // namespace words::client
