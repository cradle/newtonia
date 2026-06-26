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
#   cmake -B xbox/build-desktop -S xbox            # plain Ninja (CI canary)
#   cmake --build xbox/build-desktop --config Release
#
# Xbox Series X|S — DECIDED console path: GLon12 (desktop-GL renderer over SDL's
# WGL backend, the same renderer as Desktop) built for the Scarlett console via
# Ninja + the NDA GXDK (no GDK MSBuild platform — mirrors the Desktop Ninja
# canary, see the VCTargetsPath note below). Needs a GXDK runner:
#
#   cmake -G Ninja -B xbox/build -S xbox -DXBOX_SCARLETT=ON \
#         -DCMAKE_BUILD_TYPE=Release \
#         -DGDK_INCLUDE_DIR=<...\GRDK\...\include> \
#         -DGXDK_INCLUDE_DIR=<...\GXDK\...\gameKit\Include> \
#         -DGXDK_LIB_DIR=<...\GXDK\...\Scarlett\lib> \
#         -DGLON12_LIB=<...\opengl32.lib for the Xbox GLon12 build>
#   cmake --build xbox/build
#   # then: xbox/build_console_package.ps1  (makepkg -> .xvc)
#
# This compiles xbox_main.cpp's mode 2 (_GAMING_DESKTOP + NEWTONIA_GDK_CONSOLE,
# WINAPI_FAMILY_GAMES) — the desktop-GL renderer + SDL_GL present path with
# console-only fullscreen/PLM/safe-area layered on top. GLon12's opengl32.dll +
# libgallium_wgl.dll ship in the package (see xbox/PackagingLayout.xml).
#
# Xbox Series X|S — ABANDONED ANGLE fallback (GLES2 via ANGLE, GDKX MSBuild
# platform). Kept for reference only; not the canonical path:
#
#   cmake -B xbox/build -S xbox -A Gaming.Xbox.Scarlett.x64
#   cmake --build xbox/build --config Release
#
# The GDK MSBuild platform automatically defines:
#   _GAMING_XBOX       (+ _GAMING_XBOX_SCARLETT) for Xbox targets (ANGLE path)
#   _GAMING_DESKTOP    for the Desktop target
# Both are handled by xbox_main.cpp; do not set them manually here. The Ninja
# Scarlett path above instead defines _GAMING_DESKTOP + NEWTONIA_GDK_CONSOLE
# explicitly (see the XBOX_SCARLETT block below).
#
# ANGLE (libEGL / libGLESv2) is used ONLY by the abandoned Xbox ANGLE target and
# is NOT bundled with the GDK.  CI fetches the ANGLE.WindowsStore NuGet package
# and passes -DANGLE_INCLUDE_DIR / -DANGLE_LIB_DIR (see xbox-dev.yml); local
# builds can set the same cache variables, or set GDK_ROOT to enable the search
# fallbacks below.  Both the Desktop target and the decided GLon12 console target
# use the desktop GL renderer over SDL/WGL and need no ANGLE — see the
# renderer-target split below.
# ---------------------------------------------------------------

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Decided Xbox Series console path: GLon12 desktop-GL renderer built for
# Scarlett via Ninja + GXDK (not the ANGLE MSBuild platform). See the header
# comment above and xbox/PORT_PLAN.md Option A. Requires the NDA GXDK, so it is
# OFF by default and only switched on by the GXDK runner / build script.
option(XBOX_SCARLETT "Build the Xbox Series (Scarlett) GLon12 console package target (requires GXDK)" OFF)

# ---------------------------------------------------------------
# Build path: GDK MSBuild platform vs plain MSVC (Ninja)
# ---------------------------------------------------------------
# Two ways to build:
#   * GDK platform : cmake -A Gaming.Desktop.x64  (VS generator)
#   * Plain MSVC   : cmake -G Ninja               (CI canary; see xbox-dev.yml)
#
# CI uses the Ninja path. The VS-generator path needs CMake to discover
# VCTargetsPath via an internally generated probe project that loads the GDK
# platform's WindowsSDK check; under BWOI (no installer, no SDK registry entry)
# that probe is unsolvable on hosted runners. Ninja invokes cl.exe directly —
# no MSBuild platform, no VCTargets, no probe.
#
# The GDK is still fully set up in the Ninja build: GDK_INCLUDE_DIR is passed
# from the BWOI-extracted GDK (…/Microsoft GDK/<ed>/windows/include) so the
# game compiles against the real GDK headers, and _GAMING_DESKTOP is defined
# (the MSBuild platform would otherwise inject it). This is the basis for the
# console target — the Scarlett build is the same structure pointed at the
# xbox/ tree with _GAMING_XBOX and the console libs.
if(CMAKE_GENERATOR_PLATFORM MATCHES "Gaming")
  set(GDK_PLATFORM_BUILD TRUE)
else()
  set(GDK_PLATFORM_BUILD FALSE)
endif()

# ---------------------------------------------------------------
# Renderer target: console (ANGLE/GLES2) vs Desktop (desktop GL / GLon12)
# ---------------------------------------------------------------
# The Xbox Series target (Gaming.Xbox.Scarlett.x64) renders GLES2 through ANGLE.
# Every other configuration — the GDK Desktop platform, or the plain Ninja
# canary — is the Desktop target, which uses the desktop GL 3.3 core renderer
# over SDL's WGL backend (hardware GL, or Mesa GLon12 by dropping its DLLs next
# to the exe; see xbox/GLON12_SPIKE.md). The Desktop target needs neither ANGLE
# nor the GDK, so it builds with just SDL2 + opengl32.
if(CMAKE_GENERATOR_PLATFORM MATCHES "Xbox")
  set(NEWTONIA_XBOX_CONSOLE TRUE)
else()
  set(NEWTONIA_XBOX_CONSOLE FALSE)
endif()

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
    GIT_TAG        release-2.30.9   # pinned: branch tip (SDL2) drifts between CI runs — see issue #287
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
    GIT_TAG        release-2.8.0   # pinned alongside SDL2
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
)
# On the GDK MSBuild platform _GAMING_DESKTOP is injected automatically; the
# plain Ninja build must define it so the Desktop entry point compiles.
if(NOT GDK_PLATFORM_BUILD)
    target_compile_definitions(newtonia PRIVATE _GAMING_DESKTOP)
endif()

# Decided Scarlett console (GLon12): the renderer + present path are exactly the
# _GAMING_DESKTOP path, so we reuse it and add NEWTONIA_GDK_CONSOLE for the
# console-only runtime bits (native-res fullscreen, GDK PLM, TV safe area), plus
# the Game OS API partition so forbidden desktop-only APIs are caught at compile
# time (same partition the xbox-console-smoke workflow uses).
if(XBOX_SCARLETT)
    target_compile_definitions(newtonia PRIVATE
        NEWTONIA_GDK_CONSOLE
        WINAPI_FAMILY=WINAPI_FAMILY_GAMES
    )
    # GXDK (Xbox extensions) headers: D3D12.x, XSuspendResume.h, XTaskQueue.h,
    # xgameplatform umbrella. Provided by the GXDK runner; not in the public GRDK.
    if(DEFINED GXDK_INCLUDE_DIR)
        target_include_directories(newtonia PRIVATE "${GXDK_INCLUDE_DIR}")
    else()
        message(WARNING "XBOX_SCARLETT: GXDK_INCLUDE_DIR not set — XSuspendResume.h/"
                        "XTaskQueue.h (GXDK) will not be found. Pass -DGXDK_INCLUDE_DIR=<...>.")
    endif()
endif()

# SDL_stdinc.h unconditionally defines M_PI, so when _USE_MATH_DEFINES is also
# set the Windows CRT re-defines it.  Suppress the resulting C4005 warning.
target_compile_options(newtonia PRIVATE /wd4005)

# Desktop: use the WINDOWS subsystem so no console window appears.
# /ENTRY:mainCRTStartup keeps the standard CRT startup calling main()
# instead of WinMain(), so SDL_SetMainReady() + main() work unchanged.
# Applies to both the GDK Desktop platform and the plain Ninja build.
if(CMAKE_GENERATOR_PLATFORM MATCHES "Desktop" OR NOT GDK_PLATFORM_BUILD)
    target_link_options(newtonia PRIVATE /SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup)
endif()

# Plain Ninja build: wire in the GDK headers (the MSBuild platform would do
# this) so the game compiles against the real GDK, and link the Win32 libs the
# Desktop present path (GDI StretchDIBits) and SDL rely on. GDK_INCLUDE_DIR is
# passed by CI from the BWOI-extracted GDK.
if(NOT GDK_PLATFORM_BUILD)
    if(DEFINED GDK_INCLUDE_DIR)
        target_include_directories(newtonia PRIVATE "${GDK_INCLUDE_DIR}")
    endif()
    if(XBOX_SCARLETT)
        # Game OS API partition: gdi32/user32 are forbidden (and WINAPI_FAMILY_GAMES
        # hides them). Link the GXDK umbrella (xgameplatform.lib) + Gaming Runtime
        # (xgameruntime.lib) instead of the default Win32 libs.
        if(DEFINED GXDK_LIB_DIR)
            target_link_directories(newtonia PRIVATE "${GXDK_LIB_DIR}")
        else()
            message(WARNING "XBOX_SCARLETT: GXDK_LIB_DIR not set — xgameplatform.lib "
                            "will not be found. Pass -DGXDK_LIB_DIR=<...\\Scarlett\\lib>.")
        endif()
        target_link_libraries(newtonia PRIVATE xgameplatform xgameruntime)
        # TODO(GXDK, dev kit): the canonical Game OS link line is the GDK template's
        # /NODEFAULTLIB + an explicit lib set (xgameplatform, xgameruntime, the
        # static /MT CRT: libcmt/libvcruntime/libucrt, pixevt, d3d12_x). The exact
        # set must be confirmed against the installed GXDK before enabling
        # /NODEFAULTLIB — a bare /NODEFAULTLIB drops the CRT and fails the link.
        # Until then we rely on the default resolution (caught at link on a kit).
    else()
        # Desktop Ninja canary: SDL + the windowed present path use these.
        target_link_libraries(newtonia PRIVATE gdi32 user32)
    endif()
endif()

target_include_directories(newtonia PRIVATE
    ${CMAKE_SOURCE_DIR}/..          # repo root (all .h files)
    ${CMAKE_SOURCE_DIR}/../weapon
    ${CMAKE_SOURCE_DIR}/../view
)

target_link_libraries(newtonia PRIVATE SDL2-static SDL2_mixer)

if(NEWTONIA_XBOX_CONSOLE)
    # ---- Console: ANGLE GLES2 (libEGL / libGLESv2) ----
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
    # ANGLE_LIB_DIR is located at configure time by the CI workflow and passed
    # via -DANGLE_LIB_DIR=<path>; for local VS builds the platform props handle it.
    if(DEFINED ANGLE_LIB_DIR)
        target_link_directories(newtonia PRIVATE "${ANGLE_LIB_DIR}")
    elseif(DEFINED ENV{GDK_ROOT})
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

    target_link_libraries(newtonia PRIVATE libEGL libGLESv2)
elseif(XBOX_SCARLETT)
    # ---- Decided console path: Mesa GLon12 built for the Xbox cross target.
    # GL 1.1 base entry points link against GLon12's opengl32 import lib; GL 2.0+
    # is resolved at runtime via SDL_GL_GetProcAddress (same as Desktop). The
    # matching opengl32.dll + libgallium_wgl.dll ship in the package. Building
    # Mesa's d3d12 driver for Xbox is GXDK-gated — see xbox/PORT_PLAN.md Phase 2. ----
    if(DEFINED GLON12_LIB)
        target_link_libraries(newtonia PRIVATE "${GLON12_LIB}")
    else()
        message(WARNING "XBOX_SCARLETT: GLON12_LIB not set — the Xbox GLon12 opengl32 "
                        "import lib resolves GL 1.1 entry points. Pass -DGLON12_LIB=<...\\opengl32.lib>.")
    endif()
else()
    # ---- Desktop: system OpenGL (GL 1.1 from opengl32; GL 2.0+ resolved at
    # runtime via SDL_GL_GetProcAddress). Resolves to Mesa GLon12 when its
    # opengl32.dll is staged next to the exe. No ANGLE, no GDK required. ----
    target_link_libraries(newtonia PRIVATE opengl32)
endif()

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
