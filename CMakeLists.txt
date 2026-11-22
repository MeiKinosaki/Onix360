cmake_minimum_required(VERSION 3.20)
project(WX360 VERSION 0.3.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(wx360_cpu INTERFACE)
target_include_directories(wx360_cpu INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)

add_executable(wx360 src/main.cpp)
target_link_libraries(wx360 PRIVATE wx360_cpu)

include(CTest)
if(BUILD_TESTING)
  add_executable(wx360_cpu_tests tests/cpu_state_tests.cpp)
  target_link_libraries(wx360_cpu_tests PRIVATE wx360_cpu)
  add_test(NAME xenon_state_tests COMMAND wx360_cpu_tests)\n  add_executable(wx360_interpreter_tests tests/powerpc_interpreter_tests.cpp)\n  target_link_libraries(wx360_interpreter_tests PRIVATE wx360_cpu)\n  add_test(NAME powerpc_interpreter_tests COMMAND wx360_interpreter_tests)
endif()

if(MSVC)
  target_compile_options(wx360 PRIVATE /W4 /permissive-)
  if(BUILD_TESTING)
    target_compile_options(wx360_cpu_tests PRIVATE /W4 /permissive-)\n    target_compile_options(wx360_interpreter_tests PRIVATE /W4 /permissive-)
  endif()
else()
  target_compile_options(wx360 PRIVATE -Wall -Wextra -Wpedantic)
  if(BUILD_TESTING)
    target_compile_options(wx360_cpu_tests PRIVATE -Wall -Wextra -Wpedantic)\n    target_compile_options(wx360_interpreter_tests PRIVATE -Wall -Wextra -Wpedantic)
  endif()
endif()

# Deliberately do not enable AVX/AVX2 compiler targets.
if(WIN32)
  target_compile_definitions(wx360 PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
endif()
