add_executable(sns_visual_qa_tests
  test_visual_qa_suite.cpp
)

target_link_libraries(sns_visual_qa_tests
  PRIVATE
    sns_visual_qa
    Catch2::Catch2WithMain
    sns::compile_options
)

include(CTest)
add_test(NAME sns_visual_qa_tests COMMAND sns_visual_qa_tests)
