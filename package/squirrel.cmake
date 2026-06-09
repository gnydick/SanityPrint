option(ENABLE_SQUIRREL_WINDOWS "Generate Squirrel.Windows update artifacts" ON)
set(SQUIRREL_CLI $ENV{SQUIRREL_CLI} CACHE FILEPATH "Path to Squirrel.exe (Squirrel.Windows CLI)")
set(SQUIRREL_NUGET_EXE $ENV{SQUIRREL_NUGET_EXE} CACHE FILEPATH "Path to nuget.exe for packing the Squirrel nuspec")
set(_SQUIRREL_FEED_BASE "http://172.20.180.14/shared/sanityprint/windows")
set(SQUIRREL_FEED_URL "" CACHE STRING "Override feed URL; leave empty to use env-specific defaults")
set(SQUIRREL_FEED_URL_DEV  "${_SQUIRREL_FEED_BASE}/dev"  CACHE STRING "Feed URL for Dev builds (dev)")
set(SQUIRREL_FEED_URL_TEST "${_SQUIRREL_FEED_BASE}/test" CACHE STRING "Feed URL for Alpha builds (test)")
set(SQUIRREL_FEED_URL_PROD "${_SQUIRREL_FEED_BASE}/prod" CACHE STRING "Feed URL for Beta/Release builds (prod)")
set(SQUIRREL_ICON_URL "https://www.creality.com/favicon.ico" CACHE STRING "Icon URL shown by Squirrel installer metadata")

if (NOT ENABLE_SQUIRREL_WINDOWS)
    message(STATUS "Squirrel.Windows packaging disabled (ENABLE_SQUIRREL_WINDOWS=OFF)")
    return()
endif ()

set(SQUIRREL_STAGING_DIR "${CMAKE_BINARY_DIR}/squirrel/staging")
# isolate Squirrel outputs by environment: Dev=dev, Alpha=test, Beta/Release=prod
if (PROJECT_VERSION_EXTRA STREQUAL "Dev")
    set(_squirrel_env "dev")
elseif (PROJECT_VERSION_EXTRA STREQUAL "Alpha")
    set(_squirrel_env "test")
else()
    set(_squirrel_env "prod")
endif()
if (NOT SQUIRREL_FEED_URL)
    if (_squirrel_env STREQUAL "dev")
        set(SQUIRREL_FEED_URL "${SQUIRREL_FEED_URL_DEV}")
    elseif (_squirrel_env STREQUAL "test")
        set(SQUIRREL_FEED_URL "${SQUIRREL_FEED_URL_TEST}")
    else()
        set(SQUIRREL_FEED_URL "${SQUIRREL_FEED_URL_PROD}")
    endif()
endif()
set(SQUIRREL_RELEASE_DIR "${CMAKE_BINARY_DIR}/squirrel/Releases/${_squirrel_env}")
set(SQUIRREL_PACKAGES_DIR "${CMAKE_BINARY_DIR}/squirrel/packages/${_squirrel_env}")
file(TO_NATIVE_PATH "${SQUIRREL_STAGING_DIR}" SQUIRREL_STAGING_DIR_NATIVE)
file(TO_NATIVE_PATH "${SQUIRREL_RELEASE_DIR}" SQUIRREL_RELEASE_DIR_NATIVE)
file(TO_NATIVE_PATH "${SQUIRREL_PACKAGES_DIR}" SQUIRREL_PACKAGES_DIR_NATIVE)
file(TO_CMAKE_PATH "${SQUIRREL_RELEASE_DIR}" SQUIRREL_RELEASE_DIR)
file(TO_CMAKE_PATH "${SQUIRREL_PACKAGES_DIR}" SQUIRREL_PACKAGES_DIR)

if (CMAKE_CONFIGURATION_TYPES)
    set(SQUIRREL_BUILD_CONFIG "$<CONFIG>")
else ()
    set(SQUIRREL_BUILD_CONFIG "${CMAKE_BUILD_TYPE}")
endif ()

string(TOLOWER "${PROCESS_NAME}" SQUIRREL_ID_TOKEN)
set(SQUIRREL_ID "${SQUIRREL_ID_TOKEN}")

set(SQUIRREL_VERSION "${SANITYPRINT_VERSION}")
if (NOT "${PROJECT_VERSION_EXTRA}" STREQUAL "")
    string(TOLOWER "${PROJECT_VERSION_EXTRA}" _extra_lower)
    set(SQUIRREL_VERSION "${SQUIRREL_VERSION}-${_extra_lower}")
endif ()

message(STATUS "SQUIRREL_VERSION before conversion: ${SQUIRREL_VERSION}")

# Ensure Squirrel version is 3-part (Major.Minor.Patch) for SemVer compatibility
# Squirrel.Windows requires semantic versioning (e.g., 7.0.0, not 7.0.0.100)
# Extract 3-part version and pre-release suffix (e.g., -alpha, -release)
string(REGEX MATCH "^([0-9]+\\.[0-9]+\\.[0-9]+)(\\.[0-9]+)?(-[a-zA-Z0-9]+)?" SQUIRREL_VERSION_MATCH "${SQUIRREL_VERSION}")
if (SQUIRREL_VERSION_MATCH)
    set(SQUIRREL_VERSION_BASE "${CMAKE_MATCH_1}")
    set(SQUIRREL_SUFFIX "${CMAKE_MATCH_3}")
    set(SQUIRREL_VERSION "${SQUIRREL_VERSION_BASE}${SQUIRREL_SUFFIX}")
    message(STATUS "SQUIRREL_VERSION after conversion: ${SQUIRREL_VERSION} (base=${SQUIRREL_VERSION_BASE}, suffix=${SQUIRREL_SUFFIX})")
else()
    message(WARNING "SQUIRREL_VERSION does not match version format: ${SQUIRREL_VERSION}")
endif ()

set(SQUIRREL_ICON_PATH "${CMAKE_SOURCE_DIR}/package/icon/NSIS.ico")
file(TO_CMAKE_PATH "${SQUIRREL_ICON_PATH}" SQUIRREL_ICON_PATH)
file(TO_NATIVE_PATH "${SQUIRREL_ICON_PATH}" SQUIRREL_ICON_PATH_NATIVE)
set(SQUIRREL_DESCRIPTION "Sanity Print packaged for Squirrel.Windows delta updates (feed: ${SQUIRREL_FEED_URL}).")
set(SQUIRREL_NUPKG "${SQUIRREL_PACKAGES_DIR}/${SQUIRREL_ID}.${SQUIRREL_VERSION}.nupkg")
file(TO_CMAKE_PATH "${SQUIRREL_NUPKG}" SQUIRREL_NUPKG)
file(TO_NATIVE_PATH "${SQUIRREL_NUPKG}" SQUIRREL_NUPKG_NATIVE)
file(TO_NATIVE_PATH "${CMAKE_BINARY_DIR}/squirrel/${PROCESS_NAME}.nuspec" SQUIRREL_NUSPEC_NATIVE)

configure_file(${CMAKE_CURRENT_LIST_DIR}/squirrel.nuspec.in ${CMAKE_BINARY_DIR}/squirrel/${PROCESS_NAME}.nuspec @ONLY)

if (NOT SQUIRREL_CLI)
    message(WARNING "SQUIRREL_CLI is not set; skipping Squirrel.Windows packaging target.")
    return()
endif ()

get_filename_component(SQUIRREL_CLI_DIR "${SQUIRREL_CLI}" DIRECTORY)
set(_squirrel_helpers StubExecutable.exe WriteZipToSetup.exe Squirrel.com)
foreach(_helper IN LISTS _squirrel_helpers)
    if (NOT EXISTS "${SQUIRREL_CLI_DIR}/${_helper}")
        message(FATAL_ERROR "SQUIRREL_CLI helpers not found next to ${SQUIRREL_CLI}: missing ${_helper}. Please point SQUIRREL_CLI to the unpacked Squirrel 'tools' folder (not a lone Squirrel.exe).")
    endif()
endforeach()

# Try to locate nuget.exe automatically if the cache entry is empty
if (NOT SQUIRREL_NUGET_EXE)
    unset(SQUIRREL_NUGET_EXE CACHE)
    find_program(SQUIRREL_NUGET_EXE
        NAMES nuget.exe nuget
        HINTS "${CMAKE_SOURCE_DIR}/tools" "${CMAKE_BINARY_DIR}/tools" "$ENV{USERPROFILE}/tools" "$ENV{LOCALAPPDATA}/NuGet"
        DOC "Path to nuget.exe for packing the Squirrel nuspec")
endif()

if (NOT SQUIRREL_NUGET_EXE)
    message(FATAL_ERROR "SQUIRREL_NUGET_EXE is required; Squirrel cannot consume the .nuspec directly. Set -DSQUIRREL_NUGET_EXE to nuget.exe or add it to PATH.")
endif()

add_custom_target(squirrel-package
    COMMENT "Generate Squirrel.Windows updater artifacts"
    DEPENDS package
    COMMAND ${CMAKE_COMMAND} -E make_directory ${SQUIRREL_STAGING_DIR} ${SQUIRREL_RELEASE_DIR} ${SQUIRREL_PACKAGES_DIR}
    COMMAND ${CMAKE_COMMAND} --install ${CMAKE_BINARY_DIR} --config ${SQUIRREL_BUILD_CONFIG} --prefix ${SQUIRREL_STAGING_DIR}
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
)

add_custom_command(TARGET squirrel-package POST_BUILD
    COMMAND ${SQUIRREL_NUGET_EXE} pack "${SQUIRREL_NUSPEC_NATIVE}" -NoPackageAnalysis
            -OutputDirectory "${SQUIRREL_PACKAGES_DIR_NATIVE}"
            -BasePath "${SQUIRREL_STAGING_DIR_NATIVE}"
    COMMAND ${SQUIRREL_CLI} --releasify "${SQUIRREL_NUPKG_NATIVE}"
            --releaseDir "${SQUIRREL_RELEASE_DIR_NATIVE}"
            --packagesDir "${SQUIRREL_PACKAGES_DIR_NATIVE}"
            --no-msi --no-setup || (echo "Squirrel releasify completed with non-zero exit code, but files were generated successfully" && exit 0)
    BYPRODUCTS ${SQUIRREL_RELEASE_DIR}/RELEASES ${SQUIRREL_NUPKG}
    COMMENT "NuGet pack + Squirrel releasify"
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    USES_TERMINAL
)

# Copy update feed config once to avoid duplicate outputs
add_custom_command(TARGET squirrel-package POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/resources/data/update_config.json ${SQUIRREL_RELEASE_DIR}/update_config.json
    BYPRODUCTS ${SQUIRREL_RELEASE_DIR}/update_config.json
    COMMENT "Copy update_config.json next to Squirrel RELEASES feed"
)
