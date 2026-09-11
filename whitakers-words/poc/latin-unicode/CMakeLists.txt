add_library(latin_unicode_poc STATIC EXCLUDE_FROM_ALL
    latin_utf8.cpp
)
target_include_directories(latin_unicode_poc PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
)
words_apply_project_options(latin_unicode_poc)

if(ENABLE_TESTS AND GTest_FOUND AND NOT EMSCRIPTEN)
    add_executable(latin_unicode_poc_tests
        latin_utf8_test.cpp
    )
    target_link_libraries(latin_unicode_poc_tests PRIVATE
        latin_unicode_poc
        words_core
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

if(EMSCRIPTEN)
    foreach(size_target IN ITEMS
            latin_unicode_poc_size latin_unicode_poc_into_size
            latin_unicode_utf8proc_size)
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
