cmake_minimum_required(VERSION 3.20)
project(WX360 VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(wx360 src/main.cpp)
target_include_directories(wx360 PRIVATE include)

if(MSVC)
  target_compile_options(wx360 PRIVATE /W4 /permissive-)
else()
  target_compile_options(wx360 PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Keep the baseline portable: do not pass -mavx, -mavx2 or /arch:AVX2.
if(WIN32)
  target_compile_definitions(wx360 PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
endif()
