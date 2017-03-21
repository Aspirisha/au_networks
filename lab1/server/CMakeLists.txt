set(SOURCE_FILES main.cpp ServerSocket.cpp)

add_executable(server ${SOURCE_FILES})
set_target_properties(server PROPERTIES COMPILE_FLAGS "-m64" LINK_FLAGS "-m64")

target_include_directories(server PUBLIC ../common)

install(TARGETS server EXPORT ServerConfig
        RUNTIME  DESTINATION ${CMAKE_BINARY_DIR}/bin)  # This is for Windows