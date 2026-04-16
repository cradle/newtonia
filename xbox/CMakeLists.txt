cmake_minimum_required(VERSION 3.21)
project(newtonia CXX)

# ---------------------------------------------------------------
# GDK build for Newtonia — Xbox console and GDK Desktop (Windows PC)
# ---------------------------------------------------------------
#
# Xbox Series X|S (from the repo root):
#
#   set GDK_PATH=C:\Program Files (x86)\Microsoft GDK\241000\GRDK
#   cmake -B xbox/build -S xbox ^
#     -DCMAKE_TOOLCHAIN_FILE="%GDK_PATH%\BuildFiles\CMake\GDKxPlatform.cmake" ^
#     -DGDK_TOOLCHAIN_EDITION=241000 ^
#     -A Gaming.Xbox.Scarlett.x64
#   cmake --build xbox/build --config Release
#
# GDK Desktop — run on a Windows PC for rapid iteration (see xbox-dev.yml):
#
#   cmake -B xbox/build-desktop -S xbox ^
#     -DCMAKE_TOOLCHAIN_FILE="%GDK_PATH%\BuildFiles\CMake\GDKxPlatform.cmake" ^
#     -DGDK_TOOLCHAIN_EDITION=241000 ^
#     -A Gaming.Desktop.x64
#   cmake --build xbox/build-desktop --config Release
#
# The GDK toolchain automatically defines:
#   _GAMING_XBOX       (+ _GAMING_XBOX_SCARLETT) for Xbox targets
#   _GAMING_DESKTOP    for the Desktop target
# Both are handled by xbox_main.cpp; do not set them manually here.
#
# ANGLE (libEGL / libGLESv2) is bundled with the GDK and its import libs are
# automatically on the linker search path once the toolchain is loaded.
# ---------------------------------------------------------------

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

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

FetchContent_Declare(SDL2
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG        SDL2
    GIT_SHALLOW    TRUE
)
set(SDL_SHARED  OFF CACHE BOOL "" FORCE)
set(SDL_STATIC  ON  CACHE BOOL "" FORCE)
set(SDL_TESTS   OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(SDL2)

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

target_compile_definitions(newtonia PRIVATE
    _USE_MATH_DEFINES  # M_PI etc. on MSVC
    # _GAMING_XBOX / _GAMING_DESKTOP are injected by the GDK toolchain;
    # do not set them here or the wrong entry point will be compiled on Desktop.
)

target_include_directories(newtonia PRIVATE
    ${CMAKE_SOURCE_DIR}/..          # repo root (all .h files)
    ${CMAKE_SOURCE_DIR}/../weapon
    ${CMAKE_SOURCE_DIR}/../view
)

# GDK ships ANGLE import libraries (libEGL.lib / libGLESv2.lib) and adds
# their directory to CMAKE_LIBRARY_PATH via the toolchain file, so we can
# link by bare name here.
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
