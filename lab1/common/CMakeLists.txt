set(SOURCE_FILES protocol.cpp tcp_socket.cpp au_socket.cpp persistence.cpp ../easyloggingpp/src/easylogging++.cc)

add_library(common64 ${SOURCE_FILES})
set_target_properties(common64 PROPERTIES COMPILE_FLAGS "-m64" LINK_FLAGS "-m64")

add_library(common32 ${SOURCE_FILES})
set_target_properties(common32 PROPERTIES COMPILE_FLAGS "-m32" LINK_FLAGS "-m32")