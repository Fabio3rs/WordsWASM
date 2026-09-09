set(PARSERS_INVESTIGATION_WWDB_PATH
    "${PROJECT_SOURCE_DIR}/dist/words-web/words-full.wwdb"
    CACHE FILEPATH
    "Full WWDB image loaded by the native parsers investigation example"
)

set(PARSERS_INVESTIGATION_DATASET_ID
    "sha256:99f23fa3d2a962d2e325b192f064454046c78954af0fb20b4856b0624a288d05"
    CACHE STRING
    "Dataset identifier reported by the parsers investigation example"
)

set(PARSERS_INVESTIGATION_CORPUS_PATH
    "${CMAKE_CURRENT_SOURCE_DIR}/corpus/agreement_fixtures.json"
    CACHE FILEPATH
    "Structured corpus used by the parser investigation"
)

set(PARSERS_INVESTIGATION_MARKOV_SUPPLEMENT_PATH
    "${CMAKE_CURRENT_SOURCE_DIR}/corpus/common_phrases.tsv"
    CACHE FILEPATH
    "Preferred-lemma silver corpus used by the Markov investigation"
)

set(PARSERS_INVESTIGATION_MARKOV_ATTESTED_PATH
    "${CMAKE_CURRENT_SOURCE_DIR}/corpus/attested_gold_fixtures.json"
    CACHE FILEPATH
    "Attested editorial and treebank morphology-gold corpus used by Markov"
)

execute_process(
    COMMAND git rev-parse HEAD
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
    OUTPUT_VARIABLE PARSERS_INVESTIGATION_GIT_COMMIT
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
if(NOT PARSERS_INVESTIGATION_GIT_COMMIT)
    set(PARSERS_INVESTIGATION_GIT_COMMIT "unknown")
endif()

add_library(parsers_investigation_lib STATIC parser.cpp)
target_link_libraries(parsers_investigation_lib PUBLIC
    words_core
    nlohmann_json::nlohmann_json
)
words_apply_project_options(parsers_investigation_lib)
target_compile_definitions(parsers_investigation_lib PUBLIC
    PARSERS_INVESTIGATION_WWDB_PATH="${PARSERS_INVESTIGATION_WWDB_PATH}"
    PARSERS_INVESTIGATION_DATASET_ID="${PARSERS_INVESTIGATION_DATASET_ID}"
    PARSERS_INVESTIGATION_CORPUS_PATH="${PARSERS_INVESTIGATION_CORPUS_PATH}"
    PARSERS_INVESTIGATION_MARKOV_SUPPLEMENT_PATH="${PARSERS_INVESTIGATION_MARKOV_SUPPLEMENT_PATH}"
    PARSERS_INVESTIGATION_MARKOV_ATTESTED_PATH="${PARSERS_INVESTIGATION_MARKOV_ATTESTED_PATH}"
    PARSERS_INVESTIGATION_GIT_COMMIT="${PARSERS_INVESTIGATION_GIT_COMMIT}"
    PARSERS_INVESTIGATION_COMPILER="${CMAKE_CXX_COMPILER_ID}"
    PARSERS_INVESTIGATION_COMPILER_VERSION="${CMAKE_CXX_COMPILER_VERSION}"
    PARSERS_INVESTIGATION_BUILD_TYPE="$<CONFIG>"
)

add_executable(parsers_investigation main.cpp)
target_link_libraries(parsers_investigation PRIVATE parsers_investigation_lib)
words_apply_project_options(parsers_investigation)

add_executable(markov_parser_ranker markov_ranker.cpp)
target_link_libraries(markov_parser_ranker PRIVATE parsers_investigation_lib)
words_apply_project_options(markov_parser_ranker)

add_executable(dependency_markov_ranker dependency_markov_ranker.cpp)
target_link_libraries(dependency_markov_ranker PRIVATE parsers_investigation_lib)
words_apply_project_options(dependency_markov_ranker)

add_executable(markov_model_test markov_model_test.cpp)
target_link_libraries(markov_model_test PRIVATE parsers_investigation_lib)
words_apply_project_options(markov_model_test)

option(PARSERS_INVESTIGATION_REGISTER_TESTS
    "Register exploratory parser and Markov programs with CTest"
    OFF
)

if(ENABLE_TESTS AND PARSERS_INVESTIGATION_REGISTER_TESTS)
    add_test(NAME parsers_markov_model_test COMMAND markov_model_test)
    add_test(NAME parsers_markov_ranker_smoke COMMAND markov_parser_ranker)
    add_test(
        NAME parsers_markov_hierarchical_backoff_smoke
        COMMAND markov_parser_ranker
                --evaluation-tier attested
                --smoothing hierarchical-backoff
                --backoff-strength 1.0
    )
    add_test(
        NAME parsers_markov_attested_in_sample_smoke
        COMMAND markov_parser_ranker
                --evaluation-tier attested
                --evaluation-policy in-sample
    )
    add_test(
        NAME parsers_markov_treebank_in_sample_smoke
        COMMAND markov_parser_ranker
                --evaluation-tier treebank
                --evaluation-policy in-sample
    )
    add_test(
        NAME parsers_markov_shuffled_control_smoke
        COMMAND markov_parser_ranker
                --evaluation-tier attested
                --training-control shuffle-within-sequence
                --shuffle-seed 20260906
    )
    add_test(
        NAME parsers_markov_counterfactual_control_smoke
        COMMAND markov_parser_ranker
                --evaluation-tier attested
                --training-control counterfactual-analysis
    )
    add_test(
        NAME parsers_markov_exposure_curve_smoke
        COMMAND markov_parser_ranker
                --evaluation-tier attested
                --evaluation-policy exposure-curve
                --exposure-multipliers 0,1
    )
endif()

# if(ENABLE_TESTS)
#     add_test(
#         NAME parsers_investigation_self_test
#         COMMAND parsers_investigation --self-test
#     )
# endif()
