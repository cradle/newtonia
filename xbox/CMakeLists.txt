cmake_minimum_required(VERSION 3.21)

# CMP0091 must be set before project() so that CMAKE_MSVC_RUNTIME_LIBRARY
# is honoured by all targets (including FetchContent sub-projects).
# Without NEW the old /MD flags from the GDK platform props take precedence
# and produce MSVCP140_APP.dll / VCRUNTIME140_APP.dll runtime dependencies.
cmake_policy(SET CMP0091 NEW)

project(newtonia CXX)

# ---------------------------------------------------------------
# GDK build for Newtonia — Xbox console and GDK Desktop (Windows PC)
# ---------------------------------------------------------------
#
# GDK Desktop — run on a Windows PC for rapid iteration (see xbox-dev.yml):
#
#   cmake -B xbox/build-desktop -S xbox -A Gaming.Desktop.x64
#   cmake --build xbox/build-desktop --config Release
#
# Xbox Series X|S:
#
#   cmake -B xbox/build -S xbox -A Gaming.Xbox.Scarlett.x64
#   cmake --build xbox/build --config Release
#
# No -DCMAKE_TOOLCHAIN_FILE is needed in GDK 2510+: the installer registers
# Gaming.Desktop.x64 / Gaming.Xbox.Scarlett.x64 as VS2022 MSBuild platforms.
#
# The GDK MSBuild platform automatically defines:
#   _GAMING_XBOX       (+ _GAMING_XBOX_SCARLETT) for Xbox targets
#   _GAMING_DESKTOP    for the Desktop target
# Both are handled by xbox_main.cpp; do not set them manually here.
#
# ANGLE (libEGL / libGLESv2) is bundled with the GDK and its import libs are
# automatically on the linker search path once the platform is loaded.
# ---------------------------------------------------------------

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---------------------------------------------------------------
# Static MSVC runtime — avoids VCRUNTIME140_APP.dll / MSVCP140_APP.dll
# ---------------------------------------------------------------
# The Gaming.Desktop.x64 MSBuild platform normally links the app-sandbox
# CRT variant (_APP.dll), which is not part of the standard Visual C++
# Redistributable and is therefore absent on machines without VS installed.
# Using the static runtime (/MT) embeds the CRT directly in the binary.
# This must be set before FetchContent so SDL2/SDL2_mixer use the same CRT.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    CACHE STRING "" FORCE)

# ---------------------------------------------------------------
# Sources — collect from root, weapon/, and view/, then exclude
# entry points that belong to other platforms.
# ---------------------------------------------------------------
file(GLOB ROOT_SRCS   "${CMAKE_SOURCE_DIR}/../*.cpp")
file(GLOB WEAPON_SRCS "${CMAKE_SOURCE_DIR}/../weapon/*.cpp")
file(GLOB VIEW_SRCS   "${CMAKE_SOURCE_DIR}/../view/*.cpp")

set(ALL_SRCS ${ROOT_SRCS} ${WEAPON_SRCS} ${VIEW_SRCS})

# Exclude platform mains guarded by other preprocessor macros.
list(FILTER ALL_SRCS EXCLUDE REGEX "(android|web)_main\\.cpp$")
# glut.cpp uses FreeGLUT which is not available on Xbox.
list(FILTER ALL_SRCS EXCLUDE REGEX "glut\\.cpp$")
# iOS Objective-C++ file is not part of this build.
list(FILTER ALL_SRCS EXCLUDE REGEX "macos_window\\.mm$")

# ---------------------------------------------------------------
# SDL2 — fetched and compiled with the GDK toolchain so it picks
# up the correct platform abstraction layer automatically.
# ---------------------------------------------------------------
include(FetchContent)

# Force static builds for both SDL2 and SDL2_mixer so their
# INTERFACE_SDL2_SHARED properties agree and CMake doesn't error out.
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

FetchContent_Declare(SDL2
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG        SDL2
    GIT_SHALLOW    TRUE
)
set(SDL_SHARED           OFF CACHE BOOL "" FORCE)
set(SDL_STATIC           ON  CACHE BOOL "" FORCE)
set(SDL_TESTS            OFF CACHE BOOL "" FORCE)
# Belt-and-suspenders: SDL2 has its own CRT flag independent of
# CMAKE_MSVC_RUNTIME_LIBRARY; set both to guarantee /MT throughout.
set(SDL_FORCE_STATIC_VCRT ON  CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(SDL2)

# The Gaming.Desktop.x64 MSBuild platform defines __GDK__ for every
# compilation unit, including SDL2's.  SDL2's source then activates GDK
# code paths in SDL_dynapi.c (jump-table entries) and SDL_windowsevents.c
# (GDK_DispatchTaskQueue call), but SDL2's own CMake never adds SDL_gdk.cpp
# because it doesn't detect GDK through our VS2022-platform approach.
# Result: LNK2019 on SDL_GDKxxx_REAL symbols.
#
# Fix: undefine __GDK__ for SDL2's own object files via MSVC /U so SDL2
# uses its standard Win32 backend.  GDK Desktop is Win32-compatible, and
# our xbox_main.cpp handles all GDK PLM lifecycle events directly.
target_compile_options(SDL2-static PRIVATE /U__GDK__)

# SDL2_mixer: WAV + MP3 only (mirrors every other platform build).
FetchContent_Declare(SDL2_mixer
    GIT_REPOSITORY https://github.com/libsdl-org/SDL_mixer.git
    GIT_TAG        SDL2
    GIT_SHALLOW    TRUE
)
set(SDL2MIXER_WAVPACK OFF CACHE BOOL "" FORCE)
set(SDL2MIXER_FLAC    OFF CACHE BOOL "" FORCE)
set(SDL2MIXER_OGG     OFF CACHE BOOL "" FORCE)
set(SDL2MIXER_OPUS    OFF CACHE BOOL "" FORCE)
set(SDL2MIXER_MOD     OFF CACHE BOOL "" FORCE)
set(SDL2MIXER_MIDI    OFF CACHE BOOL "" FORCE)
set(SDL2MIXER_MP3     ON  CACHE BOOL "" FORCE)
set(SDL2MIXER_WAVE    ON  CACHE BOOL "" FORCE)
set(SDL2MIXER_SAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(SDL2_mixer)

# ---------------------------------------------------------------
# Executable
# ---------------------------------------------------------------
add_executable(newtonia ${ALL_SRCS})

# SDL2 GDK _REAL stubs — see sdl_gdk_stubs.cpp for full rationale.
target_sources(newtonia PRIVATE "${CMAKE_SOURCE_DIR}/sdl_gdk_stubs.cpp")

target_compile_definitions(newtonia PRIVATE
    _USE_MATH_DEFINES  # M_PI etc. for files that include <math.h> before SDL headers
    # _GAMING_XBOX / _GAMING_DESKTOP are injected by the GDK toolchain;
    # do not set them here or the wrong entry point will be compiled on Desktop.
)

# SDL_stdinc.h unconditionally defines M_PI, so when _USE_MATH_DEFINES is also
# set the Windows CRT re-defines it.  Suppress the resulting C4005 warning.
target_compile_options(newtonia PRIVATE /wd4005)

target_include_directories(newtonia PRIVATE
    ${CMAKE_SOURCE_DIR}/..          # repo root (all .h files)
    ${CMAKE_SOURCE_DIR}/../weapon
    ${CMAKE_SOURCE_DIR}/../view
)

# ANGLE EGL/GLES2 headers — needed for the manual EGL context in xbox_main.cpp.
# ANGLE_INCLUDE_DIR is the directory containing EGL/ and GLES2/ subdirectories;
# set by the CI workflow from the GDK or ANGLE.WindowsStore NuGet package.
if(DEFINED ANGLE_INCLUDE_DIR)
    target_include_directories(newtonia PRIVATE "${ANGLE_INCLUDE_DIR}")
elseif(DEFINED ENV{GDK_ROOT})
    file(GLOB_RECURSE _egl_candidates "$ENV{GDK_ROOT}/gameKit/Include/egl.h")
    if(_egl_candidates)
        list(GET _egl_candidates 0 _egl_h)
        get_filename_component(_egl_dir "${_egl_h}" DIRECTORY)
        get_filename_component(_angle_inc "${_egl_dir}" DIRECTORY)
        target_include_directories(newtonia PRIVATE "${_angle_inc}")
    endif()
endif()

# ANGLE import libraries (libEGL.lib / libGLESv2.lib).
# When using -A Gaming.Desktop.x64 / -A Gaming.Xbox.Scarlett.x64 without a
# CMake toolchain file, the GDK MSBuild platform props add the ANGLE lib path
# as an MSBuild AdditionalLibraryDirectories entry that CMake does not
# automatically forward to the raw linker inputs it emits.
# ANGLE_LIB_DIR is located at configure time by the CI workflow and passed
# via -DANGLE_LIB_DIR=<path>; for local VS builds the platform props handle
# it automatically.
if(DEFINED ANGLE_LIB_DIR)
    target_link_directories(newtonia PRIVATE "${ANGLE_LIB_DIR}")
elseif(DEFINED ENV{GDK_ROOT})
    # Local fallback: search the GDK gameKit lib tree for libEGL.lib.
    file(TO_CMAKE_PATH "$ENV{GDK_ROOT}/gameKit/Lib" _gkit_lib)
    file(GLOB_RECURSE _egl_candidates "${_gkit_lib}/libEGL.lib")
    if(_egl_candidates)
        list(GET _egl_candidates 0 _egl_lib)
        get_filename_component(_angle_lib_dir "${_egl_lib}" DIRECTORY)
        message(STATUS "ANGLE lib dir (auto): ${_angle_lib_dir}")
        target_link_directories(newtonia PRIVATE "${_angle_lib_dir}")
    else()
        message(WARNING "libEGL.lib not found under ${_gkit_lib} — pass -DANGLE_LIB_DIR=<path>")
    endif()
endif()

target_link_libraries(newtonia PRIVATE
    SDL2-static
    SDL2_mixer
    libEGL
    libGLESv2
)

# ---------------------------------------------------------------
# Post-build: copy audio assets next to the executable so the
# game can locate them with relative paths at runtime.
# ---------------------------------------------------------------
add_custom_command(TARGET newtonia POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/../audio"
        "$<TARGET_FILE_DIR:newtonia>/audio"
    COMMENT "Copying audio assets"
)
