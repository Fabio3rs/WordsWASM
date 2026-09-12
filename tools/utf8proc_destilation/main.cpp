#include <array>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <filesystem>
#include <ostream>
#include <print>
#include <string_view>
#include <utf8proc.h>
#include <utility>
#include <vector>

namespace {

using codepoint_entry_t = std::pair<utf8proc_int32_t, utf8proc_category_t>;
using codepoint_list_t = std::vector<codepoint_entry_t>;

enum class SwitchSemantics : std::uint8_t {
    utf8proc_category,
    boundary_flag,
};

enum class BoundaryResult : std::uint8_t {
    quote,
    dash,
    bracket,
    other_punctuation,
};

// Experimental mirror of the cases that return before utf8proc_category in
// boundary_flag (src/lexer.cpp). This is deliberately not production data.
[[nodiscard]] constexpr bool
has_specialized_boundary_flag(const utf8proc_int32_t codepoint) noexcept {
    switch (codepoint) {
    case U'!':
    case U'"':
    case U'\'':
    case U',':
    case U'.':
    case U':':
    case U';':
    case U'?':
    case U'،':
    case U'؛':
    case U'؟':
    case U'’':
    case U'․':
    case U'…':
    case U'、':
    case U'。':
    case U'！':
    case U'，':
    case U'．':
    case U'：':
    case U'；':
    case U'？':
        return true;
    default:
        return false;
    }
}

[[nodiscard]] constexpr int
switch_result(const utf8proc_category_t category,
              const SwitchSemantics semantics) noexcept {
    if (semantics == SwitchSemantics::utf8proc_category) {
        return static_cast<int>(category);
    }
    if (category == UTF8PROC_CATEGORY_PI || category == UTF8PROC_CATEGORY_PF) {
        return static_cast<int>(BoundaryResult::quote);
    }
    if (category == UTF8PROC_CATEGORY_PD) {
        return static_cast<int>(BoundaryResult::dash);
    }
    if (category == UTF8PROC_CATEGORY_PS || category == UTF8PROC_CATEGORY_PE) {
        return static_cast<int>(BoundaryResult::bracket);
    }
    return static_cast<int>(BoundaryResult::other_punctuation);
}

void write_switch_return(std::ostream &output, const int result,
                         const SwitchSemantics semantics) {
    output << "            return ";
    if (semantics == SwitchSemantics::utf8proc_category) {
        output << result;
    } else {
        switch (static_cast<BoundaryResult>(result)) {
        case BoundaryResult::quote:
            output << "words::BoundaryFlag::quote";
            break;
        case BoundaryResult::dash:
            output << "words::BoundaryFlag::dash";
            break;
        case BoundaryResult::bracket:
            output << "words::BoundaryFlag::bracket";
            break;
        case BoundaryResult::other_punctuation:
            output << "words::BoundaryFlag::other_punctuation";
            break;
        }
    }
    output << ";\n";
}

[[nodiscard]] constexpr std::string_view
boundary_result_name(const BoundaryResult result) noexcept {
    switch (result) {
    case BoundaryResult::quote:
        return "boundary_flag_t::quote";
    case BoundaryResult::dash:
        return "boundary_flag_t::dash";
    case BoundaryResult::bracket:
        return "boundary_flag_t::bracket";
    case BoundaryResult::other_punctuation:
        return "boundary_flag_t::other_punctuation";
    }
    return "boundary_flag_t::none";
}

[[nodiscard]] bool
write_compact_category_library(const std::filesystem::path &directory,
                               const codepoint_list_t &codepoints) {
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    if (error) {
        std::print("Error creating {}: {}\n", directory.string(),
                   error.message());
        return false;
    }

    const auto header_path = directory / "compact_words_category.hpp";
    std::fstream header(header_path, std::ios::out);
    if (!header) {
        std::print("Error opening {} for writing\n", header_path.string());
        return false;
    }
    header << R"cpp(#pragma once

#include <cstdint>
#include <string_view>

namespace words::poc::unicode_backend {

using codepoint_t = std::int32_t;

// Values intentionally mirror words::BoundaryFlag. The generated compact
// backend stays independent of production headers; differential tests assert
// that these bits have not drifted.
enum class boundary_flag_t : std::uint16_t {
    none = 0,
    quote = 1U << 7U,
    dash = 1U << 9U,
    bracket = 1U << 10U,
    other_punctuation = 1U << 11U,
};

inline constexpr std::string_view utf8proc_version = ")cpp"
           << utf8proc_version() << R"cpp(";
inline constexpr std::string_view unicode_version = ")cpp"
           << utf8proc_unicode_version() << R"cpp(";
inline constexpr std::uint32_t punctuation_codepoint_count = )cpp"
           << codepoints.size() << R"cpp(U;

namespace compact {

[[nodiscard]] boundary_flag_t words_category(codepoint_t codepoint) noexcept;

} // namespace compact
} // namespace words::poc::unicode_backend
)cpp";
    header.close();

    const auto source_path = directory / "compact_words_category.cpp";
    std::fstream source(source_path, std::ios::out);
    if (!source) {
        std::print("Error opening {} for writing\n", source_path.string());
        return false;
    }
    source << R"cpp(#include "compact_words_category.hpp"

namespace words::poc::unicode_backend::compact {

// Generated from utf8proc's Unicode data. Keep this as a library translation
// unit: no main, std::print, Emscripten export, or keepalive scaffold belongs
// in the production-shaped candidate.
// NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers,
//              readability-magic-numbers)
boundary_flag_t words_category(const codepoint_t codepoint) noexcept {
    switch (codepoint) {
)cpp";

    BoundaryResult last_result{};
    bool has_last_result{};
    std::size_t groups_count{};
    for (const auto &[codepoint, category] : codepoints) {
        const auto result = static_cast<BoundaryResult>(
            switch_result(category, SwitchSemantics::boundary_flag));
        if (has_last_result && last_result != result) {
            source << "            return " << boundary_result_name(last_result)
                   << ";\n";
            ++groups_count;
        }
        source << "        case " << codepoint << ":\n";
        last_result = result;
        has_last_result = true;
    }
    if (has_last_result) {
        source << "            return " << boundary_result_name(last_result)
               << ";\n";
        ++groups_count;
    }
    source << R"cpp(        default:
            return boundary_flag_t::none;
    }
}
// NOLINTEND(cppcoreguidelines-avoid-magic-numbers,
//            readability-magic-numbers)

} // namespace words::poc::unicode_backend::compact
)cpp";
    source.close();

    std::print("{}: {} codepoints in {} return groups\n", source_path.string(),
               codepoints.size(), groups_count);
    return true;
}

[[nodiscard]] bool write_switch_file(const char *const path,
                                     const char *const function_name,
                                     const SwitchSemantics semantics,
                                     const bool omit_specialized_codepoints,
                                     const codepoint_list_t &codepoints) {
    std::fstream output(path, std::ios::out);
    if (!output) {
        std::print("Error opening {} for writing\n", path);
        return false;
    }

    output << "#include <cstdint>\n";
    if (semantics == SwitchSemantics::boundary_flag) {
        output << "#include \"words/lexer.hpp\"\n";
    }
    output << "\n"
              "#if defined(__EMSCRIPTEN__)\n"
              "#include <emscripten/emscripten.h>\n"
              "#else\n"
              "#include <print>\n"
              "#include <string>\n"
              "#endif\n\n"
              "// NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers, "
              "readability-magic-numbers)\n"
              "#if defined(__EMSCRIPTEN__)\n"
              "#define UTF8PROC_DESTILATION_KEEPALIVE "
              "EMSCRIPTEN_KEEPALIVE\n"
              "#else\n"
              "#define UTF8PROC_DESTILATION_KEEPALIVE\n"
              "#endif\n\n"
              "extern \"C\" UTF8PROC_DESTILATION_KEEPALIVE ";
    if (semantics == SwitchSemantics::utf8proc_category) {
        output << "std::int8_t\n";
    } else {
        output << "words::BoundaryFlag\n";
    }
    output << function_name << "(const ";
    if (semantics == SwitchSemantics::utf8proc_category) {
        output << "std::int32_t";
    } else {
        output << "char32_t";
    }
    output << " codepoint) noexcept {\n"
              "    switch (codepoint) {\n";

    int last_result{};
    bool has_last_result{};
    std::size_t items_count{};
    std::size_t groups_count{};
    for (const auto &[codepoint, category] : codepoints) {
        if (omit_specialized_codepoints &&
            has_specialized_boundary_flag(codepoint)) {
            continue;
        }
        const auto result = switch_result(category, semantics);
        if (has_last_result && last_result != result) {
            write_switch_return(output, last_result, semantics);
            ++groups_count;
        }
        output << "        case " << codepoint << ":\n";
        last_result = result;
        has_last_result = true;
        ++items_count;
    }
    if (has_last_result) {
        write_switch_return(output, last_result, semantics);
        ++groups_count;
    }

    output << "\n        default: return ";
    if (semantics == SwitchSemantics::utf8proc_category) {
        output << "-1";
    } else {
        output << "words::BoundaryFlag::none";
    }
    output << ";\n"
              "    }\n"
              "}\n\n"
              "// NOLINTEND(cppcoreguidelines-avoid-magic-numbers, "
              "readability-magic-numbers)\n\n"
              "#undef UTF8PROC_DESTILATION_KEEPALIVE\n\n"
              "#if defined(__EMSCRIPTEN__)\n"
              "int main() { return 0; }\n"
              "#else\n"
              "int main(const int argc, const char *const argv[]) {\n"
              "    if (argc > 1) {\n"
              "        const auto codepoint = std::stoi(argv[1], nullptr, "
              "16);\n"
              "        std::print(\"Result: {}\\n\", static_cast<int>(\n"
           << "            " << function_name << '(';
    if (semantics == SwitchSemantics::boundary_flag) {
        output << "static_cast<char32_t>(codepoint)";
    } else {
        output << "codepoint";
    }
    output << ")));\n"
              "    }\n"
              "    return 0;\n"
              "}\n"
              "#endif\n";
    output.close();

    std::print("{}: {} codepoints in {} return groups\n", path, items_count,
               groups_count);
    return true;
}

} // namespace

int main(const int argc, const char *const argv[]) {
    size_t byte_count = 0;
    size_t items_count = 0;
    std::array<utf8proc_uint8_t, 4> toDiscard{};

    codepoint_list_t codepoints;
    constexpr auto DEFAULT_RESERVE = 10000;
    constexpr auto MAX_KNOWN_CODEPOINTS = 0x110000;
    codepoints.reserve(DEFAULT_RESERVE);

    for (utf8proc_int32_t codepoint = 0; codepoint <= MAX_KNOWN_CODEPOINTS;
         ++codepoint) {
        if (!utf8proc_codepoint_valid(codepoint)) {
            continue;
        }

        const auto category = utf8proc_category(codepoint);
        if (category == UTF8PROC_CATEGORY_PI ||
            category == UTF8PROC_CATEGORY_PF) {
            std::print("Quote: U+{:X}\n", codepoint);
            byte_count += utf8proc_encode_char(codepoint, toDiscard.data());
            ++items_count;
            codepoints.emplace_back(codepoint, category);
        } else if (category == UTF8PROC_CATEGORY_PD) {
            std::print("Dash: U+{:X}\n", codepoint);
            byte_count += utf8proc_encode_char(codepoint, toDiscard.data());
            ++items_count;
            codepoints.emplace_back(codepoint, category);
        } else if (category == UTF8PROC_CATEGORY_PS ||
                   category == UTF8PROC_CATEGORY_PE) {
            std::print("Bracket: U+{:X}\n", codepoint);
            byte_count += utf8proc_encode_char(codepoint, toDiscard.data());
            ++items_count;
            codepoints.emplace_back(codepoint, category);
        } else if (category >= UTF8PROC_CATEGORY_PC &&
                   category <= UTF8PROC_CATEGORY_PO) {
            std::print("Other Punctuation: U+{:X}\n", codepoint);
            byte_count += utf8proc_encode_char(codepoint, toDiscard.data());
            ++items_count;
            codepoints.emplace_back(codepoint, category);
        }
    }

    std::print("Total bytes used: {}\n", byte_count);
    std::print("Total items counted: {}\n", items_count);

    std::fstream table_dmp("unicode_table.cpp", std::ios::out);
    if (!table_dmp) {
        std::print("Error opening table.cpp for writing\n");
        return 1;
    }

    table_dmp << "#include <array>\n";
    table_dmp << "#include <cstddef>\n";
    table_dmp << "#include <cstdint>\n";
    table_dmp << "#include <utility>\n";
    table_dmp << "\n";
    table_dmp << "#if defined(__EMSCRIPTEN__)\n";
    table_dmp << "#include <emscripten/emscripten.h>\n";
    table_dmp << "#else\n";
    table_dmp << "#include <print>\n";
    table_dmp << "#include <string>\n";
    table_dmp << "#endif\n";
    table_dmp << "\n";
    table_dmp << "using pair_t = std::pair<std::int32_t, std::int8_t>;\n";
    table_dmp << "\n";
    table_dmp << "constexpr std::array<pair_t, " << codepoints.size()
              << "> codepoints = {\n";
    for (const auto &[codepoint, category] : codepoints) {
        table_dmp << "    pair_t{ " << codepoint << ", " << category << " },\n";
    }
    table_dmp << "};\n";

    table_dmp << "\n";
    table_dmp << R"cpp(
#if defined(__EMSCRIPTEN__)
#define UTF8PROC_DESTILATION_KEEPALIVE EMSCRIPTEN_KEEPALIVE
#else
#define UTF8PROC_DESTILATION_KEEPALIVE
#endif

extern "C" UTF8PROC_DESTILATION_KEEPALIVE std::int8_t
get_unicode_category(const std::int32_t codepoint) {
    std::size_t first{};
    auto count = codepoints.size();
    while (count != 0U) {
        const auto step = count / 2U;
        const auto index = first + step;
        if (codepoints[index].first < codepoint) {
            first = index + 1U;
            count -= step + 1U;
        } else {
            count = step;
        }
    }
    if (first == codepoints.size() ||
        codepoints[first].first != codepoint) {
        return -1;
    }
    return codepoints[first].second;
}

#undef UTF8PROC_DESTILATION_KEEPALIVE

#if defined(__EMSCRIPTEN__)
int main() { return 0; }
#else
int main(const int argc, const char *const argv[]) {
    std::print("Unicode codepoints count: {}\n", codepoints.size());
    std::print("Unicode table size: {}\n", sizeof(codepoints));
    if (argc > 1) {
        const auto codepoint = std::stoi(argv[1], nullptr, 16);
        std::print("Unicode category: {}\n",
                   static_cast<int>(get_unicode_category(codepoint)));
    }
    return 0;
}
#endif
)cpp";
    table_dmp.close();

    if (!write_switch_file("unicode_switch.cpp", "get_unicode_category",
                           SwitchSemantics::utf8proc_category, false,
                           codepoints) ||
        !write_switch_file("unicode_boundary_switch.cpp",
                           "unicode_boundary_flag",
                           SwitchSemantics::boundary_flag, false, codepoints) ||
        !write_switch_file("unicode_boundary_fallback_switch.cpp",
                           "unicode_boundary_fallback_flag",
                           SwitchSemantics::boundary_flag, true, codepoints)) {
        return 1;
    }

    if (argc == 3 && std::string_view{argv[1]} == "--library-output-dir") {
        if (!write_compact_category_library(argv[2], codepoints)) {
            return 1;
        }
    } else if (argc != 1) {
        std::print("Usage: {} [--library-output-dir PATH]\n", argv[0]);
        return 2;
    }

    return 0;
}
