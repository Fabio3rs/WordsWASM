#include "latin_utf8.hpp"
#include "unicode_backend.hpp"

#include "words/lexer.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace latin = words::poc::latin_unicode;
namespace backend = words::poc::unicode_backend;

namespace {

struct Corpus final {
    std::vector<std::string> words;
    std::vector<backend::codepoint_t> scalars;
    std::vector<backend::byte_t> scalar_bytes;
    std::size_t word_bytes{};
};

[[nodiscard]] std::vector<std::string>
read_ascii_words(const std::string_view path) {
    std::ifstream input{std::string{path}, std::ios::binary};
    if (!input) {
        throw std::runtime_error{"could not open ASCII corpus"};
    }
    std::vector<std::string> words;
    std::string current;
    char item{};
    while (input.get(item)) {
        if (latin::detail::is_ascii_letter(item)) {
            current.push_back(item);
        } else if (!current.empty()) {
            words.push_back(std::move(current));
            current.clear();
        }
    }
    if (!current.empty()) {
        words.push_back(std::move(current));
    }
    return words;
}

void append_quantity_words(std::vector<std::string> &words) {
    constexpr std::array<std::string_view, 18> seeds{
        "puell\xC4\x81",
        "puella\xCC\x84",
        "puell\xC4\x83",
        "puella\xCC\x86",
        "m\xC4\x81lum",
        "ma\xCC\x84lum",
        "m\xC4\x83lum",
        "ma\xCC\x86lum",
        "J\xC5\xAAV\xC4\x94NIS",
        "\xC8\xB3",
        "y\xCC\x84",
        "Y\xCC\x84",
        "y\xCC\x86",
        "Y\xCC\x86",
        "r\xC4\x93sp\xC5\xAB"
        "lica",
        "\xC4\x80M\xC4\x82V\xC4\xAAT",
        "serv\xC5\xABs",
        "ros\xC4\x83",
    };
    constexpr std::size_t repetitions = 128U;
    words.reserve(words.size() + seeds.size() * repetitions);
    for (std::size_t repeat{}; repeat < repetitions; ++repeat) {
        for (const auto seed : seeds) {
            words.emplace_back(seed);
        }
    }
}

[[nodiscard]] Corpus make_corpus(const std::string_view path) {
    Corpus result;
    result.words = read_ascii_words(path);
    append_quantity_words(result.words);
    for (const auto &word : result.words) {
        result.word_bytes += word.size();
    }

    result.scalars.reserve(0x110000U);
    result.scalar_bytes.reserve(4U * 0x110000U);
    std::array<backend::byte_t, 4> encoded{};
    for (std::uint32_t scalar{}; scalar <= 0x10FFFFU; ++scalar) {
        if (scalar >= 0xD800U && scalar <= 0xDFFFU) {
            continue;
        }
        const auto size = backend::compact::encode_char(
            static_cast<backend::codepoint_t>(scalar), encoded.data());
        result.scalars.push_back(static_cast<backend::codepoint_t>(scalar));
        result.scalar_bytes.insert(result.scalar_bytes.end(), encoded.begin(),
                                   encoded.begin() + size);
    }
    return result;
}

#if defined(__GNUC__) || defined(__clang__)
#define WORDS_POC_NOINLINE __attribute__((noinline))
#else
#define WORDS_POC_NOINLINE
#endif

[[nodiscard]] WORDS_POC_NOINLINE std::uint64_t
run_compact_decode(const Corpus &corpus) {
    std::uint64_t checksum{};
    std::size_t offset{};
    while (offset < corpus.scalar_bytes.size()) {
        backend::codepoint_t codepoint{};
        const auto consumed = backend::compact::iterate(
            corpus.scalar_bytes.data() + offset,
            static_cast<backend::ssize_t>(corpus.scalar_bytes.size() - offset),
            &codepoint);
        if (consumed <= 0) {
            std::abort();
        }
        checksum += static_cast<std::uint32_t>(codepoint);
        offset += static_cast<std::size_t>(consumed);
    }
    return checksum;
}

[[nodiscard]] WORDS_POC_NOINLINE std::uint64_t
run_compact_category(const Corpus &corpus) {
    std::uint64_t checksum{};
    for (const auto codepoint : corpus.scalars) {
        checksum +=
            std::to_underlying(backend::compact::words_category(codepoint));
    }
    return checksum;
}

[[nodiscard]] WORDS_POC_NOINLINE std::uint64_t
run_compact_normalize(const Corpus &corpus) {
    std::uint64_t checksum{};
    const latin::LatinSurfaceNormalizer normalizer;
    for (const auto &word : corpus.words) {
        const auto surface = normalizer.normalize(word);
        if (!surface) {
            std::abort();
        }
        checksum += surface->normalized_nfc.size();
        checksum += surface->lookup_ascii.size();
        checksum += surface->nfc_byte_offsets.size();
    }
    return checksum;
}

[[nodiscard]] WORDS_POC_NOINLINE std::uint64_t
run_full_decode(const Corpus &corpus) {
    std::uint64_t checksum{};
    std::size_t offset{};
    while (offset < corpus.scalar_bytes.size()) {
        backend::codepoint_t codepoint{};
        const auto consumed = backend::full::iterate(
            corpus.scalar_bytes.data() + offset,
            static_cast<backend::ssize_t>(corpus.scalar_bytes.size() - offset),
            &codepoint);
        if (consumed <= 0) {
            std::abort();
        }
        checksum += static_cast<std::uint32_t>(codepoint);
        offset += static_cast<std::size_t>(consumed);
    }
    return checksum;
}

[[nodiscard]] WORDS_POC_NOINLINE std::uint64_t
run_full_category(const Corpus &corpus) {
    std::uint64_t checksum{};
    for (const auto codepoint : corpus.scalars) {
        checksum +=
            std::to_underlying(backend::full::words_category(codepoint));
    }
    return checksum;
}

[[nodiscard]] WORDS_POC_NOINLINE std::uint64_t
run_full_normalize(const Corpus &corpus) {
    std::uint64_t checksum{};
    const words::LatinLexer lexer;
    for (const auto &word : corpus.words) {
        const auto surface = lexer.lex(word);
        if (!surface) {
            std::abort();
        }
        checksum += surface->normalized_nfc.size();
        checksum += surface->lookup_ascii.size();
        checksum += surface->nfc_byte_offsets.size();
    }
    return checksum;
}

[[nodiscard]] WORDS_POC_NOINLINE std::uint64_t
run_compact_backend(const Corpus &corpus) {
    return run_compact_decode(corpus) + run_compact_category(corpus) +
           run_compact_normalize(corpus);
}

[[nodiscard]] WORDS_POC_NOINLINE std::uint64_t
run_full_backend(const Corpus &corpus) {
    return run_full_decode(corpus) + run_full_category(corpus) +
           run_full_normalize(corpus);
}

#undef WORDS_POC_NOINLINE

[[nodiscard]] std::size_t parse_iterations(const std::string_view input) {
    std::size_t result{};
    const auto [end, error] =
        std::from_chars(input.data(), input.data() + input.size(), result);
    if (error != std::errc{} || end != input.data() + input.size() ||
        result == 0U) {
        throw std::invalid_argument{"iterations must be positive"};
    }
    return result;
}

} // namespace

int main(const int argc, const char *const argv[]) {
    try {
        if (argc < 2 || (std::string_view{argv[1]} != "compact" &&
                         std::string_view{argv[1]} != "full")) {
            std::cerr << "usage: " << argv[0]
                      << " compact|full [iterations] [ASCII_CORPUS]\n";
            return 2;
        }
        const std::string_view selected{argv[1]};
        const auto iterations =
            argc >= 3 ? parse_iterations(argv[2]) : std::size_t{1U};
        const std::string_view path =
            argc >= 4 ? argv[3] : WORDS_LATIN_POC_ASCII_CORPUS;
        const auto corpus = make_corpus(path);

        std::uint64_t checksum{};
        const auto started = std::chrono::steady_clock::now();
        for (std::size_t iteration{}; iteration < iterations; ++iteration) {
            checksum += selected == "compact" ? run_compact_backend(corpus)
                                              : run_full_backend(corpus);
        }
        const auto elapsed =
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now() - started)
                .count();
        std::cout << "backend,iterations,word_count,word_bytes,scalar_bytes,"
                     "elapsed_ns,checksum\n"
                  << selected << ',' << iterations << ',' << corpus.words.size()
                  << ',' << corpus.word_bytes << ','
                  << corpus.scalar_bytes.size() << ',' << elapsed << ','
                  << checksum << '\n';
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 2;
    }
    return 0;
}
