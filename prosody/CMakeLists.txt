cmake_minimum_required(VERSION 3.16)
project(pingze LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

add_compile_options(-Wall -Wextra)

add_library(pingze_core
  src/unicode.cpp
  src/phonology.cpp
  src/templates.cpp
  src/checker.cpp)
target_include_directories(pingze_core PUBLIC include)

# A build-time fallback so the binary and tests find the bundled data while
# developing in-tree; installed copies use the runtime search paths in main.cpp.
target_compile_definitions(pingze_core PUBLIC
  PINGZE_SOURCE_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")

add_executable(pingze src/main.cpp)
target_link_libraries(pingze PRIVATE pingze_core)

enable_testing()
add_executable(pingze_tests tests/test_pingze.cpp)
target_link_libraries(pingze_tests PRIVATE pingze_core)
add_test(NAME pingze_tests COMMAND pingze_tests)

install(TARGETS pingze RUNTIME DESTINATION bin)
install(DIRECTORY data/ DESTINATION share/pingze)
