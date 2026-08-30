#pragma once
#ifndef WORDS_LIFETIMEBOUND
#if defined(__clang__) || defined(__EMSCRIPTEN__)
#define WORDS_LIFETIMEBOUND [[clang::lifetimebound]]
#else
#define WORDS_LIFETIMEBOUND
#endif
#endif

