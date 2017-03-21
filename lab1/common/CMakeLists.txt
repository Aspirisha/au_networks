set(SOURCE_FILES protocol.cpp)

add_library(proto64 ${SOURCE_FILES})
set_target_properties(proto64 PROPERTIES COMPILE_FLAGS "-m64" LINK_FLAGS "-m64")

add_library(proto32 ${SOURCE_FILES})
set_target_properties(proto32 PROPERTIES COMPILE_FLAGS "-m32" LINK_FLAGS "-m32")