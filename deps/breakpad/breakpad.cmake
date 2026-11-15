set(patch_command git init && ${PATCH_CMD} ${CMAKE_CURRENT_LIST_DIR}/0001-crealityprint-build.patch)
orcaslicer_add_cmake_project(BREAKPAD
  # GIT_REPOSITORY https://github.com/aliyun/aliyun-oss-cpp-sdk.git
  # GIT_TAG v1.9.2
  URL https://github.com/google/breakpad/archive/refs/tags/v2024.02.16.zip
  URL_HASH SHA256=150164d489fe05d86356f2feff96fe8618b0969d77441ebc0c60e21c39de63ef
  PATCH_COMMAND ${patch_command}
  CMAKE_ARGS
    -DINSTALL_HEADERS:BOOL=ON
)
add_dependencies(dep_BREAKPAD dep_CURL)
if (MSVC)
    add_debug_dep(dep_BREAKPAD)
endif ()