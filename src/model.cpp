#include "words/model.hpp"

#include <cstddef>

namespace words {

std::string_view SurfaceForm::slice(const SurfaceRange range) const noexcept {
    const auto begin = static_cast<std::size_t>(range.begin);
    const auto count = static_cast<std::size_t>(range.count);
    if (begin > quantities.size() || count > quantities.size() - begin ||
        nfc_byte_offsets.size() != quantities.size() + 1U) {
        return {};
    }
    const auto first_byte = static_cast<std::size_t>(nfc_byte_offsets[begin]);
    const auto last_byte =
        static_cast<std::size_t>(nfc_byte_offsets[begin + count]);
    return std::string_view{normalized_nfc}.substr(first_byte,
                                                   last_byte - first_byte);
}

} // namespace words
