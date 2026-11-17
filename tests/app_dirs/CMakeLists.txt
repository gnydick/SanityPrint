get_filename_component(_TEST_NAME ${CMAKE_CURRENT_LIST_DIR} NAME)
add_executable(${_TEST_NAME}_tests ${_TEST_NAME}_tests_main.cpp)
# Header-only helper under test: only the include path, not the libslic3r library.
target_include_directories(${_TEST_NAME}_tests PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_compile_features(${_TEST_NAME}_tests PRIVATE cxx_std_17)
target_link_libraries(${_TEST_NAME}_tests test_common)
add_test(${_TEST_NAME}_tests ${_TEST_NAME}_tests ${CATCH_EXTRA_ARGS})
