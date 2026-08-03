# Idempotently apply a patch to a FetchContent-populated source tree
# (LEADERBOARD.md S1 — patches/libdatachannel-ws-ca-cert.patch).
#
#   cmake -DPATCH=<abs path> -DREPO=<abs path> -P cmake/apply_patch.cmake
#
# FetchContent's PATCH_COMMAND re-runs whenever the populate step does, so a
# bare `git apply` fails the build the second time round with "patch does not
# apply". Reverse-checking first tells us it is already in, and a script keeps
# this portable — the Xbox build configures under MSVC with no shell to run a
# `... || ...` one-liner in.

if(NOT DEFINED PATCH OR NOT DEFINED REPO)
    message(FATAL_ERROR "apply_patch.cmake needs -DPATCH= and -DREPO=")
endif()
if(NOT EXISTS "${PATCH}")
    message(FATAL_ERROR "patch not found: ${PATCH}")
endif()

find_package(Git QUIET REQUIRED)

# Already applied? (`--reverse --check` succeeds only when it is.)
execute_process(
    COMMAND "${GIT_EXECUTABLE}" apply --reverse --check "${PATCH}"
    WORKING_DIRECTORY "${REPO}"
    RESULT_VARIABLE reversible
    OUTPUT_QUIET ERROR_QUIET)
if(reversible EQUAL 0)
    message(STATUS "patch already applied: ${PATCH}")
    return()
endif()

execute_process(
    COMMAND "${GIT_EXECUTABLE}" apply "${PATCH}"
    WORKING_DIRECTORY "${REPO}"
    RESULT_VARIABLE applied
    ERROR_VARIABLE err)
if(NOT applied EQUAL 0)
    # Never fall through quietly: an unpatched libdatachannel has no
    # caCertificatePemFile on rtcWsConfiguration, so the game would fail to
    # compile anyway — but say why here rather than 200 lines later.
    message(FATAL_ERROR "failed to apply ${PATCH} in ${REPO}: ${err}")
endif()
message(STATUS "applied patch: ${PATCH}")
