#include "words/engine.hpp"
#include "words/json.hpp"

#include <emscripten/bind.h>
#include <emscripten/val.h>

#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>
#include <memory>
#include <new>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

struct LoadResult final {
    bool ok{};
    std::string code;
    std::string message;
    std::uint32_t database_bytes{};
    std::string database_kind;
};

constexpr std::string_view full_database_kind{"full"};
constexpr std::string_view search_database_kind{"search"};

class BrowserAnalysisEngine final {
  public:
    [[nodiscard]] LoadResult load_database(emscripten::val bytes,
                                           std::string dataset_id) {
        try {
            const auto uint8_array = emscripten::val::global("Uint8Array");
            if (!bytes.instanceof (uint8_array)) {
                return failure("invalid-database-buffer",
                               "database must be a Uint8Array", 0U);
            }

            const auto byte_length = bytes["byteLength"].as<double>();
            if (byte_length < 0.0 ||
                byte_length >
                    static_cast<double>(std::numeric_limits<std::uint32_t>::max())) {
                return failure("database-too-large",
                               "database exceeds the WebAssembly API limit", 0U);
            }
            const auto size = static_cast<std::uint32_t>(byte_length);
            std::vector<std::byte> database_image(size);
            if (!database_image.empty()) {
                auto destination = emscripten::val(emscripten::typed_memory_view(
                    database_image.size(),
                    reinterpret_cast<std::uint8_t *>(database_image.data())));
                destination.call<void>("set", bytes);
            }

            auto candidate = words::Engine::create(
                std::move(database_image),
                words::EngineConfig{std::move(dataset_id)});
            if (!candidate) {
                return failure(candidate.error().code,
                               candidate.error().message, size);
            }

            // WHY: replacement happens only after complete WWDB validation,
            // so a failed reload cannot destroy a working browser session.
            engine_ = std::move(*candidate);
            database_bytes_ = size;
            return LoadResult{true, {}, {}, size, database_kind()};
        } catch (const std::bad_alloc &) {
            return failure("out-of-memory",
                           "not enough WebAssembly memory for the database",
                           0U);
        } catch (const std::exception &error) {
            return failure("database-load-failure", error.what(), 0U);
        }
    }

    [[nodiscard]] bool ready() const noexcept { return engine_ != nullptr; }

    [[nodiscard]] std::string dataset_id() const {
        require_ready();
        return std::string{engine_->dataset_id()};
    }

    [[nodiscard]] std::uint32_t database_bytes() const noexcept {
        return database_bytes_;
    }

    [[nodiscard]] std::string database_kind() const {
        require_ready();
        return std::string{
            engine_->database().content() == words::DatabaseContent::full
                ? full_database_kind
                : search_database_kind};
    }

    [[nodiscard]] std::string analyze(const std::string &utf8,
                                      const bool two_words) const {
        require_ready();
        if (!engine_->supports_full_analysis()) {
            throw std::logic_error{
                "analysis JSON requires a full WWDB with meanings"};
        }
        const auto options = analysis_options(two_words);
        const auto result = engine_->analyze_text(utf8, options);
        return words::analysis_json(*engine_, result);
    }

    [[nodiscard]] std::string search(const std::string &utf8,
                                     const bool two_words) const {
        require_ready();
        const auto options = analysis_options(two_words);
        const auto result = engine_->analyze_text(utf8, options);
        return words::search_json(*engine_, result);
    }

    void reset() noexcept {
        engine_.reset();
        database_bytes_ = 0U;
    }

  private:
    [[nodiscard]] static LoadResult failure(std::string code,
                                            std::string message,
                                            const std::uint32_t size) {
        return LoadResult{false, std::move(code), std::move(message), size,
                          {}};
    }

    [[nodiscard]] static words::AnalysisOptions
    analysis_options(const bool two_words) noexcept {
        return words::AnalysisOptions{
            two_words ? words::TwoWordsMode::legacy_first_match
                      : words::TwoWordsMode::disabled};
    }

    void require_ready() const {
        if (!engine_) {
            // WHY: querying an unloaded wrapper is API misuse rather than a
            // Latin diagnostic and must not masquerade as analysis-v1 data.
            throw std::logic_error("analysis engine has no loaded database");
        }
    }

    std::unique_ptr<const words::Engine> engine_;
    std::uint32_t database_bytes_{};
};

} // namespace

EMSCRIPTEN_BINDINGS(words_analysis_engine) {
    emscripten::value_object<LoadResult>("LoadResult")
        .field("ok", &LoadResult::ok)
        .field("code", &LoadResult::code)
        .field("message", &LoadResult::message)
        .field("databaseBytes", &LoadResult::database_bytes)
        .field("databaseKind", &LoadResult::database_kind);

    emscripten::class_<BrowserAnalysisEngine>("AnalysisEngine")
        .constructor<>()
        .function("loadDatabase", &BrowserAnalysisEngine::load_database)
        .function("ready", &BrowserAnalysisEngine::ready)
        .function("datasetId", &BrowserAnalysisEngine::dataset_id)
        .function("databaseBytes", &BrowserAnalysisEngine::database_bytes)
        .function("databaseKind", &BrowserAnalysisEngine::database_kind)
        .function("analyze", &BrowserAnalysisEngine::analyze)
        .function("search", &BrowserAnalysisEngine::search)
        .function("reset", &BrowserAnalysisEngine::reset);
}
