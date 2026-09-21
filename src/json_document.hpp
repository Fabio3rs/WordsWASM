#pragma once

#include "words/engine.hpp"

#include <nlohmann/json.hpp>

namespace words {

// Native presentation documents. This private header deliberately stays out
// of include/: WebAssembly consumes the analysis engine, never nlohmann JSON.
using JsonDocument = nlohmann::ordered_json;

[[nodiscard]] JsonDocument analysis_json_document(const Engine &engine,
                                                   const QueryResult &result);
[[nodiscard]] JsonDocument search_json_document(const Engine &engine,
                                                 const QueryResult &result);
[[nodiscard]] JsonDocument analysis_json_v2_document(const Engine &engine,
                                                      const QueryResult &result);
[[nodiscard]] JsonDocument search_json_v2_document(const Engine &engine,
                                                    const QueryResult &result);
[[nodiscard]] JsonDocument analysis_json_v3_document(const Engine &engine,
                                                      const QueryResult &result);
[[nodiscard]] JsonDocument search_json_v3_document(const Engine &engine,
                                                    const QueryResult &result);

} // namespace words
