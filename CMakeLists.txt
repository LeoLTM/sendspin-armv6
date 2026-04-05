cmake_minimum_required(VERSION 3.28)
project(sendspin-armv6 VERSION 0.1.0 LANGUAGES CXX)

include(FetchContent)

# Promote Release from the default -O2 to -O3.  Set before FetchContent so
# the flag propagates to sendspin-cpp and its bundled FLAC/Opus decoders.
set(CMAKE_C_FLAGS_RELEASE   "-O3 -DNDEBUG" CACHE STRING "" FORCE)
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG" CACHE STRING "" FORCE)

FetchContent_Declare(
    sendspin-cpp
    GIT_REPOSITORY https://github.com/Sendspin/sendspin-cpp.git
    GIT_TAG        v0.1.0
    GIT_SHALLOW    TRUE
    EXCLUDE_FROM_ALL  # do not build sendspin-cpp's own examples
)
FetchContent_MakeAvailable(sendspin-cpp)

add_executable(sendspin-armv6
    src/main.cpp
    src/alsa_pipe_sink.cpp
    src/config.cpp
)

target_link_libraries(sendspin-armv6 PRIVATE sendspin asound pthread)
target_compile_features(sendspin-armv6 PRIVATE cxx_std_20)
target_include_directories(sendspin-armv6 PRIVATE src)
