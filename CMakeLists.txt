

cmake_minimum_required(VERSION 3.14)

project(udp_highrate_cpp LANGUAGES CXX)
 
set(CMAKE_CXX_STANDARD 17)

set(CMAKE_CXX_STANDARD_REQUIRED ON)
 
option(BUILD_TESTING "Build tests" ON)

option(ENABLE_COVERAGE "Enable coverage flags" OFF)
 
if(ENABLE_COVERAGE)

  message(STATUS "Coverage enabled")

  add_compile_options(-O0 -g --coverage)

  add_link_options(--coverage)

else()

  add_compile_options(-O3 -march=native -DNDEBUG)

endif()
 
include_directories(${CMAKE_SOURCE_DIR}/include)
 
add_library(udp_lib

    src/socket.cpp

    src/stats.cpp

    src/metrics_http.cpp

    src/server.cpp

    src/client.cpp

)

target_include_directories(udp_lib PUBLIC include)
 
add_executable(udp_server src/main_server.cpp)

target_link_libraries(udp_server udp_lib)
 
add_executable(udp_client src/main_client.cpp)

target_link_libraries(udp_client udp_lib)
 
if(BUILD_TESTING)

  enable_testing()

  include(FetchContent)

  FetchContent_Declare(googletest

    URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip

  )

  # If no internet, allow fallback to system if present; otherwise tests won't build.

  set(FETCHCONTENT_QUIET FALSE)

  FetchContent_MakeAvailable(googletest)

  add_subdirectory(tests)

endif()

 