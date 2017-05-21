set(SOURCE_FILES main.cpp Client.cpp)

add_executable(client64 ${SOURCE_FILES})
target_include_directories(client64 PUBLIC ../common)
set_target_properties(client64 PROPERTIES COMPILE_FLAGS "-m64" LINK_FLAGS "-m64")
target_link_libraries(client64 common64 pthread)

add_executable(client32 ${SOURCE_FILES})
target_include_directories(client32 PUBLIC ../common)
set_target_properties(client32 PROPERTIES COMPILE_FLAGS "-m32" LINK_FLAGS "-m32")
target_link_libraries(client32 common32 pthread)

install(TARGETS client32 client64 EXPORT ServerConfig
        RUNTIME  DESTINATION ${CMAKE_BINARY_DIR}/bin)  # This is for Windows

file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/clientlogger.conf DESTINATION ${CMAKE_BINARY_DIR}/bin)