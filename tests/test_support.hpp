#pragma once

#include "words/engine.hpp"

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <utility>
#include <vector>

namespace words::test {

inline constexpr auto dataset_id =
    "sha256:0000000000000000000000000000000000000000000000000000000000000000";

[[nodiscard]] inline std::vector<std::byte>
read_database_file(const std::filesystem::path &path) {
    std::ifstream input{path, std::ios::binary | std::ios::ate};
    if (!input) {
        throw std::runtime_error{"cannot open test WWDB"};
    }
    const auto end = input.tellg();
    if (end < 0) {
        throw std::runtime_error{"cannot determine test WWDB size"};
    }
    std::vector<std::byte> bytes(static_cast<std::size_t>(end));
    input.seekg(0);
    input.read(reinterpret_cast<char *>(bytes.data()), end);
    if (!input) {
        throw std::runtime_error{"cannot read test WWDB"};
    }
    return bytes;
}

[[nodiscard]] inline std::vector<std::byte> read_database() {
    return read_database_file(WORDS_TEST_WWDB);
}

[[nodiscard]] inline std::vector<std::byte> read_search_database() {
    return read_database_file(WORDS_TEST_SEARCH_WWDB);
}

[[nodiscard]] inline const Engine &engine() {
    static const auto value = [] {
        auto loaded = Engine::create(read_database(), EngineConfig{dataset_id});
        if (!loaded) {
            throw std::runtime_error{loaded.error().code + ": " +
                                     loaded.error().message};
        }
        return std::move(*loaded);
    }();
    return *value;
}

[[nodiscard]] inline const Engine &search_engine() {
    static const auto value = [] {
        auto loaded =
            Engine::create(read_search_database(), EngineConfig{dataset_id});
        if (!loaded) {
            throw std::runtime_error{loaded.error().code + ": " +
                                     loaded.error().message};
        }
        return std::move(*loaded);
    }();
    return *value;
}

} // namespace words::test
