cmake_minimum_required(VERSION 3.16)
project(sendspin-armv6 VERSION 0.1.0 LANGUAGES CXX)

include(FetchContent)

FetchContent_Declare(
    sendspin-cpp
    GIT_REPOSITORY https://github.com/Sendspin/sendspin-cpp.git
    GIT_TAG        v0.1.0
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(sendspin-cpp)

add_executable(sendspin-armv6
    src/main.cpp
    src/alsa_pipe_sink.cpp
    src/config.cpp
)

target_link_libraries(sendspin-armv6 PRIVATE sendspin)
target_compile_features(sendspin-armv6 PRIVATE cxx_std_20)
target_include_directories(sendspin-armv6 PRIVATE src)
