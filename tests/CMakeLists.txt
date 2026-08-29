add_executable(words_tests
    artificial_test.cpp
    database_test.cpp
    engine_test.cpp
    lexer_test.cpp
)
target_link_libraries(words_tests PRIVATE words_core GTest::GTest GTest::Main)
target_compile_definitions(words_tests PRIVATE
    WORDS_TEST_WWDB="${CMAKE_SOURCE_DIR}/whitakers-words/poc/compact-db/output/words-poc-dense.wwdb"
)
gtest_discover_tests(words_tests)

if(NOT EMSCRIPTEN)
    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(Python3_Interpreter_FOUND)
        add_test(
            NAME words_differential
            COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/differential_test.py"
                --root "${CMAKE_SOURCE_DIR}"
                --cpp "$<TARGET_FILE:words_cli>"
        )
        add_test(
            NAME words_aeneid_corpus
            COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/corpus_differential_test.py"
                --root "${CMAKE_SOURCE_DIR}"
                --cpp "$<TARGET_FILE:words_cli>"
        )
    endif()
endif()
