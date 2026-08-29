cmake_minimum_required(VERSION 3.12)
project(WordsWASM C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_C_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_EXTENSIONS OFF)

if ("${CMAKE_BUILD_TYPE}" STREQUAL "Debug")
    set(SHOULD_ENABLE_SANITIZERS ON)
else()
    set(SHOULD_ENABLE_SANITIZERS OFF)
endif()

if(EMSCRIPTEN)
    message(STATUS "Building for WASM")
endif()

option(ENABLE_TESTS "Enable tests" ON)
option(ENABLE_SANITIZERS "Enable sanitizers" ${SHOULD_ENABLE_SANITIZERS})
option(ENABLE_WEB_COMPRESSION "Generate Brotli/Gzip compressed web assets when building with Emscripten" ON)

# Alpine's GCC toolchain defines _FORTIFY_SOURCE by default. Undefine the
# toolchain-provided value before selecting ours to avoid a redefinition
# warning becoming an error under -Werror.
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=3)
endif()

add_subdirectory(vendor/utf8proc SYSTEM)
add_subdirectory(vendor/json SYSTEM)

if (${CMAKE_CXX_COMPILER_ID} STREQUAL "GNU")
    message(STATUS "Setting G++ flags")
    add_compile_options(-Wall -Werror -Wextra -Wformat-security -Wconversion -Wsign-conversion -Wno-gnu -Wno-gnu-statement-expression)
elseif (${CMAKE_CXX_COMPILER_ID} STREQUAL "MSVC")
    message(STATUS "Setting MSVC flags")
    add_compile_options(/W4 /WX)
elseif (${CMAKE_CXX_COMPILER_ID} STREQUAL "Clang")
    message(STATUS "Setting Clang flags")
    add_compile_options(-Werror -Weverything -Wno-c++98-compat -Wno-c++98-compat-pedantic -Wno-padded -Wno-global-constructors -Wno-exit-time-destructors -Wno-weak-vtables -Wno-documentation -Wno-documentation-unknown-command -Wno-unsafe-buffer-usage)
endif()

if(ENABLE_TESTS)
    include(CTest)
    enable_testing()
    find_package(GTest QUIET)
    if(GTest_FOUND)
        message(STATUS "GTest found, enabling tests")
        include(GoogleTest)
    else()
        message(WARNING "GTest not found, tests will be disabled. Install GTest to enable testing.")
        set(ENABLE_TESTS OFF)
    endif()
endif()

if(ENABLE_SANITIZERS)
    # Common sanitizer flags for better security and debugging
    set(SANITIZER_FLAGS
        -fno-omit-frame-pointer
        -fsanitize=address
        -fsanitize=alignment
        -fsanitize=bool
        -fsanitize=bounds
        -fsanitize=enum
        -fsanitize=float-cast-overflow
        -fsanitize=float-divide-by-zero
        -fsanitize=integer-divide-by-zero
        -fsanitize=leak
        -fsanitize=nonnull-attribute
        -fsanitize=pointer-compare
        -fsanitize=pointer-overflow
        -fsanitize=pointer-subtract
        -fsanitize=return
        -fsanitize=returns-nonnull-attribute
        -fsanitize=shift
        -fsanitize=signed-integer-overflow
        -fsanitize=undefined
        -fsanitize=unreachable
        -fsanitize=vla-bound
        -fsanitize=vptr
        -g
    )

    add_compile_options(${SANITIZER_FLAGS})
    add_link_options(${SANITIZER_FLAGS})

    if (${CMAKE_CXX_COMPILER_ID} STREQUAL "Clang")
        message(STATUS "Setting Clang-specific sanitizer flags")
        add_compile_options(-fsanitize=implicit-conversion -fsanitize=unsigned-integer-overflow)
        add_link_options(-fsanitize=implicit-conversion -fsanitize=unsigned-integer-overflow)
    endif()
endif()

add_library(words_core STATIC)
target_sources(words_core PRIVATE
    src/artificial.cpp
    src/database.cpp
    src/engine.cpp
    src/json.cpp
    src/lexer.cpp
    src/model.cpp
)
target_include_directories(words_core PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
)
target_link_libraries(words_core
    PUBLIC utf8proc::utf8proc
    PRIVATE nlohmann_json::nlohmann_json
)
add_library(WordsWASM_lib ALIAS words_core)
if(EMSCRIPTEN)
    add_executable(WordsWASM
        wasmsrc/main.cpp
    )

    target_link_libraries(WordsWASM
        PUBLIC words_core
        -lembind
    )

    # WebAssembly specific settings
    target_link_options(WordsWASM PRIVATE
        -lembind
        -sWASM=1
        -sMODULARIZE=1
        -sEXPORT_NAME=Module
        -sALLOW_MEMORY_GROWTH=1
        -sENVIRONMENT=web
        -sNO_DISABLE_EXCEPTION_CATCHING=1
        -sEXPORTED_FUNCTIONS=_malloc,_free
        -sEXPORTED_RUNTIME_METHODS=ccall,cwrap,HEAPU8
        --emit-tsd words_wasm.d.ts
    )

    if(ENABLE_WEB_COMPRESSION)
        find_program(BROTLI_EXECUTABLE brotli)
        find_program(GZIP_EXECUTABLE gzip)

        if(BROTLI_EXECUTABLE AND GZIP_EXECUTABLE)
            set(WEB_COMPRESSION_SCRIPT "${CMAKE_CURRENT_BINARY_DIR}/compress_web_assets.cmake")
            configure_file(
                "${CMAKE_CURRENT_SOURCE_DIR}/cmake/compress_web_assets.cmake.in"
                "${WEB_COMPRESSION_SCRIPT}"
                @ONLY
            )

            add_custom_target(WordsWASM_assets ALL
                DEPENDS WordsWASM
                COMMAND "${CMAKE_COMMAND}" -P "${WEB_COMPRESSION_SCRIPT}"
                COMMENT "Generating compressed web assets"
                VERBATIM
            )
        else()
            message(WARNING "brotli/gzip not found; compressed web assets will not be generated")
        endif()
    endif()
else()
    add_executable(words_cli src/main.cpp)
    target_link_libraries(words_cli PRIVATE words_core)
endif()

if(ENABLE_TESTS AND GTest_FOUND)
    add_subdirectory(tests)
endif()
