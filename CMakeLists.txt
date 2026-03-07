cmake_minimum_required(VERSION 3.18)
project(Newtonia CXX)

set(CMAKE_CXX_STANDARD 11)

# Collect all game source files (everything except the desktop GLUT main).
file(GLOB_RECURSE GAME_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/view/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/weapon/*.cpp"
)

# Exclude the desktop entry point; android_main.cpp is included automatically.
list(FILTER GAME_SOURCES EXCLUDE REGEX ".*glut\\.cpp$")

# SDL2 dependency: build from source if SDL2_SOURCE_DIR is provided (e.g. CI),
# otherwise expect pre-built SDL2 to be found via SDL2_DIR / SDL2_mixer_DIR.
if(DEFINED SDL2_SOURCE_DIR AND DEFINED SDL2_MIXER_SOURCE_DIR)
    add_subdirectory(${SDL2_SOURCE_DIR} ${CMAKE_BINARY_DIR}/SDL2 EXCLUDE_FROM_ALL)
    # Expose SDL2's build dir so SDL2_mixer's find_package(SDL2) can locate it.
    set(SDL2_DIR ${CMAKE_BINARY_DIR}/SDL2 CACHE PATH "" FORCE)
    add_subdirectory(${SDL2_MIXER_SOURCE_DIR} ${CMAKE_BINARY_DIR}/SDL2_mixer EXCLUDE_FROM_ALL)
endif()

add_library(newtonia SHARED ${GAME_SOURCES})

target_include_directories(newtonia PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/view
    ${CMAKE_CURRENT_SOURCE_DIR}/weapon
    # SDL2 headers are picked up from the NDK SDL2 build or via SDL2_DIR.
)

# Android-specific link libraries
find_library(log-lib   log)
find_library(gles2-lib GLESv2)
find_library(egl-lib   EGL)
find_library(android-lib android)

target_link_libraries(newtonia
    SDL2
    SDL2_mixer
    ${gles2-lib}
    ${egl-lib}
    ${android-lib}
    ${log-lib}
)

target_compile_definitions(newtonia PRIVATE __ANDROID__)
