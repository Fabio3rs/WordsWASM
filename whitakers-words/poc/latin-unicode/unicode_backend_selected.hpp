#pragma once

#include "unicode_backend.hpp"

#if defined(WORDS_POC_BACKEND_COMPACT) && defined(WORDS_POC_BACKEND_FULL)
#error "select exactly one Unicode backend"
#elif defined(WORDS_POC_BACKEND_COMPACT)
namespace words::poc::unicode_backend {
namespace selected = compact;
}
#elif defined(WORDS_POC_BACKEND_FULL)
namespace words::poc::unicode_backend {
namespace selected = full;
}
#else
#error "a Unicode backend must be selected by the build"
#endif
