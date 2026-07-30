cmake_minimum_required(VERSION 3.18)
project(Newtonia CXX)

set(CMAKE_CXX_STANDARD 11)

# 16 KB page-size support (Google Play requirement for 16 KB-page devices).
# NDK r28+ aligns ELF LOAD segments to 16 KB by default, but set the linker
# flags explicitly so the requirement stays visible in the build and survives
# an accidental NDK downgrade (r27 and lower align to 4 KB otherwise). Applied
# through the shared-linker flags BEFORE the SDL2 / SDL2_mixer subdirectories
# so every .so we ship (libnewtonia, libSDL2, libSDL2_mixer) carries it.
if(ANDROID)
    string(APPEND CMAKE_SHARED_LINKER_FLAGS
           " -Wl,-z,max-page-size=16384 -Wl,-z,common-page-size=16384")
endif()

# Collect all game source files (everything except the desktop GLUT main).
#
# CONFIGURE_DEPENDS makes the build re-check these patterns on every build
# instead of only at configure time. Without it the source list is frozen
# into the CMake cache the first time a build directory is configured, so a
# NEWLY ADDED .cpp is silently left out of an EXISTING local build tree and
# only shows up as an undefined symbol at link — e.g.
# `undefined symbol: MenuSelect::draw_row(...)` after menu_select.cpp landed,
# against an android/app/.cxx cache configured before it existed. CI never
# hits this (a fresh checkout configures from scratch every run), so it is
# purely a local-rebuild trap, and `gradlew clean` does NOT clear it —
# android/app/.cxx survives, which is where the stale list lives.
# Costs a directory stat per build; supported since CMake 3.12, well under
# the 3.18 minimum above and the 3.22.1 android/app/build.gradle pins.
file(GLOB_RECURSE GAME_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/view/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/weapon/*.cpp"
)

# Exclude the desktop entry point; android_main.cpp is included automatically.
list(FILTER GAME_SOURCES EXCLUDE REGEX ".*glut\\.cpp$")
# Exclude CMake-generated compiler-identification files that end up inside
# the source tree when Gradle runs CMake from android/app/.cxx/.
list(FILTER GAME_SOURCES EXCLUDE REGEX ".*/CMakeFiles/.*")
list(FILTER GAME_SOURCES EXCLUDE REGEX ".*\\.cxx/.*")
# Exclude the Steamworks SDK a developer may have unzipped at ./sdk/ for
# local `make steam` builds (gitignored, so CI never sees it): the recursive
# glob above would otherwise sweep Valve's sample sources (e.g. glmgr, which
# includes macOS-only <OpenGL/OpenGL.h>) into the Android build.
list(FILTER GAME_SOURCES EXCLUDE REGEX ".*/sdk/.*")

# SDL2 dependency: build from source if SDL2_SOURCE_DIR is provided (e.g. CI),
# otherwise expect pre-built SDL2 to be found via SDL2_DIR / SDL2_mixer_DIR.
if(DEFINED SDL2_SOURCE_DIR AND DEFINED SDL2_MIXER_SOURCE_DIR)
    set(SDL_OPENSLCES ON CACHE BOOL "" FORCE)   # fallback on API < 26
    set(SDL_AAUDIO    ON CACHE BOOL "" FORCE)   # low-latency path on API 26+
    add_subdirectory(${SDL2_SOURCE_DIR} ${CMAKE_BINARY_DIR}/SDL2 EXCLUDE_FROM_ALL)
    # Expose SDL2's build dir so SDL2_mixer's find_package(SDL2) can locate it.
    set(SDL2_DIR ${CMAKE_BINARY_DIR}/SDL2 CACHE PATH "" FORCE)

    # Disable optional SDL2_mixer formats — the game only initialises the
    # mixer for future use and does not load any audio files yet.
    set(SDL2MIXER_VORBIS  ""    CACHE STRING "" FORCE)
    set(SDL2MIXER_WAVPACK OFF   CACHE BOOL   "" FORCE)
    set(SDL2MIXER_FLAC    OFF   CACHE BOOL   "" FORCE)
    set(SDL2MIXER_OPUS    OFF   CACHE BOOL   "" FORCE)
    set(SDL2MIXER_MOD     OFF   CACHE BOOL   "" FORCE)
    set(SDL2MIXER_MIDI    OFF   CACHE BOOL   "" FORCE)
    set(SDL2MIXER_MP3     OFF   CACHE BOOL   "" FORCE)

    add_subdirectory(${SDL2_MIXER_SOURCE_DIR} ${CMAKE_BINARY_DIR}/SDL2_mixer EXCLUDE_FROM_ALL)
endif()

# ---------------------------------------------------------------
# Netplay backend (M3-3) — MbedTLS + libdatachannel via FetchContent,
# transplanted from xbox/CMakeLists.txt (pins per NETPLAY.md). The NDK
# toolchain cross-compiles both for every ABI Gradle asks for.
# ---------------------------------------------------------------
option(NEWTONIA_NET "Enable the online netplay backend (libdatachannel)" ON)

if(NEWTONIA_NET)
    include(FetchContent)
    # Skipping install rules sidesteps libdatachannel's install(EXPORT)
    # failing because the in-tree mbedtls target is in no export set.
    set(CMAKE_SKIP_INSTALL_RULES ON)

    # libdatachannel's MbedTLS DTLS transport uses the DTLS-SRTP API even
    # with NO_MEDIA; MbedTLS's default config compiles that API out. The
    # user config defines MBEDTLS_SSL_DTLS_SRTP for library and consumers.
    add_compile_definitions(
        "MBEDTLS_USER_CONFIG_FILE=\"${CMAKE_CURRENT_SOURCE_DIR}/xbox/mbedtls_user_config.h\"")

    FetchContent_Declare(MbedTLS
        GIT_REPOSITORY https://github.com/Mbed-TLS/mbedtls.git
        GIT_TAG        v3.6.6
    )
    set(ENABLE_PROGRAMS OFF CACHE BOOL "" FORCE)
    set(ENABLE_TESTING  OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(MbedTLS)

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
    set(NO_WEBSOCKET OFF CACHE BOOL "" FORCE)  # room-code signaling
    set(NO_EXAMPLES  ON  CACHE BOOL "" FORCE)
    set(NO_TESTS     ON  CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(libdatachannel)
endif()

add_library(newtonia SHARED ${GAME_SOURCES})

target_include_directories(newtonia PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/view
    ${CMAKE_CURRENT_SOURCE_DIR}/weapon
    # SDL2 headers are picked up from the NDK SDL2 build or via SDL2_DIR.
)

# Android-specific link libraries
find_library(log-lib      log)
find_library(gles2-lib    GLESv2)
find_library(egl-lib      EGL)
find_library(android-lib  android)
find_library(openslES-lib OpenSLES)

target_link_libraries(newtonia
    SDL2
    SDL2_mixer
    ${gles2-lib}
    ${egl-lib}
    ${android-lib}
    ${log-lib}
    ${openslES-lib}
)

# PLAY_GAMES_BUILD enables the Play Games achievements backend
# (play_games_achievements.cpp + PlayGamesAchievements.java); this CMake file
# only builds the Android library, so it is always on here.
#
# NEWTONIA_NET_IDENTITY_BACKEND / NEWTONIA_NET_VERIFY_BACKEND route the netplay
# peer-identity seam (net_identity.cpp) to the Play Games backend
# (play_games_identity.cpp + PlayGamesIdentity.java, NETPLAY.md V2): the player
# display name for the badge and the single-use server auth code the signal
# worker attests with. Both TUs guard on __ANDROID__ && PLAY_GAMES_BUILD, so
# they are empty everywhere else and these defines only matter here.
target_compile_definitions(newtonia PRIVATE __ANDROID__ PLAY_GAMES_BUILD
    NEWTONIA_NET_IDENTITY_BACKEND NEWTONIA_NET_VERIFY_BACKEND)

# Version stamped into replay headers (Replay::Header::game_version, 23 chars
# max) so leaderboard seasons can bucket runs by release — REPLAY.md R4. Same
# resolution order as the Makefile: -DNEWTONIA_VERSION, then the environment
# (how deploy-android.yml passes the tag through Gradle), then git describe.
# Unresolvable leaves replay.h's "dev" default.
if(NOT NEWTONIA_VERSION)
    if(DEFINED ENV{NEWTONIA_VERSION})
        set(NEWTONIA_VERSION "$ENV{NEWTONIA_VERSION}")
    else()
        execute_process(COMMAND git describe --tags --dirty --always
                        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
                        OUTPUT_VARIABLE NEWTONIA_VERSION
                        OUTPUT_STRIP_TRAILING_WHITESPACE
                        ERROR_QUIET)
    endif()
endif()
if(NEWTONIA_VERSION)
    message(STATUS "Newtonia version stamp: ${NEWTONIA_VERSION}")
    target_compile_definitions(newtonia PRIVATE
        NEWTONIA_VERSION_STRING="${NEWTONIA_VERSION}")
endif()

if(NEWTONIA_NET)
    # Activates net_transport_rtc.cpp / net_signal_rtc.cpp (empty TUs
    # everywhere else) and net_available(); see net_transport.h.
    target_compile_definitions(newtonia PRIVATE NEWTONIA_NET_RTC)
    target_link_libraries(newtonia LibDataChannel::LibDataChannelStatic)
endif()
