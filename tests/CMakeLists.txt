add_executable(WordsWASM_tests test.cpp)
target_link_libraries(WordsWASM_tests WordsWASM_lib GTest::GTest GTest::Main)
gtest_discover_tests(WordsWASM_tests)
