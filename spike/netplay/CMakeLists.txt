# Phase 0 netplay spike — see NETPLAY.md.
#
# Proves the riskiest external of the netplay plan before any product code:
#   * FetchContent MbedTLS v3.6.6 + libdatachannel v0.24.5 (with its
#     libjuice/usrsctp/plog git submodules) configure and build under
#     MSVC + Ninja with the static CRT (/MT) — the exact combination
#     xbox/CMakeLists.txt will use in Phase 2.
#   * The C API (rtc/rtc.h) links from a plain C program and the
#     rtcDataChannelInit/rtcReliability struct supports the
#     unordered + unreliable (maxRetransmits=0) channel we need for INPUT.
#
# Standalone project — NOT included by any game build. Built and run by
# .github/workflows/netplay-spike.yml on windows-latest:
#
#   cmake -G Ninja -B build -S spike/netplay -DCMAKE_BUILD_TYPE=Release
#   cmake --build build
#   build/netplay_spike.exe        # prints SPIKE PASS / SPIKE FAIL

cmake_minimum_required(VERSION 3.21)

# Same CRT policy as xbox/CMakeLists.txt: CMP0091 NEW before project() so
# CMAKE_MSVC_RUNTIME_LIBRARY reaches every FetchContent sub-project.
cmake_policy(SET CMP0091 NEW)

project(netplay_spike C CXX)

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    CACHE STRING "" FORCE)

include(FetchContent)

set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

# MbedTLS first so libdatachannel (USE_MBEDTLS) resolves the MbedTLS::*
# targets from this build instead of find_package'ing the system.
# Pins verified via git ls-remote 2026-07-04 (see NETPLAY.md).
FetchContent_Declare(MbedTLS
    GIT_REPOSITORY https://github.com/Mbed-TLS/mbedtls.git
    GIT_TAG        v3.6.6
    # No GIT_SHALLOW: both projects resolve tags + submodules; shallow
    # clones have broken submodule fetches here before (see NETPLAY.md).
)
set(ENABLE_PROGRAMS OFF CACHE BOOL "" FORCE)
set(ENABLE_TESTING  OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(MbedTLS)

# libdatachannel consumes MbedTLS as the imported-style targets its
# FindMbedTLS.cmake would create (MbedTLS::MbedTLS etc.), but skips the
# find_package entirely when MbedTLS::MbedTLS already exists
# (CMakeLists.txt:428 in v0.24.5). The FetchContent MbedTLS build only
# defines the plain mbedtls/mbedx509/mbedcrypto targets, so alias them —
# without this, its find module searches the system, finds nothing on a
# clean runner, and configuration fails.
if(NOT TARGET MbedTLS::MbedTLS)
  add_library(MbedTLS::MbedTLS    ALIAS mbedtls)
  add_library(MbedTLS::MbedX509   ALIAS mbedx509)
  add_library(MbedTLS::MbedCrypto ALIAS mbedcrypto)
endif()

FetchContent_Declare(libdatachannel
    GIT_REPOSITORY https://github.com/paullouisageneau/libdatachannel.git
    GIT_TAG        v0.24.5
)
set(USE_MBEDTLS  ON  CACHE BOOL "" FORCE)
set(NO_MEDIA     ON  CACHE BOOL "" FORCE)
set(NO_WEBSOCKET ON  CACHE BOOL "" FORCE)
set(NO_EXAMPLES  ON  CACHE BOOL "" FORCE)
set(NO_TESTS     ON  CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(libdatachannel)

add_executable(netplay_spike main.c)
target_link_libraries(netplay_spike LibDataChannel::LibDataChannelStatic)
