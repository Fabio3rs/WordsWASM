#include "words/engine.hpp"
#include "words/json.hpp"

#include <cstddef>
#include <exception>
#include <expected>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <new>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

struct Options final {
    std::filesystem::path database;
    std::string dataset_id;
    std::string format;
    std::string word;
    words::TwoWordsMode two_words{words::TwoWordsMode::disabled};
    bool batch_json_lines{false};
};

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
            if (!value)
                return std::unexpected(std::move(value.error()));
            options.database = *value;
        } else if (argument == "--dataset-id") {
            auto value = require_value(argument);
            if (!value)
                return std::unexpected(std::move(value.error()));
            options.dataset_id = *value;
        } else if (argument == "--format") {
            auto value = require_value(argument);
            if (!value)
                return std::unexpected(std::move(value.error()));
            options.format = *value;
        } else if (argument == "--two-words=legacy") {
            options.two_words = words::TwoWordsMode::legacy_first_match;
        } else if (argument == "--batch-json-lines") {
            options.batch_json_lines = true;
        } else if (argument.starts_with("--two-words=")) {
            return std::unexpected("two-words mode must be legacy");
        } else if (argument.starts_with('-')) {
            return std::unexpected("unknown option: " + std::string{argument});
        } else if (options.word.empty()) {
            options.word = argument;
        } else {
            return std::unexpected(
                "pass a multi-token Latin query as one quoted argument");
        }
    }

    if (options.database.empty() || options.dataset_id.empty() ||
        options.format.empty() ||
        (!options.batch_json_lines && options.word.empty())) {
        return std::unexpected(
            "database, dataset-id, format, and word are required");
    }
    if (options.batch_json_lines && !options.word.empty()) {
        return std::unexpected(
            "batch-json-lines reads queries from stdin and accepts no word");
    }
    if (options.format != "analysis" && options.format != "search") {
        return std::unexpected("format must be analysis or search");
    }
    return options;
}

[[nodiscard]] std::expected<std::vector<std::byte>, std::string>
read_file(const std::filesystem::path &path) {
    std::ifstream input{path, std::ios::binary | std::ios::ate};
    if (!input) {
        return std::unexpected("cannot open database: " + path.string());
    }
    const auto end = input.tellg();
    if (end < 0 ||
        !std::in_range<std::size_t>(static_cast<std::streamoff>(end))) {
        return std::unexpected("database size is invalid");
    }
    std::vector<std::byte> bytes(static_cast<std::size_t>(end));
    input.seekg(0);
    if (!bytes.empty()) {
        if (bytes.size() > static_cast<std::size_t>(
                               std::numeric_limits<std::streamsize>::max())) {
            return std::unexpected("database exceeds stream limits");
        }
        input.read(reinterpret_cast<char *>(bytes.data()),
                   static_cast<std::streamsize>(bytes.size()));
    }
    if (!input) {
        return std::unexpected("cannot read complete database: " +
                               path.string());
    }
    return bytes;
}

void usage() {
    std::cerr << "usage: words_cli --database FILE --dataset-id sha256:... "
                 "--format analysis|search [--two-words=legacy] "
                 "[--batch-json-lines | LATIN_TEXT]\n";
}

void write_result(const words::Engine &engine, const std::string_view query,
                  const std::string_view format,
                  const words::AnalysisOptions options) {
    const auto result = engine.analyze_text(query, options);
    if (format == "analysis") {
        std::cout << words::analysis_json(engine, result) << '\n';
    } else {
        std::cout << words::search_json(engine, result) << '\n';
    }
}

} // namespace

int main(const int argc, char *argv[]) try {
    auto options = parse_options(argc, argv);
    if (!options) {
        usage();
        std::cerr << "words_cli: " << options.error() << '\n';
        return 2;
    }
    auto bytes = read_file(options->database);
    if (!bytes) {
        std::cerr << "words_cli: " << bytes.error() << '\n';
        return 3;
    }
    auto engine = words::Engine::create(
        std::move(*bytes), words::EngineConfig{options->dataset_id});
    if (!engine) {
        std::cerr << "words_cli: " << engine.error().code << ": "
                  << engine.error().message << '\n';
        return 3;
    }
    if (options->format == "analysis" &&
        !(*engine)->supports_full_analysis()) {
        std::cerr << "words_cli: unsupported-output: analysis format requires "
                     "a full WWDB with meanings\n";
        return 3;
    }

    const words::AnalysisOptions analysis_options{options->two_words};
    if (options->batch_json_lines) {
        // WHY: corpus acceptance should exercise one long-lived immutable
        // snapshot instead of measuring thousands of process startups.
        std::string query;
        while (std::getline(std::cin, query)) {
            if (!query.empty() && query.back() == '\r') {
                query.pop_back();
            }
            if (!query.empty()) {
                write_result(**engine, query, options->format,
                             analysis_options);
            }
        }
    } else {
        write_result(**engine, options->word, options->format,
                     analysis_options);
    }
    return 0;
} catch (const std::bad_alloc &) {
    std::cerr << "words_cli: out of memory\n";
    return 4;
} catch (const std::exception &error) {
    std::cerr << "words_cli: unexpected failure: " << error.what() << '\n';
    return 4;
}
