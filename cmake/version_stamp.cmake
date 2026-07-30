# Build-time version stamp (cmake -P), shared by the root (Android) and
# xbox CMake builds. Writes OUT (a one-line header defining
# NEWTONIA_VERSION_STRING) only when the value changes, so the objects that
# include it rebuild exactly when the stamp moves — the CMake equivalent of
# the Makefile's version.stamp rule.
#
# Running at BUILD time (not configure) is the point: the configure-time
# describe this replaces went stale whenever git moved without touching the
# files CMAKE_CONFIGURE_DEPENDS happened to watch — creating a tag (which
# touches neither HEAD nor the branch ref, and is exactly the moment the
# stamp matters), `git pack-refs` deleting the watched loose ref file so
# later commits went unnoticed, and worktrees where .git is a file and
# nothing was watched at all.
#
# Inputs: -DOUT=<header path> -DSRC_DIR=<repo root> [-DPINNED=<value>]
# Resolution order matches the Makefile: pinned -D, then the environment,
# then git describe; unresolvable writes no define and replay.h's "dev"
# fallback applies.
set(v "${PINNED}")
if(NOT v AND DEFINED ENV{NEWTONIA_VERSION} AND NOT "$ENV{NEWTONIA_VERSION}" STREQUAL "")
    set(v "$ENV{NEWTONIA_VERSION}")
endif()
if(NOT v)
    execute_process(COMMAND git describe --tags --abbrev=7 --dirty=+ --always
                    WORKING_DIRECTORY "${SRC_DIR}"
                    OUTPUT_VARIABLE v
                    OUTPUT_STRIP_TRAILING_WHITESPACE
                    ERROR_QUIET)
endif()
if(v)
    set(content "#define NEWTONIA_VERSION_STRING \"${v}\"\n")
else()
    set(content "// version unresolvable - replay.h falls back to \"dev\"\n")
endif()
set(old "")
if(EXISTS "${OUT}")
    file(READ "${OUT}" old)
endif()
if(NOT old STREQUAL content)
    file(WRITE "${OUT}" "${content}")
endif()
