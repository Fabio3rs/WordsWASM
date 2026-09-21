#include "json_document.hpp"
#include "words/engine.hpp"

#include <cstddef>
#include <exception>
#include <expected>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <new>
#include <print>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#ifndef WORDS_CLI_VERSION
#define WORDS_CLI_VERSION "development"
#endif

namespace {

struct Options final {
    std::filesystem::path database;
    std::string dataset_id;
    std::string format;
    std::string word;
    words::AnalysisOptions analysis;
    bool batch_json_lines{false};
    bool pretty{false};
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

        if (argument.starts_with("--database=")) {
            options.database =
                argument.substr(std::string_view{"--database="}.size());
        } else if (argument.starts_with("--db=")) {
            options.database =
                argument.substr(std::string_view{"--db="}.size());
        } else if (argument == "--database" || argument == "--db") {
            auto value = require_value(argument);
            if (!value) {
                return std::unexpected(std::move(value.error()));
            }
            options.database = *value;
        } else if (argument.starts_with("--dataset-id=")) {
            options.dataset_id =
                argument.substr(std::string_view{"--dataset-id="}.size());
        } else if (argument == "--dataset-id") {
            auto value = require_value(argument);
            if (!value) {
                return std::unexpected(std::move(value.error()));
            }
            options.dataset_id = *value;
        } else if (argument.starts_with("--format=")) {
            options.format =
                argument.substr(std::string_view{"--format="}.size());
        } else if (argument.starts_with("-f=")) {
            options.format = argument.substr(std::string_view{"-f="}.size());
        } else if (argument == "--format" || argument == "-f") {
            auto value = require_value(argument);
            if (!value) {
                return std::unexpected(std::move(value.error()));
            }
            options.format = *value;
        } else if (argument == "--two-words=legacy") {
            options.analysis.two_words =
                words::TwoWordsMode::legacy_first_match;
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
        } else if (argument == "--batch-json-lines" || argument == "--batch") {
            options.batch_json_lines = true;
        } else if (argument == "--pretty") {
            options.pretty = true;
        } else if (argument.starts_with("--two-words=")) {
            return std::unexpected("two-words mode must be legacy");
        } else if (argument.starts_with("--orthography=")) {
            return std::unexpected(
                "orthography mode must be disabled, classical, or medieval");
        } else if (argument.starts_with('-')) {
            return std::unexpected("unknown option: " + std::string{argument});
        } else {
            if (!options.word.empty()) {
                options.word.push_back(' ');
            }
            options.word.append(argument);
        }
    }

    if (options.database.empty() || options.format.empty() ||
        (!options.batch_json_lines && options.word.empty())) {
        return std::unexpected("database, format, and word are required");
    }
    if (options.batch_json_lines && !options.word.empty()) {
        return std::unexpected(
            "--batch-json-lines reads queries from stdin and accepts no word");
    }
    if (options.batch_json_lines && options.pretty) {
        return std::unexpected(
            "--pretty cannot be used with --batch-json-lines because JSONL "
            "requires one JSON value per line");
    }
    if (options.format != "analysis" && options.format != "search" &&
        options.format != "analysis-v2" && options.format != "search-v2" &&
        options.format != "analysis-v3" && options.format != "search-v3") {
        return std::unexpected(
            "format must be analysis, search, analysis-v2, search-v2, "
            "analysis-v3, or search-v3");
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
    std::print(stdout, R"(WordsWASM native CLI

Usage:
  words_cli --database FILE --format FORMAT [OPTIONS] LATIN_TEXT ...
  words_cli --database FILE --format FORMAT --batch-json-lines [OPTIONS] < queries.txt

Options:
  --database FILE, --db FILE  WWDB database to load.
  --dataset-id ID             Expected dataset identifier, when known.
  --format FORMAT, -f FORMAT  analysis-v3 (recommended) or search-v3.
  --pretty                    Indent JSON for terminal reading; emit an array for multiple results.
  --batch-json-lines, --batch Read one query per stdin line; emit one compact JSON value per line.
  --two-words=legacy          Use the legacy two-word choice.
  --orthography=MODE          disabled, classical, or medieval.
  --no-fixes, --no-prefixes, --no-suffixes, --no-tickons, --no-tackons,
  --no-packons, --no-syncope, --no-verbal-compounds
                              Disable individual analysis mechanisms.
  --help, -h                  Show this help.
  --version                   Show the CLI version.

Formats:
  analysis-v3  Full morphological analysis; requires a full WWDB.
  search-v3    Search-oriented result; works with full and search WWDBs.

Exit status: 0 success; 2 invalid command; 3 database or engine failure;
4 unexpected failure. JSON is written to stdout and diagnostics to stderr.
)");
}

[[nodiscard]] words::JsonDocument
result_document(const words::Engine &engine, const words::QueryResult &result,
                const std::string_view format) {
    words::JsonDocument document;
    if (format == "analysis") {
        document = words::analysis_json_document(engine, result);
    } else if (format == "analysis-v2") {
        document = words::analysis_json_v2_document(engine, result);
    } else if (format == "analysis-v3") {
        document = words::analysis_json_v3_document(engine, result);
    } else if (format == "search-v2") {
        document = words::search_json_v2_document(engine, result);
    } else if (format == "search-v3") {
        document = words::search_json_v3_document(engine, result);
    } else {
        document = words::search_json_document(engine, result);
    }
    return document;
}

void write_result(const words::Engine &engine, const words::QueryResult &result,
                  const std::string_view format, const bool pretty) {
    const auto document = result_document(engine, result, format);
    std::print(stdout, "{}\n", document.dump(pretty ? 2 : -1));
}

void write_text_result(const words::Engine &engine,
                       const std::string_view query,
                       const std::string_view format,
                       const words::AnalysisOptions options,
                       const bool pretty) {
    write_result(engine, engine.analyze_text(query, options), format, pretty);
}

void write_line_results(const words::Engine &engine,
                        const std::string_view query,
                        const std::string_view format,
                        const words::AnalysisOptions options,
                        const bool pretty) {
    const auto results = engine.analyze_line(query, options);
    if (pretty && results.size() > 1U) {
        auto document = words::JsonDocument::array();
        for (const auto &result : results) {
            document.push_back(result_document(engine, result, format));
        }
        std::print(stdout, "{}\n", document.dump(2));
        return;
    }
    for (const auto &result : results) {
        write_result(engine, result, format, pretty);
    }
}

} // namespace

int main(const int argc, char *argv[]) try {
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument = argv[index];
        if (argument == "--help" || argument == "-h") {
            usage();
            return 0;
        }
        if (argument == "--version") {
            std::print(stdout, "words_cli {}\n", WORDS_CLI_VERSION);
            return 0;
        }
    }
    auto options = parse_options(argc, argv);
    if (!options) {
        std::print(stderr, "words_cli: {}. Run with --help for usage.\n",
                   options.error());
        return 2;
    }
    auto bytes = read_file(options->database);
    if (!bytes) {
        std::print(stderr, "words_cli: {}\n", bytes.error());
        return 3;
    }
    auto engine = words::Engine::create(
        std::move(*bytes), words::EngineConfig{options->dataset_id});
    if (!engine) {
        std::print(stderr, "words_cli: {}: {}\n", engine.error().code,
                   engine.error().message);
        return 3;
    }
    if ((options->format == "analysis" || options->format == "analysis-v2" ||
         options->format == "analysis-v3") &&
        !(*engine)->supports_full_analysis()) {
        std::print(stderr, "words_cli: unsupported-output: analysis format "
                           "requires a full WWDB with meanings\n");
        return 3;
    }

    const auto analysis_options = options->analysis;
    if (options->batch_json_lines) {
        // WHY: corpus acceptance should exercise one long-lived immutable
        // snapshot instead of measuring thousands of process startups.
        std::string query;
        while (std::getline(std::cin, query)) {
            if (!query.empty() && query.back() == '\r') {
                query.pop_back();
            }
            if (!query.empty()) {
                write_text_result(**engine, query, options->format,
                                  analysis_options, false);
            }
        }
    } else {
        write_line_results(**engine, options->word, options->format,
                           analysis_options, options->pretty);
    }
    return 0;
} catch (const std::bad_alloc &) {
    std::print(stderr, "words_cli: out of memory\n");
    return 4;
} catch (const std::exception &error) {
    std::print(stderr, "words_cli: unexpected failure: {}\n", error.what());
    return 4;
}
