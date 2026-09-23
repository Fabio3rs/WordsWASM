#pragma once

#include "result_filters.hpp"
#include "words/engine.hpp"

#include <cstddef>
#include <string>

namespace words::client {

struct HumanOptions final {
    bool compact{};
    bool detailed{};
    bool color{};
};

[[nodiscard]] std::string human_header();
[[nodiscard]] std::string render_human(const Engine &engine,
                                       const QueryResult &result,
                                       const ResultFilters &filters,
                                       HumanOptions options,
                                       std::size_t result_number);

} // namespace words::client
