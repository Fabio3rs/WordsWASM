#include "json_document.hpp"
#include "human_result.hpp"
#include "result_filters.hpp"
#include "words/engine.hpp"

#include <cstddef>
#include <exception>
#include <expected>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <new>
#include <optional>
#include <print>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <io.h>
#include <windows.h>
#include <shellapi.h>
#else
#include <unistd.h>
#endif

#ifndef WORDS_CLI_VERSION
#define WORDS_CLI_VERSION "development"
#endif

namespace {

#ifdef _WIN32
[[nodiscard]] std::expected<std::vector<std::string>, std::string>
utf8_command_line() {
    int count{};
    const auto release = [](LPWSTR *value) { static_cast<void>(LocalFree(value)); };
    std::unique_ptr<LPWSTR, decltype(release)> wide{
        CommandLineToArgvW(GetCommandLineW(), &count), release};
    if (!wide)
        return std::unexpected("cannot read the Windows command line");

    std::vector<std::string> arguments;
    arguments.reserve(static_cast<std::size_t>(count));
    for (int index{}; index < count; ++index) {
        const int length = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
                                               wide.get()[index], -1, nullptr,
                                               0, nullptr, nullptr);
        if (length <= 0)
            return std::unexpected("cannot encode a command-line argument as UTF-8");
        std::string argument(static_cast<std::size_t>(length), '\0');
        if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
                                wide.get()[index], -1, argument.data(), length,
                                nullptr, nullptr) != length)
            return std::unexpected("cannot encode a command-line argument as UTF-8");
        argument.pop_back();
        arguments.push_back(std::move(argument));
    }
    return arguments;
}
#endif

struct Options final {
    std::filesystem::path database;
    std::string dataset_id;
    std::string format;
    std::string word;
    words::AnalysisOptions analysis;
    words::client::ResultFilters filters;
    bool filters_specified{false};
    bool stream_input{false};
    std::optional<std::filesystem::path> input;
    bool pretty{false};
    bool detailed{false};
    std::string human_style{"normal"};
    bool human_style_specified{false};
    std::string color{"auto"};
    bool color_specified{false};
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
        } else if (argument == "--filter-trim" ||
                   argument.starts_with("--filter-trim=")) {
            if (options.filters_specified) {
                return std::unexpected("--filter-trim may only be specified once");
            }
            options.filters_specified = true;
            auto value = argument == "--filter-trim"
                ? require_value(argument)
                : std::expected<std::string_view, std::string>{
                      argument.substr(std::string_view{"--filter-trim="}.size())};
            if (!value) {
                return std::unexpected(std::move(value.error()));
            }
            auto filters = words::client::parse_trim_filters(*value);
            if (!filters) {
                return std::unexpected(std::move(filters.error()));
            }
            options.filters = std::move(*filters);
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
        } else if (argument.starts_with("--input=")) {
            if (options.stream_input) {
                return std::unexpected(
                    "input selector may only be specified once");
            }
            if (argument.size() == std::string_view{"--input="}.size()) {
                return std::unexpected("missing value for --input");
            }
            options.stream_input = true;
            options.input = std::filesystem::path{
                argument.substr(std::string_view{"--input="}.size())};
        } else if (argument == "--input" || argument == "-i") {
            if (options.stream_input) {
                return std::unexpected(
                    "input selector may only be specified once");
            }
            auto value = require_value(argument);
            if (!value) {
                return std::unexpected(std::move(value.error()));
            }
            options.stream_input = true;
            options.input = std::filesystem::path{*value};
        } else if (argument == "--batch-json-lines" || argument == "--batch") {
            if (options.stream_input) {
                return std::unexpected(
                    "input selector may only be specified once");
            }
            options.stream_input = true;
            options.input = std::filesystem::path{"-"};
        } else if (argument == "--pretty") {
            options.pretty = true;
        } else if (argument == "--detailed") {
            options.detailed = true;
        } else if (argument.starts_with("--human-style=")) {
            options.human_style_specified = true;
            options.human_style = argument.substr(
                std::string_view{"--human-style="}.size());
        } else if (argument == "--human-style") {
            options.human_style_specified = true;
            auto value = require_value(argument);
            if (!value) return std::unexpected(std::move(value.error()));
            options.human_style = *value;
        } else if (argument.starts_with("--color=")) {
            options.color_specified = true;
            options.color = argument.substr(std::string_view{"--color="}.size());
        } else if (argument == "--color") {
            options.color_specified = true;
            auto value = require_value(argument);
            if (!value) return std::unexpected(std::move(value.error()));
            options.color = *value;
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

    if (options.database.empty() || options.format.empty()) {
        return std::unexpected("database and format are required");
    }
    if (options.stream_input && !options.word.empty()) {
        return std::unexpected(
            "positional text cannot be used with stream input");
    }
    if ((options.stream_input || options.word.empty()) && options.pretty) {
        return std::unexpected("--pretty cannot be used with stream input");
    }
    if (options.format != "analysis" && options.format != "search" &&
        options.format != "analysis-v2" && options.format != "search-v2" &&
        options.format != "analysis-v3" && options.format != "search-v3" &&
        options.format != "analysis-v4" && options.format != "search-v4" &&
        options.format != "human") {
        return std::unexpected(
            "format must be analysis, search, analysis-v2, search-v2, "
            "analysis-v3, search-v3, analysis-v4, search-v4, or human");
    }
    if (!options.filters.exclude_whitaker_trim_reasons.empty() &&
        options.format != "analysis-v3" && options.format != "search-v3" &&
        options.format != "analysis-v4" && options.format != "search-v4" &&
        options.format != "human") {
        return std::unexpected("trim filters require a v3 or human format");
    }
    if (options.human_style != "normal" && options.human_style != "compact")
        return std::unexpected("human style must be normal or compact");
    if (options.color != "auto" && options.color != "always" &&
        options.color != "never")
        return std::unexpected("color must be auto, always, or never");
    if (options.format == "human") {
        if (options.pretty)
            return std::unexpected("--pretty requires a JSON format");
        if (options.human_style == "compact" && options.color == "always")
            return std::unexpected("compact human output cannot be colored");
    } else if (options.detailed || options.human_style_specified ||
               options.color_specified) {
        return std::unexpected("--detailed, --human-style, and --color require human format");
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
                               (std::numeric_limits<std::streamsize>::max)())) {
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
  words_cli --database FILE --format FORMAT [OPTIONS] --input FILE
  words_cli --database FILE --format FORMAT [OPTIONS] < queries.txt

Options:
  --database FILE, --db FILE  WWDB database to load.
  --dataset-id ID             Expected dataset identifier, when known.
  --format FORMAT, -f FORMAT  human, analysis-v3/v4, or search-v3/v4.
  --pretty                    Indent JSON for terminal reading; emit an array for multiple results.
  --human-style STYLE         normal (default) or compact (tab-separated rows).
  --detailed                  Show full meanings, editorial notes, and quantity evidence.
  --color MODE                auto (TTY), always, or never; human display only.
  -i FILE, --input FILE       Read one query per line; use - for standard input.
                              With no text and no --input, read from standard input.
  --batch-json-lines, --batch Legacy aliases for --input -.
  --filter-trim MOTIVES       Comma-separated Whitaker trim reasons to hide (v3/v4/human).
                              Use none to explicitly disable filtering (default).
                              unsupported-short-imperative, invalid-imperative-person,
                              impersonal-non-third-person, deponent-active-form,
                              semideponent-passive-present-system,
                              semideponent-active-perfect-system.
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
  analysis-v4  Analysis with suffix quantity evidence; requires a full WWDB.
  search-v4    Search with suffix quantity evidence; works with both WWDBs.
  human        Readable analyses; requires a full WWDB.

Human output, including compact TSV, is presentation and may change in minor releases.
Select an explicit versioned format for a stable machine contract.

Exit status: 0 success; 2 invalid command; 3 database or engine failure;
4 unexpected failure. Results are written to stdout; CLI errors to stderr.
)");
}

[[nodiscard]] bool stdout_is_tty() noexcept {
#ifdef _WIN32
    return _isatty(_fileno(stdout)) != 0;
#else
    return isatty(fileno(stdout)) != 0;
#endif
}

[[nodiscard]] bool terminal_color_ready() noexcept {
#ifdef _WIN32
    const auto handle = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode{};
    if (handle == INVALID_HANDLE_VALUE || !GetConsoleMode(handle, &mode))
        return false;
    return SetConsoleMode(handle, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING) != 0;
#else
    return true;
#endif
}

[[nodiscard]] words::JsonDocument
result_document(const words::Engine &engine, const words::QueryResult &result,
                const std::string_view format,
                const words::client::ResultFilters &filters) {
    words::JsonDocument document;
    if (format == "analysis") {
        document = words::analysis_json_document(engine, result);
    } else if (format == "analysis-v2") {
        document = words::analysis_json_v2_document(engine, result);
    } else if (format == "analysis-v3") {
        document = words::analysis_json_v3_document(engine, result);
    } else if (format == "analysis-v4") {
        document = words::analysis_json_v4_document(engine, result);
    } else if (format == "search-v2") {
        document = words::search_json_v2_document(engine, result);
    } else if (format == "search-v3") {
        document = words::search_json_v3_document(engine, result);
    } else if (format == "search-v4") {
        document = words::search_json_v4_document(engine, result);
    } else {
        document = words::search_json_document(engine, result);
    }
    words::client::filter_result(document, filters);
    return document;
}

void write_result(const words::Engine &engine, const words::QueryResult &result,
                  const std::string_view format, const bool pretty,
                  const words::client::ResultFilters &filters,
                  const words::client::HumanOptions human,
                  std::size_t &result_number) {
    if (format == "human") {
        if (result_number > 0U && !human.compact) std::print(stdout, "\n");
        std::print(stdout, "{}", words::client::render_human(
            engine, result, filters, human, ++result_number));
        return;
    }
    const auto document = result_document(engine, result, format, filters);
    std::print(stdout, "{}\n", document.dump(pretty ? 2 : -1));
}

void write_text_result(const words::Engine &engine,
                       const std::string_view query,
                       const std::string_view format,
                       const words::AnalysisOptions options,
                       const bool pretty,
                       const words::client::ResultFilters &filters,
                       const words::client::HumanOptions human,
                       std::size_t &result_number) {
    write_result(engine, engine.analyze_text(query, options), format, pretty,
                 filters, human, result_number);
}

void write_line_results(const words::Engine &engine,
                        const std::string_view query,
                        const std::string_view format,
                        const words::AnalysisOptions options,
                        const bool pretty,
                        const words::client::ResultFilters &filters,
                        const words::client::HumanOptions human,
                        std::size_t &result_number) {
    const auto results = engine.analyze_line(query, options);
    if (pretty && results.size() > 1U) {
        auto document = words::JsonDocument::array();
        for (const auto &result : results) {
            document.push_back(result_document(engine, result, format, filters));
        }
        std::print(stdout, "{}\n", document.dump(2));
        return;
    }
    for (const auto &result : results) {
        write_result(engine, result, format, pretty, filters, human,
                     result_number);
    }
}

} // namespace

int main(const int argc, char *argv[]) try {
#ifdef _WIN32
    static_cast<void>(argc);
    static_cast<void>(argv);
    auto utf8_arguments = utf8_command_line();
    if (!utf8_arguments) {
        std::print(stderr, "words_cli: {}\n", utf8_arguments.error());
        return 2;
    }
    std::vector<char *> argument_pointers;
    argument_pointers.reserve(utf8_arguments->size());
    for (auto &argument : *utf8_arguments)
        argument_pointers.push_back(argument.data());
    const auto argument_count = static_cast<int>(argument_pointers.size());
    char *const *arguments = argument_pointers.data();
#else
    const auto argument_count = argc;
    char *const *arguments = argv;
#endif
    for (int index = 1; index < argument_count; ++index) {
        const std::string_view argument = arguments[index];
        if (argument == "--help" || argument == "-h") {
            usage();
            return 0;
        }
        if (argument == "--version") {
            std::print(stdout, "words_cli {}\n", WORDS_CLI_VERSION);
            return 0;
        }
    }
    auto options = parse_options(argument_count, arguments);
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
         options->format == "analysis-v3" ||
         options->format == "analysis-v4" ||
         options->format == "human") &&
        !(*engine)->supports_full_analysis()) {
        std::print(stderr, "words_cli: unsupported-output: selected format "
                           "requires a full WWDB with meanings\n");
        return 3;
    }

    const auto analysis_options = options->analysis;
    const auto compact = options->human_style == "compact";
    const auto color = options->format == "human" && !compact &&
        options->color != "never" &&
        (options->color == "always"
             ? (!stdout_is_tty() || terminal_color_ready())
             : (stdout_is_tty() && terminal_color_ready()));
    const words::client::HumanOptions human{
        .compact = compact, .detailed = options->detailed, .color = color};
    if (options->format == "human" && compact)
        std::print(stdout, "{}", words::client::human_header());
    std::size_t result_number{};
    if (options->stream_input || options->word.empty()) {
        // WHY: corpus acceptance should exercise one long-lived immutable
        // snapshot instead of measuring thousands of process startups.
        std::ifstream input_file;
        std::istream *input = &std::cin;
        if (options->input && *options->input != std::filesystem::path{"-"}) {
            input_file.open(*options->input);
            if (!input_file) {
                std::print(stderr, "words_cli: cannot open input: {}\n",
                           options->input->string());
                return 3;
            }
            input = &input_file;
        }
        std::string query;
        while (std::getline(*input, query)) {
            if (!query.empty() && query.back() == '\r') {
                query.pop_back();
            }
            if (!query.empty()) {
                write_text_result(**engine, query, options->format,
                                  analysis_options, false, options->filters,
                                  human, result_number);
            }
        }
        if (input->bad()) {
            const auto source =
                options->input && *options->input != std::filesystem::path{"-"}
                    ? options->input->string()
                    : std::string{"standard input"};
            std::print(stderr, "words_cli: cannot read input: {}\n", source);
            return 3;
        }
    } else {
        write_line_results(**engine, options->word, options->format,
                           analysis_options, options->pretty, options->filters,
                           human, result_number);
    }
    return 0;
} catch (const std::bad_alloc &) {
    std::print(stderr, "words_cli: out of memory\n");
    return 4;
} catch (const std::exception &error) {
    std::print(stderr, "words_cli: unexpected failure: {}\n", error.what());
    return 4;
}
