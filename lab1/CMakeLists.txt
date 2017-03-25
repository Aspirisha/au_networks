cmake_minimum_required(VERSION 3.4)
project(lab1)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")

add_subdirectory(client)
add_subdirectory(server)
add_subdirectory(common)
add_subdirectory(test)

#install(TARGETS server client32 client64 DESTINATION bin/)