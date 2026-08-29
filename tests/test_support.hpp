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

[[nodiscard]] inline std::vector<std::byte> read_database() {
    const std::filesystem::path path{WORDS_TEST_WWDB};
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

} // namespace words::test
