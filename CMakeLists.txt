cmake_minimum_required(VERSION 3.16)
project(youtube_cloud LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(youtube_cloud_core
    src/core/common.cpp
    src/core/encoder.cpp
    src/core/decoder.cpp
)

target_include_directories(youtube_cloud_core PUBLIC include)
target_compile_features(youtube_cloud_core PUBLIC cxx_std_20)

add_executable(youtube_cloud src/cli/main.cpp)
target_link_libraries(youtube_cloud PRIVATE youtube_cloud_core)

# Future Qt integration can link youtube_cloud_core into a separate GUI target.
