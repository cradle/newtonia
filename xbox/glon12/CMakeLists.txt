cmake_minimum_required(VERSION 3.21)

# ---------------------------------------------------------------
# Newtonia GLon12 desktop rendering spike (Phase 2, work-item 3a)
# ---------------------------------------------------------------
# Builds glon12_probe — a standalone SDL2 + GL 3.3 core program that reports
# whether the GL backend behind its WGL context (GLon12 when Mesa's desktop
# opengl32.dll is dropped next to the exe; otherwise the system GL driver)
# covers Newtonia's renderer feature set. See xbox/GLON12_SPIKE.md.
#
# This is intentionally GDKX-free: it is a plain desktop Windows (or Linux/macOS)
# build, no GDK platform, no ANGLE. It exists to de-risk the console GLon12 path
# without a dev kit.
#
#   cmake -B xbox/glon12/build -S xbox/glon12
#   cmake --build xbox/glon12/build --config Release
#
# On Windows with MSVC the static CRT keeps the exe self-contained so it can be
# dropped into a folder next to Mesa's GLon12 DLLs and run anywhere.

project(newtonia_glon12_probe CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

cmake_policy(SET CMP0091 NEW)
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
        CACHE STRING "" FORCE)
endif()

# ---------------------------------------------------------------
# SDL2 — reuse an installed SDL2 if present (find_package), else fetch and
# build it statically (mirrors xbox/CMakeLists.txt's pin).
# ---------------------------------------------------------------
find_package(SDL2 QUIET)

if(NOT SDL2_FOUND)
    include(FetchContent)
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(SDL2
        GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
        GIT_TAG        release-2.30.9   # match xbox/CMakeLists.txt
        GIT_SHALLOW    TRUE
    )
    set(SDL_SHARED            OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC            ON  CACHE BOOL "" FORCE)
    set(SDL_TESTS             OFF CACHE BOOL "" FORCE)
    set(SDL_FORCE_STATIC_VCRT ON  CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(SDL2)
endif()

add_executable(glon12_probe glon12_probe.cpp)

# SDL2 may expose either the SDL2::SDL2 / SDL2::SDL2-static imported targets
# (find_package or recent FetchContent) or the plain SDL2-static target name.
if(TARGET SDL2::SDL2-static)
    target_link_libraries(glon12_probe PRIVATE SDL2::SDL2-static)
elseif(TARGET SDL2-static)
    target_link_libraries(glon12_probe PRIVATE SDL2-static)
elseif(TARGET SDL2::SDL2)
    target_link_libraries(glon12_probe PRIVATE SDL2::SDL2)
else()
    target_link_libraries(glon12_probe PRIVATE ${SDL2_LIBRARIES})
    target_include_directories(glon12_probe PRIVATE ${SDL2_INCLUDE_DIRS})
endif()

# GL types only (entry points are loaded via SDL_GL_GetProcAddress at runtime).
if(WIN32)
    target_link_libraries(glon12_probe PRIVATE opengl32)
elseif(APPLE)
    find_library(OPENGL_FRAMEWORK OpenGL REQUIRED)
    target_link_libraries(glon12_probe PRIVATE ${OPENGL_FRAMEWORK})
else()
    find_package(OpenGL REQUIRED)
    target_link_libraries(glon12_probe PRIVATE OpenGL::GL)
endif()
