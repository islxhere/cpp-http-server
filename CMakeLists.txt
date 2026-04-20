cmake_minimum_required(VERSION 3.16)
project(HttpServer)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_BUILD_TYPE Debug)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -g")

# 源文件
file(GLOB_RECURSE CORE_SRCS src/core/*.cpp)
file(GLOB_RECURSE HTTP_SRCS src/http/*.cpp)
file(GLOB_RECURSE UTILS_SRCS src/utils/*.cpp)

add_executable(http_server
    src/main.cpp
    ${CORE_SRCS}
    ${HTTP_SRCS}
    ${UTILS_SRCS}
)

target_include_directories(http_server PRIVATE src)
target_link_libraries(http_server pthread)

# 测试
enable_testing()
add_subdirectory(build/_deps/googletest-src build/_deps/googletest-build EXCLUDE_FROM_ALL)

file(GLOB TEST_SRCS tests/*.cpp)
add_executable(run_tests ${TEST_SRCS} ${CORE_SRCS} ${HTTP_SRCS} ${UTILS_SRCS})
target_include_directories(run_tests PRIVATE src)
target_link_libraries(run_tests GTest::gtest_main pthread)
add_test(NAME all_tests COMMAND run_tests)
