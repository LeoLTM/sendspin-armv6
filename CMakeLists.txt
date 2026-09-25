cmake_minimum_required(VERSION 3.28)
project(sendspin-armv6 VERSION 0.1.9 LANGUAGES CXX)

include(FetchContent)

# Promote Release from the default -O2 to -O3.  Set before FetchContent so
# the flag propagates to sendspin-cpp and its bundled FLAC/Opus decoders.
set(CMAKE_C_FLAGS_RELEASE   "-O3 -DNDEBUG" CACHE STRING "" FORCE)
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG" CACHE STRING "" FORCE)

set(SENDSPIN_ENABLE_PLAYER     ON  CACHE BOOL "" FORCE)
set(SENDSPIN_ENABLE_CONTROLLER OFF CACHE BOOL "" FORCE)
set(SENDSPIN_ENABLE_METADATA   ON  CACHE BOOL "" FORCE)  # track info for the optional OLED display
set(SENDSPIN_ENABLE_COLOR      OFF CACHE BOOL "" FORCE)
set(SENDSPIN_ENABLE_ARTWORK    OFF CACHE BOOL "" FORCE)
set(SENDSPIN_ENABLE_VISUALIZER OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    sendspin-cpp
    GIT_REPOSITORY https://github.com/Sendspin/sendspin-cpp.git
    GIT_TAG        v0.6.1
    GIT_SHALLOW    TRUE
    EXCLUDE_FROM_ALL  # do not build sendspin-cpp's own examples
)
FetchContent_MakeAvailable(sendspin-cpp)

add_executable(sendspin-armv6
    src/main.cpp
    src/alsa_pipe_sink.cpp
    src/config.cpp
    src/display_layout.cpp
    src/framebuffer.cpp
    src/oled_display.cpp
    src/ssd1306.cpp
)

target_link_libraries(sendspin-armv6 PRIVATE sendspin asound pthread)
target_compile_features(sendspin-armv6 PRIVATE cxx_std_20)
target_include_directories(sendspin-armv6 PRIVATE src)
