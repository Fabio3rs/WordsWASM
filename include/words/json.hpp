#pragma once

#include "words/engine.hpp"

#include <string>

namespace words {

// Analysis IDs are local to the loaded dataset. Callers must project a result
// with the same Engine that produced it; the built-in CLI and Wasm adapters do
// so synchronously.
[[nodiscard]] std::string analysis_json(const Engine &engine,
                                        const QueryResult &result);
[[nodiscard]] std::string search_json(const Engine &engine,
                                      const QueryResult &result);
[[nodiscard]] std::string analysis_json_v2(const Engine &engine,
                                           const QueryResult &result);
[[nodiscard]] std::string search_json_v2(const Engine &engine,
                                         const QueryResult &result);

} // namespace words
