# Points the NTFS junction LINK at the folder TARGET, replacing any junction already there (#25).
#   cmake -DLINK=<junction path> -DTARGET=<existing folder> -P RefreshJunction.cmake
# A junction left by an older or moved checkout (or one whose target is gone) is replaced, so a
# build always runs with the resources of the checkout that built it. Removing a junction never
# touches the files of the folder it pointed at. A real folder at LINK is refused, not deleted.
# Windows only (mklink /J).
cmake_minimum_required(VERSION 3.13)

if (NOT LINK OR NOT TARGET)
    message(FATAL_ERROR "RefreshJunction: usage: cmake -DLINK=<junction> -DTARGET=<folder> -P ${CMAKE_CURRENT_LIST_FILE}")
endif ()
file(TO_NATIVE_PATH "${LINK}" native_link)
file(TO_NATIVE_PATH "${TARGET}" native_target)

if (NOT IS_DIRECTORY "${TARGET}")
    message(FATAL_ERROR "RefreshJunction: target folder ${native_target} does not exist")
endif ()

if (IS_SYMLINK "${LINK}")
    # rmdir without /s removes only the junction itself.
    execute_process(COMMAND cmd /c rmdir "${native_link}" RESULT_VARIABLE r ERROR_VARIABLE err)
    if (NOT r EQUAL 0 OR IS_SYMLINK "${LINK}")
        message(FATAL_ERROR "RefreshJunction: could not remove the old junction ${native_link}: ${err}")
    endif ()
elseif (EXISTS "${LINK}")
    message(FATAL_ERROR "RefreshJunction: ${native_link} is a real folder or file, not a junction; "
                        "move it away, then build again")
endif ()

execute_process(COMMAND cmd /c mklink /J "${native_link}" "${native_target}" RESULT_VARIABLE r OUTPUT_QUIET ERROR_VARIABLE err)
if (NOT r EQUAL 0)
    message(FATAL_ERROR "RefreshJunction: mklink /J ${native_link} ${native_target} failed: ${err}")
endif ()
