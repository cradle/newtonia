cmake_minimum_required(VERSION 3.20)
cmake_policy(SET CMP0091 NEW)
project(net_spike C CXX)

# Mirror the game's runtime setup: static CRT, static libs (xbox/CMakeLists.txt does the same)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

include(FetchContent)

FetchContent_Declare(MbedTLS
    GIT_REPOSITORY https://github.com/Mbed-TLS/mbedtls.git
    GIT_TAG        v3.6.6
)
set(ENABLE_PROGRAMS OFF CACHE BOOL "" FORCE)
set(ENABLE_TESTING  OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(MbedTLS)

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

add_executable(net_spike main.c)
set_property(TARGET net_spike PROPERTY C_STANDARD 11)
target_link_libraries(net_spike PRIVATE LibDataChannel::LibDataChannelStatic)
