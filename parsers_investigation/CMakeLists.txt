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

add_executable(parsers_investigation main.cpp parser.cpp)
target_link_libraries(parsers_investigation PRIVATE
    words_core
    nlohmann_json::nlohmann_json
)
target_compile_definitions(parsers_investigation PRIVATE
    PARSERS_INVESTIGATION_WWDB_PATH="${PARSERS_INVESTIGATION_WWDB_PATH}"
    PARSERS_INVESTIGATION_DATASET_ID="${PARSERS_INVESTIGATION_DATASET_ID}"
    PARSERS_INVESTIGATION_CORPUS_PATH="${PARSERS_INVESTIGATION_CORPUS_PATH}"
    PARSERS_INVESTIGATION_GIT_COMMIT="${PARSERS_INVESTIGATION_GIT_COMMIT}"
    PARSERS_INVESTIGATION_COMPILER="${CMAKE_CXX_COMPILER_ID}"
    PARSERS_INVESTIGATION_COMPILER_VERSION="${CMAKE_CXX_COMPILER_VERSION}"
    PARSERS_INVESTIGATION_BUILD_TYPE="$<CONFIG>"
)

# if(ENABLE_TESTS)
#     add_test(
#         NAME parsers_investigation_self_test
#         COMMAND parsers_investigation --self-test
#     )
# endif()
