set(SOURCE_FILES main.cpp)

add_executable(client64 ${SOURCE_FILES})
set_target_properties(client64 PROPERTIES COMPILE_FLAGS "-m64" LINK_FLAGS "-m64")

add_executable(client32 ${SOURCE_FILES})
set_target_properties(client32 PROPERTIES COMPILE_FLAGS "-m32" LINK_FLAGS "-m32")

install(TARGETS client32 client64 EXPORT ServerConfig
        RUNTIME  DESTINATION ${CMAKE_BINARY_DIR}/bin)  # This is for Windows