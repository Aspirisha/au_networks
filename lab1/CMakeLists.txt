cmake_minimum_required(VERSION 3.4)
project(lab1)

set(CMAKE_BUILD_TYPE DEBUG)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11 -DELPP_THREAD_SAFE")

add_subdirectory(client)
add_subdirectory(server)
add_subdirectory(common)
add_subdirectory(test)