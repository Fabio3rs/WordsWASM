#pragma once

#include "json_document.hpp"

#include <expected>
#include <string>
#include <string_view>
#include <vector>

namespace words::client {

struct ResultFilters final {
    std::vector<WhitakerTrimReason> exclude_whitaker_trim_reasons;
};

[[nodiscard]] std::expected<ResultFilters, std::string>
parse_trim_filters(std::string_view value);

// Presentation only: call after projection, before JSON or human rendering.
void filter_result(JsonDocument &document, const ResultFilters &filters);

} // namespace words::client
