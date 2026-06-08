get_filename_component(_TEST_NAME ${CMAKE_CURRENT_LIST_DIR} NAME)
add_executable(${_TEST_NAME}_tests 
    ${_TEST_NAME}_tests.cpp
	test_import_3mf.cpp
	test_import_obj.cpp
	test_import_stl.cpp
	)

# NOTE: engine has no test_hollowing.cpp (this was copy-pasted from tests/libslic3r), so the
# OpenVDB-gated target_sources is removed; it referenced a non-existent file and broke generate.

# Match the release lib too (assimp-vc143-mt), not only the debug one (-mtd), so Release configures.
find_library(LIB_ASSIMP NAMES assimp-vc143-mt assimp-vc143-mtd assimp)

target_link_libraries(${_TEST_NAME}_tests test_common libslic3r OpenSSL::SSL OpenSSL::Crypto Bcrypt ${LIB_ASSIMP})
set_property(TARGET ${_TEST_NAME}_tests PROPERTY FOLDER "tests")

if (WIN32)
    if ("${CMAKE_BUILD_TYPE}" STREQUAL "Debug")
        crealityprint_copy_dlls(COPY_DLLS "Debug" "d" "")
    else()
        crealityprint_copy_dlls(COPY_DLLS "Release" "" "")
    endif()
endif()

# catch_discover_tests(${_TEST_NAME}_tests TEST_PREFIX "${_TEST_NAME}: ")
add_test(${_TEST_NAME}_tests ${_TEST_NAME}_tests ${CATCH_EXTRA_ARGS})
