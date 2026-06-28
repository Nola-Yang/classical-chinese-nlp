cmake_minimum_required(VERSION 3.16)
project(stylo LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
add_compile_options(-Wall -Wextra)

add_library(stylo_core src/text.cpp src/metrics.cpp)
target_include_directories(stylo_core PUBLIC include)

add_executable(stylo src/main.cpp)
target_link_libraries(stylo PRIVATE stylo_core)

enable_testing()
add_executable(stylo_tests tests/test_stylo.cpp)
target_link_libraries(stylo_tests PRIVATE stylo_core)
add_test(NAME stylo_tests COMMAND stylo_tests)

install(TARGETS stylo RUNTIME DESTINATION bin)

add_executable(stylo-advanced src/advanced.cpp)
target_link_libraries(stylo-advanced PRIVATE stylo_core)
install(TARGETS stylo-advanced RUNTIME DESTINATION bin)
