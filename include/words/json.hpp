#pragma once

#include "words/engine.hpp"

#include <string>

namespace words {

[[nodiscard]] std::string analysis_json(const Engine &engine,
                                        const QueryResult &result);
[[nodiscard]] std::string search_json(const Engine &engine,
                                      const QueryResult &result);

} // namespace words
