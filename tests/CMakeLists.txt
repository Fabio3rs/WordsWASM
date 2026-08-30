add_executable(words_tests
    artificial_test.cpp
    database_test.cpp
    engine_test.cpp
    lexer_test.cpp
)
# The test inspects both JSON contracts directly. words_core intentionally
# keeps its serializer dependency private because no public header exposes it,
# so the test must declare its own compile-time dependency.
target_link_libraries(words_tests PRIVATE
    words_core
    nlohmann_json::nlohmann_json
    GTest::GTest
    GTest::Main
)
target_compile_definitions(words_tests PRIVATE
    WORDS_TEST_WWDB="${CMAKE_SOURCE_DIR}/whitakers-words/poc/compact-db/output/words-poc-dense.wwdb"
    WORDS_TEST_SEARCH_WWDB="${CMAKE_SOURCE_DIR}/whitakers-words/poc/compact-db/output/words-poc-search-only.wwdb"
)
gtest_discover_tests(words_tests)

if(NOT EMSCRIPTEN)
    find_program(NODE_EXECUTABLE node)
    if(NODE_EXECUTABLE)
        add_test(
            NAME words_wasm_wrapper
            COMMAND "${NODE_EXECUTABLE}" --test
                "${CMAKE_CURRENT_SOURCE_DIR}/wasm_wrapper_test.mjs"
        )
    endif()

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
        add_test(
            NAME words_quantity_import
            COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/quantity_import_test.py"
        )
        add_test(
            NAME words_quantity_suggestion
            COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/quantity_suggestion_test.py"
        )
        add_test(
            NAME words_lexical_expansion_audit
            COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/lexical_expansion_test.py"
        )
        add_test(
            NAME words_lexeme_review
            COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/lexeme_review_test.py"
        )
        add_test(
            NAME words_lexeme_decision_validation
            COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/lexeme_decision_validation_test.py"
        )
    endif()
endif()
