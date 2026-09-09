cmake_minimum_required(VERSION 3.14)

# Let targets select their warning level without CMake injecting MSVC /W3.
if(POLICY CMP0092)
    cmake_policy(SET CMP0092 NEW)
endif()

project(WordsWASM C CXX)

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND
   CMAKE_CXX_COMPILER_VERSION VERSION_LESS 14)
    message(FATAL_ERROR "WordsWASM requires GCC 14 or newer")
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND
       CMAKE_CXX_COMPILER_VERSION VERSION_LESS 19)
    message(FATAL_ERROR "WordsWASM requires LLVM Clang 19 or newer")
endif()

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
option(ENABLE_STATIC_CLI "Statically link the native CLI where the platform supports it" OFF)
option(FETCH_GTEST "Download GoogleTest when tests are enabled and it is not installed" OFF)

if(ENABLE_STATIC_CLI AND ENABLE_SANITIZERS)
    message(FATAL_ERROR
        "ENABLE_STATIC_CLI and ENABLE_SANITIZERS require separate build trees"
    )
endif()

# Alpine's GCC toolchain defines _FORTIFY_SOURCE by default. Undefine the
# toolchain-provided value before selecting ours to avoid a redefinition
# warning becoming an error under -Werror. Keep this Linux-only: neither
# Apple platforms nor MinGW use glibc's fortify implementation.
add_library(words_project_warnings INTERFACE)
if(UNIX AND NOT APPLE AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(words_project_warnings INTERFACE
        -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=3
    )
endif()

add_subdirectory(vendor/utf8proc SYSTEM)
if(NOT EMSCRIPTEN)
    add_subdirectory(vendor/json SYSTEM)
endif()

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    message(STATUS "Setting G++ flags")
    target_compile_options(words_project_warnings INTERFACE
        -Wall -Werror -Wextra -Wformat-security -Wconversion -Wsign-conversion
    )
elseif(MSVC)
    message(STATUS "Setting MSVC flags")
    target_compile_options(words_project_warnings INTERFACE /W4 /WX /utf-8)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(STATUS "Setting Clang flags")
    target_compile_options(words_project_warnings INTERFACE
        -Werror -Weverything
        -Wno-c++98-compat
        -Wno-c++98-compat-pedantic
        -Wno-padded
        -Wno-global-constructors
        -Wno-exit-time-destructors
        -Wno-weak-vtables
        -Wno-documentation
        -Wno-documentation-unknown-command
        -Wno-unsafe-buffer-usage
    )
endif()

if(ENABLE_TESTS)
    include(CTest)
    enable_testing()
    find_package(GTest QUIET)

    if(NOT GTest_FOUND AND FETCH_GTEST)
        include(FetchContent)
        set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
        if(MSVC)
            # Keep GoogleTest on the same MSVC runtime selected by the parent.
            set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
        endif()
        FetchContent_Declare(
            googletest
            GIT_REPOSITORY https://github.com/google/googletest.git
            GIT_TAG v1.17.0
            GIT_SHALLOW TRUE
        )
        FetchContent_MakeAvailable(googletest)

        if(TARGET gtest AND NOT TARGET GTest::GTest)
            add_library(GTest::GTest ALIAS gtest)
        endif()
        if(TARGET gtest_main AND NOT TARGET GTest::Main)
            add_library(GTest::Main ALIAS gtest_main)
        endif()
        if(TARGET GTest::GTest AND TARGET GTest::Main)
            set(GTest_FOUND TRUE)
        endif()
    endif()

    if(GTest_FOUND)
        message(STATUS "GTest found, enabling tests")
        include(GoogleTest)
    else()
        message(WARNING "GTest not found, tests will be disabled. Install GTest to enable testing.")
        set(ENABLE_TESTS OFF)
    endif()
endif()

add_library(words_project_sanitizers INTERFACE)
if(ENABLE_SANITIZERS)
    if(MSVC)
        target_compile_options(words_project_sanitizers INTERFACE
            /fsanitize=address /Zi
        )
        target_link_options(words_project_sanitizers INTERFACE
            /fsanitize=address /INCREMENTAL:NO
        )
    elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND NOT WIN32)
        target_compile_options(words_project_sanitizers INTERFACE
            -fno-omit-frame-pointer
            -fsanitize=address,undefined,leak,float-divide-by-zero
            -g
        )
        target_link_options(words_project_sanitizers INTERFACE
            -fno-omit-frame-pointer
            -fsanitize=address,undefined,leak,float-divide-by-zero
        )
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        set(WORDS_CLANG_SANITIZERS
            -fsanitize=address,undefined,implicit-conversion,unsigned-integer-overflow,float-divide-by-zero
        )
        target_compile_options(words_project_sanitizers INTERFACE
            -fno-omit-frame-pointer ${WORDS_CLANG_SANITIZERS} -g
        )
        target_link_options(words_project_sanitizers INTERFACE
            -fno-omit-frame-pointer ${WORDS_CLANG_SANITIZERS}
        )
    else()
        message(WARNING
            "Sanitizers are not configured for ${CMAKE_CXX_COMPILER_ID}; disabling them"
        )
        set(ENABLE_SANITIZERS OFF)
    endif()
endif()

if(MSVC AND ENABLE_SANITIZERS)
    # MSVC's annotated STL requires every object in the final executable to
    # agree on ASan instrumentation. Instrument fetched GoogleTest directly
    # so its install(EXPORT) does not acquire a dependency on a project target.
    foreach(gtest_target IN ITEMS gtest gtest_main gmock gmock_main)
        if(TARGET ${gtest_target})
            target_compile_options(${gtest_target} PRIVATE
                /fsanitize=address /Zi
            )
        endif()
    endforeach()
endif()

function(words_apply_project_options target_name)
    target_link_libraries(${target_name} PRIVATE words_project_warnings)
    if(ENABLE_SANITIZERS)
        target_link_libraries(${target_name} PRIVATE words_project_sanitizers)
    endif()
endfunction()

add_library(words_core STATIC)
target_sources(words_core PRIVATE
    src/artificial.cpp
    src/database.cpp
    src/engine.cpp
    src/lexeme.cpp
    src/lexer.cpp
    src/model.cpp
    src/projection.cpp
    src/semantics.cpp
)
target_include_directories(words_core PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
)
target_link_libraries(words_core
    PUBLIC utf8proc::utf8proc
)
words_apply_project_options(words_core)
add_library(WordsWASM_lib ALIAS words_core)

if(NOT EMSCRIPTEN)
    # WHY: JSON is a presentation backend for the CLI and acceptance fixtures,
    # not part of the analysis engine or its WebAssembly build graph.
    add_library(words_json STATIC src/json.cpp)
    target_link_libraries(words_json
        PUBLIC words_core
        PRIVATE nlohmann_json::nlohmann_json
    )
    words_apply_project_options(words_json)
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
    words_apply_project_options(wwdb_poc_pack)
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
    words_apply_project_options(WordsWASM)
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
    words_apply_project_options(words_cli)

    if(ENABLE_STATIC_CLI)
        if(BUILD_SHARED_LIBS)
            message(FATAL_ERROR
                "ENABLE_STATIC_CLI requires BUILD_SHARED_LIBS=OFF"
            )
        elseif(APPLE)
            message(STATUS
                "macOS CLI uses static project libraries and dynamic system libraries"
            )
        elseif(MSVC)
            message(FATAL_ERROR
                "ENABLE_STATIC_CLI on Windows requires a MinGW toolchain"
            )
        elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
            target_link_options(words_cli PRIVATE -static)
            if(WIN32)
                target_link_options(words_cli PRIVATE
                    -static-libgcc -static-libstdc++
                )
            endif()
        else()
            message(FATAL_ERROR
                "ENABLE_STATIC_CLI is unsupported with ${CMAKE_CXX_COMPILER_ID}"
            )
        endif()
    endif()

    add_subdirectory(parsers_investigation)
endif()

if(ENABLE_TESTS AND GTest_FOUND)
    add_subdirectory(tests)
endif()
