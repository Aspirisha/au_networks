find_package(Boost COMPONENTS filesystem system)

add_library(libserver server.cpp)
set_target_properties(libserver PROPERTIES COMPILE_FLAGS "-m64" LINK_FLAGS "-m64")

target_include_directories(libserver PUBLIC ../common ${Boost_INCLUDE_DIRS})
target_link_libraries(libserver common64 ${Boost_LIBRARIES} pthread)

add_executable(server main.cpp)
set_target_properties(server PROPERTIES COMPILE_FLAGS "-m64" LINK_FLAGS "-m64")
target_link_libraries(server libserver)
install(TARGETS server EXPORT ServerConfig
        RUNTIME  DESTINATION ${CMAKE_BINARY_DIR}/bin)  # This is for Windows