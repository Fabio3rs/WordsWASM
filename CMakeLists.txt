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
if(NOT EMSCRIPTEN)
    add_subdirectory(vendor/json SYSTEM)
endif()

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
    src/lexeme.cpp
    src/lexer.cpp
    src/model.cpp
    src/semantics.cpp
)
target_include_directories(words_core PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
)
target_link_libraries(words_core
    PUBLIC utf8proc::utf8proc
)
add_library(WordsWASM_lib ALIAS words_core)

if(NOT EMSCRIPTEN)
    # WHY: JSON is a presentation backend for the CLI and acceptance fixtures,
    # not part of the analysis engine or its WebAssembly build graph.
    add_library(words_json STATIC src/json.cpp)
    target_link_libraries(words_json
        PUBLIC words_core
        PRIVATE nlohmann_json::nlohmann_json
    )
endif()
if(NOT EMSCRIPTEN)
    # The packer is explicit because its output is a release/test fixture, not
    # a prerequisite for consumers that link only the analysis library.
    add_executable(wwdb_poc_pack EXCLUDE_FROM_ALL
        whitakers-words/poc/compact-db/wwdb_poc_pack.cpp
    )
    target_include_directories(wwdb_poc_pack PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
    )
    target_link_libraries(wwdb_poc_pack PRIVATE nlohmann_json::nlohmann_json)
endif()

if(EMSCRIPTEN)
    # Emscripten marks this setting compile+link. The database validator uses
    # an internal exception to construct LoadError; applying it only at link
    # time would let corrupt input escape past the intended catch boundary.
    target_compile_options(words_core PRIVATE
        -sDISABLE_EXCEPTION_CATCHING=0
    )

    add_executable(WordsWASM
        wasmsrc/main.cpp
    )
    set_target_properties(WordsWASM PROPERTIES
        OUTPUT_NAME words_wasm
        SUFFIX ".mjs"
    )

    target_link_libraries(WordsWASM
        PRIVATE words_core
        -lembind
    )
    target_compile_options(WordsWASM PRIVATE
        -sDISABLE_EXCEPTION_CATCHING=0
    )

    # WHY: Embind keeps ordinary analysis calls at the typed language boundary;
    # only the initial immutable database image crosses as a contiguous byte
    # array. There is no public allocator/pointer ABI to keep synchronized.
    target_link_options(WordsWASM PRIVATE
        -lembind
        -sWASM=1
        -sMODULARIZE=1
        -sEXPORT_ES6=1
        -sEXPORT_NAME=createWordsModule
        -sALLOW_MEMORY_GROWTH=1
        -sENVIRONMENT=web,worker,node
        -sFILESYSTEM=0
        -sDISABLE_EXCEPTION_CATCHING=0
        --emit-tsd words_wasm.d.ts
        -flto
    )

    add_custom_target(WordsWASM_browser_wrapper ALL
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/wasmsrc/words-engine.mjs"
            "${CMAKE_CURRENT_BINARY_DIR}/words-engine.mjs"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/wasmsrc/words-engine.d.ts"
            "${CMAKE_CURRENT_BINARY_DIR}/words-engine.d.ts"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/wasmsrc/words-engine.d.mts"
            "${CMAKE_CURRENT_BINARY_DIR}/words-engine.d.mts"
        COMMENT "Copying the browser analysis wrapper"
        VERBATIM
    )
    add_dependencies(WordsWASM_browser_wrapper WordsWASM)

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
                DEPENDS WordsWASM_browser_wrapper
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
    target_link_libraries(words_cli PRIVATE words_json)

    add_subdirectory(parsers_investigation)
endif()

if(ENABLE_TESTS AND GTest_FOUND)
    add_subdirectory(tests)
endif()
