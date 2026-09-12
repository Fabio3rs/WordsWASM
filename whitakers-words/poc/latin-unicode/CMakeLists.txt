add_library(latin_unicode_poc STATIC EXCLUDE_FROM_ALL
    latin_utf8.cpp
)
target_include_directories(latin_unicode_poc PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
)
words_apply_project_options(latin_unicode_poc)

add_library(unicode_backend_compact_poc STATIC EXCLUDE_FROM_ALL
    unicode_backend_compact.cpp
    generated/compact_words_category.cpp
)
target_include_directories(unicode_backend_compact_poc PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/generated"
)
target_link_libraries(unicode_backend_compact_poc PUBLIC latin_unicode_poc)
words_apply_project_options(unicode_backend_compact_poc)

add_library(unicode_backend_full_poc STATIC EXCLUDE_FROM_ALL
    unicode_backend_full.cpp
)
target_include_directories(unicode_backend_full_poc PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/generated"
)
target_link_libraries(unicode_backend_full_poc PUBLIC utf8proc::utf8proc)
words_apply_project_options(unicode_backend_full_poc)

if(ENABLE_TESTS AND GTest_FOUND AND NOT EMSCRIPTEN)
    add_executable(latin_unicode_poc_tests
        latin_utf8_test.cpp
    )
    target_link_libraries(latin_unicode_poc_tests PRIVATE
        latin_unicode_poc
        words_core
        utf8proc::utf8proc
        GTest::GTest
        GTest::Main
    )
    target_compile_definitions(latin_unicode_poc_tests PRIVATE
        WORDS_LATIN_POC_CORPUS="${CMAKE_CURRENT_SOURCE_DIR}/latin_library_corpus.tsv"
    )
    words_apply_project_options(latin_unicode_poc_tests)
    gtest_discover_tests(latin_unicode_poc_tests
        DISCOVERY_MODE PRE_TEST
        PROPERTIES LABELS latin_unicode_poc
    )

    add_executable(unicode_backend_poc_tests
        unicode_backend_test.cpp
    )
    target_link_libraries(unicode_backend_poc_tests PRIVATE
        unicode_backend_compact_poc
        unicode_backend_full_poc
        words_core
        GTest::GTest
        GTest::Main
    )
    words_apply_project_options(unicode_backend_poc_tests)
    gtest_discover_tests(unicode_backend_poc_tests
        DISCOVERY_MODE PRE_TEST
        PROPERTIES LABELS unicode_backend_poc
    )
endif()

add_executable(latin_unicode_poc_size EXCLUDE_FROM_ALL
    latin_utf8_size_main.cpp
)
target_link_libraries(latin_unicode_poc_size PRIVATE latin_unicode_poc)
words_apply_project_options(latin_unicode_poc_size)

add_executable(latin_unicode_poc_into_size EXCLUDE_FROM_ALL
    latin_utf8_size_main.cpp
)
target_compile_definitions(latin_unicode_poc_into_size PRIVATE
    LATIN_UNICODE_SIZE_INTO=1
)
target_link_libraries(latin_unicode_poc_into_size PRIVATE latin_unicode_poc)
words_apply_project_options(latin_unicode_poc_into_size)

add_executable(latin_unicode_utf8proc_size EXCLUDE_FROM_ALL
    latin_utf8_size_main.cpp
)
target_compile_definitions(latin_unicode_utf8proc_size PRIVATE
    LATIN_UNICODE_SIZE_UTF8PROC=1
)
target_link_libraries(latin_unicode_utf8proc_size PRIVATE utf8proc::utf8proc)
words_apply_project_options(latin_unicode_utf8proc_size)

set(WORDS_POC_UNICODE_BACKEND "AUTO" CACHE STRING
    "Backend for the selected Unicode research target: AUTO, FULL, or COMPACT"
)
set_property(CACHE WORDS_POC_UNICODE_BACKEND PROPERTY STRINGS
    AUTO FULL COMPACT
)
string(TOUPPER "${WORDS_POC_UNICODE_BACKEND}" WORDS_POC_UNICODE_BACKEND_NORMALIZED)
if(WORDS_POC_UNICODE_BACKEND_NORMALIZED STREQUAL "AUTO")
    if(EMSCRIPTEN)
        set(WORDS_POC_UNICODE_BACKEND_RESOLVED "COMPACT")
    else()
        set(WORDS_POC_UNICODE_BACKEND_RESOLVED "FULL")
    endif()
elseif(WORDS_POC_UNICODE_BACKEND_NORMALIZED MATCHES "^(FULL|COMPACT)$")
    set(WORDS_POC_UNICODE_BACKEND_RESOLVED
        "${WORDS_POC_UNICODE_BACKEND_NORMALIZED}"
    )
else()
    message(FATAL_ERROR
        "WORDS_POC_UNICODE_BACKEND must be AUTO, FULL, or COMPACT"
    )
endif()

function(add_unicode_backend_size_target target_name backend_name)
    add_executable(${target_name} EXCLUDE_FROM_ALL
        unicode_backend_size_main.cpp
    )
    if(backend_name STREQUAL "COMPACT")
        target_compile_definitions(${target_name} PRIVATE
            WORDS_POC_BACKEND_COMPACT=1
        )
        target_link_libraries(${target_name} PRIVATE
            unicode_backend_compact_poc
        )
    else()
        target_compile_definitions(${target_name} PRIVATE
            WORDS_POC_BACKEND_FULL=1
        )
        target_link_libraries(${target_name} PRIVATE
            unicode_backend_full_poc
        )
    endif()
    words_apply_project_options(${target_name})
endfunction()

add_unicode_backend_size_target(unicode_backend_compact_size COMPACT)
add_unicode_backend_size_target(unicode_backend_full_size FULL)
add_unicode_backend_size_target(unicode_backend_selected_size
    "${WORDS_POC_UNICODE_BACKEND_RESOLVED}"
)

if(EMSCRIPTEN)
    foreach(size_target IN ITEMS
            latin_unicode_poc_size latin_unicode_poc_into_size
            latin_unicode_utf8proc_size
            unicode_backend_compact_size unicode_backend_full_size
            unicode_backend_selected_size)
        set_target_properties(${size_target} PROPERTIES SUFFIX ".mjs")
        target_compile_options(${size_target} PRIVATE -Oz -flto)
        target_link_options(${size_target} PRIVATE
            -Oz
            -flto
            -sWASM=1
            -sFILESYSTEM=0
            -sENVIRONMENT=node
        )
    endforeach()
else()
    add_executable(latin_unicode_poc_benchmark EXCLUDE_FROM_ALL
        latin_utf8_benchmark.cpp
    )
    target_compile_definitions(latin_unicode_poc_benchmark PRIVATE
        WORDS_LATIN_POC_ASCII_CORPUS="${CMAKE_SOURCE_DIR}/whitakers-words/test/01_aeneid/input.txt"
    )
    target_link_libraries(latin_unicode_poc_benchmark PRIVATE
        latin_unicode_poc
        words_core
    )
    words_apply_project_options(latin_unicode_poc_benchmark)

    add_executable(unicode_backend_poc_benchmark EXCLUDE_FROM_ALL
        unicode_backend_benchmark.cpp
    )
    target_compile_definitions(unicode_backend_poc_benchmark PRIVATE
        WORDS_LATIN_POC_ASCII_CORPUS="${CMAKE_SOURCE_DIR}/whitakers-words/test/01_aeneid/input.txt"
    )
    target_link_libraries(unicode_backend_poc_benchmark PRIVATE
        unicode_backend_compact_poc
        unicode_backend_full_poc
        words_core
    )
    words_apply_project_options(unicode_backend_poc_benchmark)

    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        add_executable(latin_unicode_poc_fuzz EXCLUDE_FROM_ALL
            latin_utf8_fuzz.cpp
        )
        target_link_libraries(latin_unicode_poc_fuzz PRIVATE
            latin_unicode_poc
            words_core
        )
        target_compile_options(latin_unicode_poc_fuzz PRIVATE
            -fsanitize=fuzzer
        )
        target_link_options(latin_unicode_poc_fuzz PRIVATE
            -fsanitize=fuzzer
        )
        words_apply_project_options(latin_unicode_poc_fuzz)
    endif()
endif()
