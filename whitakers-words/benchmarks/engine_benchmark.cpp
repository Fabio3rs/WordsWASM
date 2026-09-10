#include "words/engine.hpp"

#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <expected>
#include <filesystem>
#include <fstream>
#include <limits>
#include <new>
#include <print>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#if __has_include(<valgrind/callgrind.h>)
#include <valgrind/callgrind.h>
#define WORDS_HAS_CALLGRIND_CLIENT_REQUESTS 1
#else
#define WORDS_HAS_CALLGRIND_CLIENT_REQUESTS 0
#endif

namespace {

constexpr std::string_view default_dataset_id =
    "sha256:0000000000000000000000000000000000000000000000000000000000000000";

enum class Mode : std::uint8_t {
    queries,
    lines,
    corpus,
};

struct Options final {
    std::filesystem::path database;
    std::filesystem::path corpus;
    std::string dataset_id{default_dataset_id};
    words::AnalysisOptions analysis;
    Mode mode{Mode::lines};
    std::size_t iterations{1U};
    std::size_t warmup{0U};
};

struct Counts final {
    std::uint64_t units{};
    std::uint64_t tokens{};
    std::uint64_t analyses{};

    Counts &operator+=(const Counts &other) noexcept {
        units += other.units;
        tokens += other.tokens;
        analyses += other.analyses;
        return *this;
    }

    [[nodiscard]] std::uint64_t checksum() const noexcept {
        return units + tokens + analyses;
    }
};

[[nodiscard]] std::expected<std::size_t, std::string>
parse_size(const std::string_view text, const std::string_view option) {
    std::uint64_t value{};
    const auto [end, error] =
        std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size() ||
        !std::in_range<std::size_t>(value)) {
        return std::unexpected("invalid value for " + std::string{option} +
                               ": " + std::string{text});
    }
    return static_cast<std::size_t>(value);
}

[[nodiscard]] std::expected<Mode, std::string>
parse_mode(const std::string_view text) {
    if (text == "queries") {
        return Mode::queries;
    }
    if (text == "lines") {
        return Mode::lines;
    }
    if (text == "corpus") {
        return Mode::corpus;
    }
    return std::unexpected("mode must be queries, lines, or corpus");
}

[[nodiscard]] std::expected<Options, std::string>
parse_options(const int argc, char *const argv[]) {
    Options options;
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument = argv[index];
        const auto require_value = [&](const std::string_view name)
            -> std::expected<std::string_view, std::string> {
            if (index + 1 >= argc) {
                return std::unexpected("missing value for " +
                                       std::string{name});
            }
            ++index;
            return std::string_view{argv[index]};
        };

        if (argument == "--database") {
            auto value = require_value(argument);
            if (!value) {
                return std::unexpected(std::move(value.error()));
            }
            options.database = *value;
        } else if (argument == "--corpus") {
            auto value = require_value(argument);
            if (!value) {
                return std::unexpected(std::move(value.error()));
            }
            options.corpus = *value;
        } else if (argument == "--dataset-id") {
            auto value = require_value(argument);
            if (!value) {
                return std::unexpected(std::move(value.error()));
            }
            options.dataset_id = *value;
        } else if (argument == "--mode") {
            auto value = require_value(argument);
            if (!value) {
                return std::unexpected(std::move(value.error()));
            }
            auto mode = parse_mode(*value);
            if (!mode) {
                return std::unexpected(std::move(mode.error()));
            }
            options.mode = *mode;
        } else if (argument == "--iterations") {
            auto value = require_value(argument);
            if (!value) {
                return std::unexpected(std::move(value.error()));
            }
            auto count = parse_size(*value, argument);
            if (!count || *count == 0U) {
                return std::unexpected(count ? "iterations must be positive"
                                             : std::move(count.error()));
            }
            options.iterations = *count;
        } else if (argument == "--warmup") {
            auto value = require_value(argument);
            if (!value) {
                return std::unexpected(std::move(value.error()));
            }
            auto count = parse_size(*value, argument);
            if (!count) {
                return std::unexpected(std::move(count.error()));
            }
            options.warmup = *count;
        } else if (argument == "--orthography=disabled") {
            options.analysis.orthography = words::OrthographyMode::disabled;
        } else if (argument == "--orthography=classical") {
            options.analysis.orthography =
                words::OrthographyMode::classical_only;
        } else if (argument == "--orthography=medieval") {
            options.analysis.orthography =
                words::OrthographyMode::classical_and_medieval;
        } else if (argument == "--no-prefixes") {
            options.analysis.mechanisms.prefixes = false;
        } else if (argument == "--no-fixes") {
            options.analysis.mechanisms.productive_derivations = false;
        } else if (argument == "--no-suffixes") {
            options.analysis.mechanisms.suffixes = false;
        } else if (argument == "--no-tickons") {
            options.analysis.mechanisms.tickons = false;
        } else if (argument == "--no-tackons") {
            options.analysis.mechanisms.tackons = false;
        } else if (argument == "--no-packons") {
            options.analysis.mechanisms.packons = false;
        } else if (argument == "--no-syncope") {
            options.analysis.mechanisms.syncope = false;
        } else if (argument == "--no-verbal-compounds") {
            options.analysis.mechanisms.verbal_compounds = false;
        } else if (argument.starts_with("--orthography=")) {
            return std::unexpected(
                "orthography mode must be disabled, classical, or medieval");
        } else {
            return std::unexpected("unknown option: " + std::string{argument});
        }
    }

    if (options.database.empty() || options.corpus.empty()) {
        return std::unexpected("database and corpus are required");
    }
    if (!words::valid_dataset_id(options.dataset_id)) {
        return std::unexpected("dataset-id must be sha256 followed by 64 hex "
                               "digits");
    }
    return options;
}

void usage() {
    std::println(
        stderr,
        "usage: words_engine_benchmark --database FILE --corpus FILE "
        "[--dataset-id sha256:...] [--mode queries|lines|corpus] "
        "[--iterations N] [--warmup N] "
        "[--orthography=disabled|classical|medieval] "
        "[--no-fixes] [--no-prefixes] [--no-suffixes] [--no-tickons] "
        "[--no-tackons] [--no-packons] [--no-syncope] "
        "[--no-verbal-compounds]");
}

[[nodiscard]] std::expected<std::vector<std::byte>, std::string>
read_binary_file(const std::filesystem::path &path) {
    std::ifstream input{path, std::ios::binary | std::ios::ate};
    if (!input) {
        return std::unexpected("cannot open file: " + path.string());
    }
    const auto end = input.tellg();
    if (end < 0 ||
        !std::in_range<std::size_t>(static_cast<std::streamoff>(end))) {
        return std::unexpected("file size is invalid: " + path.string());
    }
    std::vector<std::byte> bytes(static_cast<std::size_t>(end));
    input.seekg(0);
    if (!bytes.empty()) {
        if (bytes.size() > static_cast<std::size_t>(
                               std::numeric_limits<std::streamsize>::max())) {
            return std::unexpected("file exceeds stream limits: " +
                                   path.string());
        }
        input.read(reinterpret_cast<char *>(bytes.data()),
                   static_cast<std::streamsize>(bytes.size()));
    }
    if (!input) {
        return std::unexpected("cannot read complete file: " + path.string());
    }
    return bytes;
}

[[nodiscard]] std::expected<std::string, std::string>
read_text_file(const std::filesystem::path &path) {
    auto bytes = read_binary_file(path);
    if (!bytes) {
        return std::unexpected(std::move(bytes.error()));
    }
    if (bytes->empty()) {
        return std::string{};
    }
    return std::string{reinterpret_cast<const char *>(bytes->data()),
                       bytes->size()};
}

[[nodiscard]] std::vector<std::string_view>
nonempty_lines(const std::string_view corpus) {
    std::vector<std::string_view> lines;
    std::size_t begin{};
    while (begin < corpus.size()) {
        auto end = corpus.find('\n', begin);
        if (end == std::string_view::npos) {
            end = corpus.size();
        }
        auto line = corpus.substr(begin, end - begin);
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1U);
        }
        if (!line.empty()) {
            lines.push_back(line);
        }
        if (end == corpus.size()) {
            break;
        }
        begin = end + 1U;
    }
    return lines;
}

void consume(const words::QueryResult &result, Counts &counts) noexcept {
    ++counts.units;
    counts.analyses += static_cast<std::uint64_t>(result.total_analyses());
    counts.tokens +=
        static_cast<std::uint64_t>(result.independent_tokens.size());
}

void consume(const std::vector<words::QueryResult> &results,
             Counts &counts) noexcept {
    for (const auto &result : results) {
        consume(result, counts);
    }
}

[[nodiscard]] Counts
run_iteration(const words::Engine &engine, const std::string_view corpus,
              const std::vector<std::string_view> &lines, const Mode mode,
              const words::AnalysisOptions options) {
    Counts counts;
    if (mode == Mode::queries) {
        for (const auto line : lines) {
            consume(engine.analyze_text(line, options), counts);
        }
    } else if (mode == Mode::lines) {
        for (const auto line : lines) {
            consume(engine.analyze_line(line, options), counts);
        }
    } else {
        consume(engine.analyze_line(corpus, options), counts);
    }
    return counts;
}

[[nodiscard]] Counts
run_iterations(const words::Engine &engine, const std::string_view corpus,
               const std::vector<std::string_view> &lines, const Mode mode,
               const words::AnalysisOptions options,
               const std::size_t iterations) {
    Counts total;
    for (std::size_t iteration{}; iteration < iterations; ++iteration) {
        total += run_iteration(engine, corpus, lines, mode, options);
    }
    return total;
}

[[nodiscard]] constexpr std::string_view mode_name(const Mode mode) noexcept {
    if (mode == Mode::queries) {
        return "queries";
    }
    if (mode == Mode::lines) {
        return "lines";
    }
    if (mode == Mode::corpus) {
        return "corpus";
    }
    std::unreachable();
}

void callgrind_start() noexcept {
#if WORDS_HAS_CALLGRIND_CLIENT_REQUESTS
    CALLGRIND_ZERO_STATS;
    CALLGRIND_START_INSTRUMENTATION;
#endif
}

void callgrind_stop() noexcept {
#if WORDS_HAS_CALLGRIND_CLIENT_REQUESTS
    CALLGRIND_STOP_INSTRUMENTATION;
#endif
}

} // namespace

int main(const int argc, char *argv[]) try {
    auto options = parse_options(argc, argv);
    if (!options) {
        usage();
        std::println(stderr, "words_engine_benchmark: {}", options.error());
        return 2;
    }

    auto database = read_binary_file(options->database);
    if (!database) {
        std::println(stderr, "words_engine_benchmark: {}", database.error());
        return 3;
    }
    auto corpus = read_text_file(options->corpus);
    if (!corpus) {
        std::println(stderr, "words_engine_benchmark: {}", corpus.error());
        return 3;
    }
    auto engine = words::Engine::create(
        std::move(*database), words::EngineConfig{options->dataset_id});
    if (!engine) {
        std::println(stderr, "words_engine_benchmark: {}: {}",
                     engine.error().code, engine.error().message);
        return 3;
    }

    const auto lines = nonempty_lines(*corpus);
    const auto warmup = run_iterations(**engine, *corpus, lines, options->mode,
                                       options->analysis, options->warmup);

    callgrind_start();
    const auto started = std::chrono::steady_clock::now();
    const auto measured =
        run_iterations(**engine, *corpus, lines, options->mode,
                       options->analysis, options->iterations);
    const auto stopped = std::chrono::steady_clock::now();
    callgrind_stop();

    const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
        stopped - started);
    const auto elapsed_ns = static_cast<std::uint64_t>(elapsed.count());
    const auto nanoseconds_per_unit =
        measured.units == 0U ? 0U : elapsed_ns / measured.units;

    std::println("mode={} iterations={} warmup={} corpus_bytes={} "
                 "source_lines={}",
                 mode_name(options->mode), options->iterations,
                 options->warmup, corpus->size(), lines.size());
    std::println(
        "checksum={} units={} tokens={} analyses={} warmup_checksum={}",
        measured.checksum(), measured.units, measured.tokens, measured.analyses,
        warmup.checksum());
    std::println("elapsed_ns={} ns_per_unit={}", elapsed_ns,
                 nanoseconds_per_unit);
    return 0;
} catch (const std::bad_alloc &) {
    std::println(stderr, "words_engine_benchmark: out of memory");
    return 4;
} catch (const std::exception &error) {
    std::println(stderr, "words_engine_benchmark: unexpected failure: {}",
                 error.what());
    return 4;
}
