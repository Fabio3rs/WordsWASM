#pragma once

#include "words/engine.hpp"

#include <string>

namespace words {

// Analysis IDs are local to the loaded dataset. Projections reject results
// whose compact dataset tag does not match the supplied Engine. An empty
// EngineConfig::dataset_id selects tag zero and intentionally disables
// distinction between anonymous engines.
[[nodiscard]] std::string analysis_json(const Engine &engine,
                                        const QueryResult &result);
[[nodiscard]] std::string search_json(const Engine &engine,
                                      const QueryResult &result);
[[nodiscard]] std::string analysis_json_v2(const Engine &engine,
                                           const QueryResult &result);
[[nodiscard]] std::string search_json_v2(const Engine &engine,
                                         const QueryResult &result);

} // namespace words
