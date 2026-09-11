#include "words/database.hpp"

#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
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

struct Options final {
    std::filesystem::path database;
    std::size_t iterations{1U};
    std::size_t warmup{0U};
};

[[nodiscard]] std::expected<std::size_t, std::string>
parse_size(const std::string_view text, const std::string_view option) {
    std::uint64_t value{};
    const auto [end, error] =
        std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size() ||
        !std::in_range<std::size_t>(value)) {
        return std::unexpected("invalid value for " + std::string{option});
    }
    return static_cast<std::size_t>(value);
}

[[nodiscard]] std::expected<Options, std::string>
parse_options(const int argc, char *const argv[]) {
    Options options;
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument = argv[index];
        const auto require_value =
            [&]() -> std::expected<std::string_view, std::string> {
            if (++index >= argc) {
                return std::unexpected("missing value for " +
                                       std::string{argument});
            }
            return std::string_view{argv[index]};
        };
        if (argument == "--database") {
            auto value = require_value();
            if (!value) {
                return std::unexpected(std::move(value.error()));
            }
            options.database = *value;
        } else if (argument == "--iterations" || argument == "--warmup") {
            auto value = require_value();
            if (!value) {
                return std::unexpected(std::move(value.error()));
            }
            auto parsed = parse_size(*value, argument);
            if (!parsed) {
                return std::unexpected(std::move(parsed.error()));
            }
            if (argument == "--iterations") {
                options.iterations = *parsed;
            } else {
                options.warmup = *parsed;
            }
        } else {
            return std::unexpected("unknown option: " + std::string{argument});
        }
    }
    if (options.database.empty() || options.iterations == 0U) {
        return std::unexpected("database and positive iterations are required");
    }
    return options;
}

[[nodiscard]] std::expected<std::vector<std::byte>, std::string>
read_file(const std::filesystem::path &path) {
    std::ifstream input{path, std::ios::binary | std::ios::ate};
    if (!input) {
        return std::unexpected("cannot open database");
    }
    const auto end = input.tellg();
    if (end < 0 ||
        !std::in_range<std::size_t>(static_cast<std::streamoff>(end))) {
        return std::unexpected("invalid database size");
    }
    std::vector<std::byte> bytes(static_cast<std::size_t>(end));
    input.seekg(0);
    input.read(reinterpret_cast<char *>(bytes.data()),
               static_cast<std::streamsize>(end));
    if (!input) {
        return std::unexpected("cannot read complete database");
    }
    return bytes;
}

[[nodiscard]] std::expected<std::uint64_t, std::string>
load_once(const std::vector<std::byte> &source,
          std::chrono::nanoseconds *elapsed) {
    auto image = source;
#if WORDS_HAS_CALLGRIND_CLIENT_REQUESTS
    CALLGRIND_START_INSTRUMENTATION;
#endif
    const auto started = std::chrono::steady_clock::now();
    auto database = words::Database::load_poc(std::move(image));
    const auto stopped = std::chrono::steady_clock::now();
#if WORDS_HAS_CALLGRIND_CLIENT_REQUESTS
    CALLGRIND_STOP_INSTRUMENTATION;
#endif
    if (!database) {
        return std::unexpected(database.error().code + ": " +
                               database.error().message);
    }
    if (elapsed != nullptr) {
        *elapsed += stopped - started;
    }
    const auto stems = (*database)->lookup_stem("puell").size();
    const auto endings = (*database)->lookup_ending("ae").size();
    return static_cast<std::uint64_t>((*database)->lexemes().size()) +
           static_cast<std::uint64_t>((*database)->rules().size()) + stems +
           endings;
}

} // namespace

int main(const int argc, char *argv[]) try {
    auto options = parse_options(argc, argv);
    if (!options) {
        std::println(stderr,
                     "usage: words_database_load_benchmark --database FILE "
                     "[--warmup N] [--iterations N]");
        std::println(stderr, "words_database_load_benchmark: {}",
                     options.error());
        return 2;
    }
    auto source = read_file(options->database);
    if (!source) {
        std::println(stderr, "words_database_load_benchmark: {}",
                     source.error());
        return 3;
    }
    for (std::size_t iteration{}; iteration < options->warmup; ++iteration) {
        auto loaded = load_once(*source, nullptr);
        if (!loaded) {
            std::println(stderr, "words_database_load_benchmark: {}",
                         loaded.error());
            return 3;
        }
    }
#if WORDS_HAS_CALLGRIND_CLIENT_REQUESTS
    CALLGRIND_ZERO_STATS;
#endif
    std::chrono::nanoseconds elapsed{};
    std::uint64_t checksum{};
    for (std::size_t iteration{}; iteration < options->iterations;
         ++iteration) {
        auto loaded = load_once(*source, &elapsed);
        if (!loaded) {
            std::println(stderr, "words_database_load_benchmark: {}",
                         loaded.error());
            return 3;
        }
        checksum += *loaded;
    }
    const auto elapsed_ns = static_cast<std::uint64_t>(elapsed.count());
    std::println("iterations={} warmup={} database_bytes={} checksum={}",
                 options->iterations, options->warmup, source->size(),
                 checksum);
    std::println("elapsed_ns={} ns_per_load={}", elapsed_ns,
                 elapsed_ns / options->iterations);
    return 0;
} catch (const std::bad_alloc &) {
    std::println(stderr, "words_database_load_benchmark: out of memory");
    return 4;
} catch (const std::exception &error) {
    std::println(stderr, "words_database_load_benchmark: {}", error.what());
    return 4;
}
