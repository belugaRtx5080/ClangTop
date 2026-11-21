cmake_minimum_required(VERSION 3.16)
project(ClangTopPP VERSION 0.3.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

set(CMAKE_CXX_FLAGS_RELEASE "-O2")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra")
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

add_library(clangtop_core STATIC
  src/collectors/sys_kernel_collector.cpp
  src/collectors/cpu_collector.cpp
  src/collectors/power_collector.cpp
  src/collectors/mem_io_net_collector.cpp
  src/collectors/fs_code_scanner.cpp
  src/collectors/kernel_lab.cpp
)
target_include_directories(clangtop_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(clangtop_core PUBLIC Threads::Threads)

add_executable(clangtoppp src/main.cpp)
set_target_properties(clangtoppp PROPERTIES OUTPUT_NAME "ClangTop++")
target_link_libraries(clangtoppp PRIVATE clangtop_core)

add_executable(test_smoke tests/test_smoke.cpp)
target_link_libraries(test_smoke PRIVATE clangtop_core)
