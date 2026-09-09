#pragma once

#include "words/database.hpp"
#include "words/lexer.hpp"
#include "words/lifetime.hpp"
#include "words/model.hpp"

#include <cstddef>
#include <expected>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace words {

struct EngineConfig final {
    std::string dataset_id;
};

class Engine final {
  public:
    [[nodiscard]] static std::expected<std::unique_ptr<const Engine>, LoadError>
    create(std::vector<std::byte> database_image, EngineConfig config);

    Engine(const Engine &) = delete;
    Engine &operator=(const Engine &) = delete;
    Engine(Engine &&) = delete;
    Engine &operator=(Engine &&) = delete;
    ~Engine() = default;

    [[nodiscard]] QueryResult analyze(std::string_view utf8,
                                      AnalysisOptions options = {}) const;
    // Preserve lexical context collected by TextTokenCursor for analysis
    // policies that depend on the following boundary, such as abbreviations.
    [[nodiscard]] QueryResult analyze(const TextToken &token,
                                      AnalysisOptions options = {}) const;
    [[nodiscard]] QueryResult analyze_text(std::string_view utf8,
                                           AnalysisOptions options = {}) const;
    // Analyze a complete input line using the historical one-token lookahead:
    // a recognized verbal compound consumes two tokens, while a failed peek
    // leaves both word results intact.
    [[nodiscard]] std::vector<QueryResult>
    analyze_line(std::string_view utf8, AnalysisOptions options = {}) const;
    [[nodiscard]] const Database &
    database() const noexcept WORDS_LIFETIMEBOUND {
        return *database_;
    }
    [[nodiscard]] std::string_view
    dataset_id() const noexcept WORDS_LIFETIMEBOUND {
        return dataset_identity_.value_;
    }
    [[nodiscard]] bool owns(const QueryResult &result) const noexcept {
        return result.origin == dataset_identity_;
    }
    [[nodiscard]] bool supports_full_analysis() const noexcept {
        return database_->has_meanings();
    }

  private:
    Engine(std::unique_ptr<const Database> database, EngineConfig config)
        : database_{std::move(database)},
          dataset_identity_{std::move(config.dataset_id)} {}

    std::unique_ptr<const Database> database_;
    DatasetIdentity dataset_identity_;
    LatinLexer lexer_;
};

[[nodiscard]] bool valid_dataset_id(std::string_view value) noexcept;

} // namespace words
