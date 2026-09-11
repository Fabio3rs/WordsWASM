#include "latin_utf8.hpp"

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

namespace {

constexpr std::size_t quantity_corpus_repetitions = 128U;
constexpr std::size_t caller_storage_capacity = 64U;

struct Corpus final {
    std::string name;
    std::vector<std::string> words;
    std::vector<std::vector<char32_t>> scalars;
    std::vector<std::vector<latin::LatinGlyph>> glyphs;
    std::size_t byte_count{};
};

[[nodiscard]] std::vector<std::string>
read_ascii_words(const std::string_view path) {
    std::ifstream input{std::string{path}, std::ios::binary};
    if (!input) {
        throw std::runtime_error{"could not open ASCII corpus"};
    }
    std::vector<std::string> words;
    std::string current;
    char value{};
    while (input.get(value)) {
        if (latin::detail::is_ascii_letter(value)) {
            current.push_back(value);
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

[[nodiscard]] std::vector<std::string> quantity_words() {
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
        "blica",
        "\xC4\x80M\xC4\x82V\xC4\xAAT",
        "serv\xC5\xABs",
        "ros\xC4\x83",
    };
    std::vector<std::string> result;
    result.reserve(seeds.size() * quantity_corpus_repetitions);
    for (std::size_t repeat = 0U; repeat < quantity_corpus_repetitions;
         ++repeat) {
        for (const auto seed : seeds) {
            result.emplace_back(seed);
        }
    }
    return result;
}

[[nodiscard]] Corpus make_corpus(std::string name,
                                 std::vector<std::string> words) {
    Corpus result{.name = std::move(name),
                  .words = std::move(words),
                  .scalars = {},
                  .glyphs = {},
                  .byte_count = 0U};
    result.scalars.reserve(result.words.size());
    result.glyphs.reserve(result.words.size());
    const latin::LatinSurfaceNormalizer normalizer;
    for (const auto &word : result.words) {
        result.byte_count += word.size();
        std::vector<char32_t> scalars;
        std::size_t offset{};
        while (offset < word.size()) {
            const auto decoded = latin::decode_utf8_scalar(word, offset);
            if (!decoded) {
                throw std::runtime_error{"benchmark corpus has invalid UTF-8"};
            }
            scalars.push_back(decoded->value);
            offset += decoded->byte_count;
        }
        result.scalars.push_back(std::move(scalars));

        const auto surface = normalizer.normalize(word);
        if (!surface) {
            throw std::runtime_error{
                "benchmark corpus is outside Latin domain"};
        }
        std::vector<latin::LatinGlyph> glyphs;
        glyphs.reserve(surface->quantities.size());
        for (std::size_t index = 0U; index < surface->quantities.size();
             ++index) {
            glyphs.push_back(latin::LatinGlyph{
                .base = surface->orthography_ascii[index],
                .quantity = surface->quantities[index],
            });
        }
        result.glyphs.push_back(std::move(glyphs));
    }
    return result;
}

template <class Operation>
void measure(const Corpus &corpus, const std::string_view stage,
             const std::size_t iterations, Operation operation) {
    constexpr std::size_t samples = 7U;
    std::vector<std::chrono::nanoseconds::rep> timings;
    timings.reserve(samples);
    std::uint64_t checksum{};
    for (std::size_t sample = 0U; sample < samples + 1U; ++sample) {
        const auto started = std::chrono::steady_clock::now();
        std::uint64_t sample_checksum{};
        for (std::size_t iteration = 0U; iteration < iterations; ++iteration) {
            sample_checksum += operation(corpus);
        }
        const auto elapsed =
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now() - started);
        if (sample != 0U) {
            timings.push_back(elapsed.count());
            checksum ^= sample_checksum;
        }
    }
    std::ranges::sort(timings);
    const auto median = timings[timings.size() / 2U];
    const auto total_bytes = static_cast<double>(corpus.byte_count) *
                             static_cast<double>(iterations);
    std::cout << corpus.name << ',' << stage << ',' << corpus.words.size()
              << ',' << corpus.byte_count << ',' << iterations << ',' << median
              << ',' << static_cast<double>(median) / total_bytes << ','
              << checksum << '\n';
}

[[nodiscard]] std::size_t parse_iterations(const char *value) {
    const std::string_view text{value};
    std::size_t parsed{};
    const auto [end, error] =
        std::from_chars(text.data(), text.data() + text.size(), parsed);
    if (error != std::errc{} || end != text.data() + text.size() ||
        parsed == 0U) {
        throw std::invalid_argument{
            "iterations must fit size_t and be positive"};
    }
    return parsed;
}

} // namespace

int main(const int argc, const char *const argv[]) {
    try {
        const std::string_view ascii_path =
            argc >= 2 ? argv[1] : WORDS_LATIN_POC_ASCII_CORPUS;
        const auto iterations = argc >= 3 ? parse_iterations(argv[2]) : 20U;
        const auto ascii = make_corpus("ascii", read_ascii_words(ascii_path));
        const auto quantity = make_corpus("quantity", quantity_words());
        const latin::LatinSurfaceNormalizer normalizer;
        const words::LatinLexer lexer;

        std::cout << "corpus,stage,words,bytes,iterations,median_ns,ns_per_"
                     "byte,checksum\n";
        for (const auto *corpus : {&ascii, &quantity}) {
            measure(*corpus, "decode", iterations, [](const Corpus &data) {
                std::uint64_t checksum{};
                for (const auto &word : data.words) {
                    std::size_t offset{};
                    while (offset < word.size()) {
                        const auto decoded =
                            latin::decode_utf8_scalar(word, offset);
                        if (!decoded) {
                            std::abort();
                        }
                        checksum += static_cast<std::uint32_t>(decoded->value);
                        offset += decoded->byte_count;
                    }
                }
                return checksum;
            });
            measure(*corpus, "mapping", iterations, [](const Corpus &data) {
                std::uint64_t checksum{};
                for (const auto &word : data.scalars) {
                    for (const auto scalar : word) {
                        const auto mapping = latin::map_latin_codepoint(scalar);
                        if (!mapping) {
                            std::abort();
                        }
                        checksum +=
                            static_cast<unsigned char>(mapping->glyph.base);
                        checksum += std::to_underlying(mapping->glyph.quantity);
                    }
                }
                return checksum;
            });
            measure(*corpus, "composition", iterations, [](const Corpus &data) {
                std::uint64_t checksum{};
                for (const auto &word : data.glyphs) {
                    for (const auto glyph : word) {
                        const auto encoded =
                            latin::encode_latin_glyph_nfc(glyph);
                        if (!encoded) {
                            std::abort();
                        }
                        checksum += encoded->at(0).byte_count;
                        checksum += encoded->at(1).byte_count;
                    }
                }
                return checksum;
            });
            measure(*corpus, "poc_surface", iterations,
                    [&normalizer](const Corpus &data) {
                        std::uint64_t checksum{};
                        for (const auto &word : data.words) {
                            const auto surface = normalizer.normalize(word);
                            if (!surface) {
                                std::abort();
                            }
                            checksum += surface->normalized_nfc.size();
                            checksum += surface->lookup_ascii.size();
                            checksum += surface->nfc_byte_offsets.size();
                        }
                        return checksum;
                    });
            measure(
                *corpus, "poc_surface_into", iterations,
                [&normalizer](const Corpus &data) {
                    std::uint64_t checksum{};
                    std::array<char, caller_storage_capacity> normalized;
                    std::array<char, caller_storage_capacity> orthography;
                    std::array<char, caller_storage_capacity> lookup;
                    std::array<latin::LatinQuantity, caller_storage_capacity>
                        quantities;
                    std::array<std::uint32_t, caller_storage_capacity + 1U>
                        offsets;
                    const latin::LatinSurfaceBuffers buffers{
                        .normalized_nfc = normalized,
                        .orthography_ascii = orthography,
                        .lookup_ascii = lookup,
                        .quantities = quantities,
                        .nfc_byte_offsets = offsets,
                    };
                    for (const auto &word : data.words) {
                        if (word.size() > caller_storage_capacity) {
                            std::abort();
                        }
                        const auto surface =
                            normalizer.normalize_into(word, buffers);
                        if (!surface) {
                            std::abort();
                        }
                        checksum += surface->normalized_nfc.size();
                        checksum += surface->lookup_ascii.size();
                        checksum += surface->nfc_byte_offsets.size();
                    }
                    return checksum;
                });
            measure(*corpus, "utf8proc_lexer", iterations,
                    [&lexer](const Corpus &data) {
                        std::uint64_t checksum{};
                        for (const auto &word : data.words) {
                            const auto surface = lexer.lex(word);
                            if (!surface) {
                                std::abort();
                            }
                            checksum += surface->normalized_nfc.size();
                            checksum += surface->lookup_ascii.size();
                            checksum += surface->nfc_byte_offsets.size();
                        }
                        return checksum;
                    });
        }
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 2;
    }
    return 0;
}
