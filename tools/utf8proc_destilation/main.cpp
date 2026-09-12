#include <array>
#include <cstddef>
#include <fstream>
#include <print>
#include <utf8proc.h>
#include <utility>
#include <vector>

int main() {
    size_t byte_count = 0;
    size_t items_count = 0;
    std::array<utf8proc_uint8_t, 4> toDiscard{};

    std::vector<std::pair<utf8proc_int32_t, utf8proc_category_t>> codepoints;
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

    // switch table generation
    std::fstream switch_dmp("unicode_switch.cpp", std::ios::out);
    if (!switch_dmp) {
        std::print("Error opening switch.cpp for writing\n");
        return 1;
    }

    switch_dmp << "#include <cstdint>\n";
    switch_dmp << "\n";
    switch_dmp << "#if defined(__EMSCRIPTEN__)\n";
    switch_dmp << "#include <emscripten/emscripten.h>\n";
    switch_dmp << "#else\n";
    switch_dmp << "#include <print>\n";
    switch_dmp << "#include <string>\n";
    switch_dmp << "#endif\n";
    switch_dmp << "\n";
    switch_dmp << "// NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers, "
                  "readability-magic-numbers)\n";
    switch_dmp << "// NOLINTNEXTLINE\n";
    switch_dmp << "#if defined(__EMSCRIPTEN__)\n";
    switch_dmp << "#define UTF8PROC_DESTILATION_KEEPALIVE "
                  "EMSCRIPTEN_KEEPALIVE\n";
    switch_dmp << "#else\n";
    switch_dmp << "#define UTF8PROC_DESTILATION_KEEPALIVE\n";
    switch_dmp << "#endif\n";
    switch_dmp << "\n";
    switch_dmp << "extern \"C\" UTF8PROC_DESTILATION_KEEPALIVE "
                  "std::int8_t\n";
    switch_dmp << "get_unicode_category(const std::int32_t codepoint) {\n";
    switch_dmp << "    switch (codepoint) {\n";

    int last = -1;

    for (const auto &[codepoint, category] : codepoints) {
        if (last != -1 && last != category) {
            switch_dmp << "            return " << static_cast<int>(last)
                       << ";\n";
        }
        switch_dmp << "        case " << codepoint << ":\n";
        last = category;
    }
    if (last != -1) {
        switch_dmp << "            return " << last << ";\n";
    }

    switch_dmp << "\n"
                  "        default: return -1;\n"
                  "    }\n"
                  "}\n";
    switch_dmp << "\n";

    switch_dmp << "// NOLINTEND(cppcoreguidelines-avoid-magic-numbers, "
                  "readability-magic-numbers)\n";

    switch_dmp << R"cpp(
#undef UTF8PROC_DESTILATION_KEEPALIVE

#if defined(__EMSCRIPTEN__)
int main() { return 0; }
#else
int main(const int argc, const char *const argv[]) {
    if (argc > 1) {
        const auto codepoint = std::stoi(argv[1], nullptr, 16);
        std::print("Unicode category: {}\n",
                   static_cast<int>(get_unicode_category(codepoint)));
    }
    return 0;
}
#endif
)cpp";

    switch_dmp.close();

    return 0;
}
