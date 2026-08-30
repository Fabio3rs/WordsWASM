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

struct AnalysisOptions final {
    TwoWordsMode two_words{TwoWordsMode::disabled};
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
    [[nodiscard]] QueryResult analyze_text(std::string_view utf8,
                                           AnalysisOptions options = {}) const;
    [[nodiscard]] const Database &
    database() const noexcept WORDS_LIFETIMEBOUND {
        return *database_;
    }
    [[nodiscard]] std::string_view
    dataset_id() const noexcept WORDS_LIFETIMEBOUND {
        return config_.dataset_id;
    }
    [[nodiscard]] bool supports_full_analysis() const noexcept {
        return database_->has_meanings();
    }

  private:
    Engine(std::unique_ptr<const Database> database, EngineConfig config)
        : database_{std::move(database)}, config_{std::move(config)} {}

    std::unique_ptr<const Database> database_;
    EngineConfig config_;
    LatinLexer lexer_;
};

[[nodiscard]] bool valid_dataset_id(std::string_view value) noexcept;

} // namespace words
