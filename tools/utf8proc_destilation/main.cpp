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
    table_dmp << "#include <cstdint>\n";
    table_dmp << "#include <print>\n";
    table_dmp << "#include <utility>\n";
    table_dmp << "\n";
    table_dmp << "using pair_t = std::pair<int32_t, int8_t>;\n";
    table_dmp << "\n";
    table_dmp << "constexpr std::array<pair_t, " << codepoints.size()
              << "> codepoints = {\n";
    for (const auto &[codepoint, category] : codepoints) {
        table_dmp << "    pair_t{ " << codepoint << ", " << category << " },\n";
    }
    table_dmp << "};\n";

    table_dmp << "\n";
    table_dmp << R"cpp(
int main() {
    std::print("Unicode codepoints count: {}\n", codepoints.size());
    std::print("Unicode table size: {}\n", sizeof(codepoints));
    return 0;
}
    )cpp";
    table_dmp.close();

    // switch table generation
    std::fstream switch_dmp("unicode_switch.cpp", std::ios::out);
    if (!switch_dmp) {
        std::print("Error opening switch.cpp for writing\n");
        return 1;
    }

    switch_dmp << "#include <cstdint>\n";
    switch_dmp << "#include <print>\n";
    switch_dmp << "\n";
    switch_dmp << "// NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers, "
                  "readability-magic-numbers)\n";
    switch_dmp << "// NOLINTNEXTLINE\n";
    switch_dmp << "int8_t get_unicode_category(int32_t codepoint) {\n";
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
int main(int argc, char **argv) {
    if (argc > 1) {
        int32_t codepoint = std::stoi(argv[1], nullptr, 16);
        std::print("Unicode category: {}\n", get_unicode_category(codepoint));
    }
    return 0;
}
    )cpp";

    switch_dmp.close();

    return 0;
}