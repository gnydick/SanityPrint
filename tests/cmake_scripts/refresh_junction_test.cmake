# Tests cmake/RefreshJunction.cmake (#25).
# Run: cmake -DSCRIPT=<repo>/cmake/RefreshJunction.cmake -DWORK=<scratch dir> -P refresh_junction_test.cmake
# Windows only: the script makes NTFS junctions with mklink /J.
cmake_minimum_required(VERSION 3.13)

if (NOT SCRIPT OR NOT WORK)
    message(FATAL_ERROR "usage: cmake -DSCRIPT=<RefreshJunction.cmake> -DWORK=<scratch dir> -P ${CMAKE_CURRENT_LIST_FILE}")
endif ()
file(TO_CMAKE_PATH "${WORK}" WORK)
file(TO_CMAKE_PATH "${SCRIPT}" SCRIPT)

set_property(GLOBAL PROPERTY rj_failures 0)
set_property(GLOBAL PROPERTY rj_total 0)

function(record ok msg)
    get_property(f GLOBAL PROPERTY rj_failures)
    get_property(t GLOBAL PROPERTY rj_total)
    math(EXPR t "${t} + 1")
    if (ok)
        message(STATUS "ok: ${msg}")
    else ()
        math(EXPR f "${f} + 1")
        message(STATUS "FAILED: ${msg}")
    endif ()
    set_property(GLOBAL PROPERTY rj_failures ${f})
    set_property(GLOBAL PROPERTY rj_total ${t})
endfunction()

function(expect_exit_zero code msg)
    if (code EQUAL 0)
        record(TRUE "${msg}")
    else ()
        record(FALSE "${msg} (exit ${code})")
    endif ()
endfunction()

function(expect_exit_nonzero code msg)
    if (code EQUAL 0)
        record(FALSE "${msg} (exit 0)")
    else ()
        record(TRUE "${msg}")
    endif ()
endfunction()

function(expect_exists path msg)
    if (EXISTS "${path}")
        record(TRUE "${msg}")
    else ()
        record(FALSE "${msg} (missing: ${path})")
    endif ()
endfunction()

function(expect_missing path msg)
    if (EXISTS "${path}")
        record(FALSE "${msg} (present: ${path})")
    else ()
        record(TRUE "${msg}")
    endif ()
endfunction()

function(run_script link target out_code)
    execute_process(COMMAND "${CMAKE_COMMAND}" "-DLINK=${link}" "-DTARGET=${target}" -P "${SCRIPT}"
                    RESULT_VARIABLE code OUTPUT_QUIET ERROR_QUIET)
    set(${out_code} "${code}" PARENT_SCOPE)
endfunction()

function(make_junction link target)
    file(TO_NATIVE_PATH "${link}" nl)
    file(TO_NATIVE_PATH "${target}" nt)
    execute_process(COMMAND cmd /c mklink /J "${nl}" "${nt}" OUTPUT_QUIET RESULT_VARIABLE r)
    if (NOT r EQUAL 0)
        message(FATAL_ERROR "fixture: mklink /J ${nl} ${nt} failed (${r})")
    endif ()
endfunction()

function(remove_junction link)
    file(TO_NATIVE_PATH "${link}" nl)
    execute_process(COMMAND cmd /c rmdir "${nl}" OUTPUT_QUIET ERROR_QUIET)
endfunction()

# Fixture: two checkouts' resources folders, each with its own marker file.
set(W "${WORK}/refresh_junction")
remove_junction("${W}/link")
file(REMOVE_RECURSE "${W}")
file(MAKE_DIRECTORY "${W}/old_checkout/resources" "${W}/new_checkout/resources")
file(WRITE "${W}/old_checkout/resources/old.marker" "old")
file(WRITE "${W}/new_checkout/resources/new.marker" "new")
set(LINK "${W}/link")
set(NEW "${W}/new_checkout/resources")
set(OLD "${W}/old_checkout/resources")

# Positive control: the fixture's own junction is visible to EXISTS through the link.
make_junction("${LINK}" "${OLD}")
expect_exists("${LINK}/old.marker" "control: a fixture junction is seen through the link")
remove_junction("${LINK}")
expect_missing("${LINK}" "control: rmdir removes a fixture junction")

# 1. No link yet: the script makes one to the target.
run_script("${LINK}" "${NEW}" code)
expect_exit_zero("${code}" "no link: script exits 0")
expect_exists("${LINK}/new.marker" "no link: link reaches the target")

# 2. Link to another checkout (the stale case): replaced; the old target's files are kept.
remove_junction("${LINK}")
make_junction("${LINK}" "${OLD}")
run_script("${LINK}" "${NEW}" code)
expect_exit_zero("${code}" "stale link: script exits 0")
expect_exists("${LINK}/new.marker" "stale link: link reaches the new target")
expect_missing("${LINK}/old.marker" "stale link: link no longer reaches the old target")
expect_exists("${OLD}/old.marker" "stale link: old target's files are not deleted")

# 3. Link to a folder that no longer exists (the I:\ case): replaced.
remove_junction("${LINK}")
file(MAKE_DIRECTORY "${W}/gone")
make_junction("${LINK}" "${W}/gone")
file(REMOVE_RECURSE "${W}/gone")
run_script("${LINK}" "${NEW}" code)
expect_exit_zero("${code}" "dead link: script exits 0")
expect_exists("${LINK}/new.marker" "dead link: link reaches the new target")

# 4. Already correct: still works after a second run.
run_script("${LINK}" "${NEW}" code)
expect_exit_zero("${code}" "correct link: script exits 0")
expect_exists("${LINK}/new.marker" "correct link: link still reaches the target")

# 5. A real folder (not a link) at the link path: refused; its files are untouched.
remove_junction("${LINK}")
file(MAKE_DIRECTORY "${LINK}")
file(WRITE "${LINK}/user.file" "keep")
run_script("${LINK}" "${NEW}" code)
expect_exit_nonzero("${code}" "real folder: script refuses")
expect_exists("${LINK}/user.file" "real folder: its files are untouched")

file(REMOVE_RECURSE "${W}")
get_property(f GLOBAL PROPERTY rj_failures)
get_property(t GLOBAL PROPERTY rj_total)
message(STATUS "refresh_junction_test: ${f} of ${t} checks failed")
if (f GREATER 0)
    message(FATAL_ERROR "refresh_junction_test failed")
endif ()
