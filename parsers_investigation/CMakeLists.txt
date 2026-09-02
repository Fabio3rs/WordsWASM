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

add_executable(parsers_investigation main.cpp)
target_link_libraries(parsers_investigation PRIVATE words_core)
target_compile_definitions(parsers_investigation PRIVATE
    PARSERS_INVESTIGATION_WWDB_PATH="${PARSERS_INVESTIGATION_WWDB_PATH}"
    PARSERS_INVESTIGATION_DATASET_ID="${PARSERS_INVESTIGATION_DATASET_ID}"
)
