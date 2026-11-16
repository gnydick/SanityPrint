# Tests of the CMake scripts under cmake/ that run during the build.
if (WIN32)
    add_test(NAME refresh_junction
             COMMAND ${CMAKE_COMMAND}
                     -DSCRIPT=${PROJECT_SOURCE_DIR}/cmake/RefreshJunction.cmake
                     -DWORK=${CMAKE_CURRENT_BINARY_DIR}/work
                     -P ${CMAKE_CURRENT_SOURCE_DIR}/refresh_junction_test.cmake)
endif ()
