#pragma once

#include "words/database.hpp"

#include <string>
#include <string_view>

namespace words {

// WHY: the canonical headword depends only on compact stems and lexical flags.
// Keeping this formatter independent of meanings lets search-only clients
// resolve a persistent LexemeId without loading the editorial definition pool.
[[nodiscard]] std::string
citation_lemma(const Database &database, const LexemeRecord &lexeme,
               std::string_view fallback = {});

} // namespace words
