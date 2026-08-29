#pragma once

#include "words/model.hpp"

#include <expected>
#include <string>
#include <string_view>

namespace words {

struct LexError final {
    std::string code;
    std::string message;
};

class LatinLexer final {
  public:
    [[nodiscard]] std::expected<SurfaceForm, LexError>
    lex(std::string_view utf8) const;
};

} // namespace words
