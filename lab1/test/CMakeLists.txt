set(SOURCE_FILES main.cpp test_simple.cpp server_test.cpp)
FIND_PACKAGE(GTest REQUIRED)
find_package(Boost REQUIRED COMPONENTS filesystem system)

add_executable(socktest ${SOURCE_FILES})
target_include_directories(socktest PRIVATE ${GTEST_INCLUDE_DIRS} ${Boost_INCLUDE_DIRS})
target_link_libraries(socktest libserver common64 ${GTEST_BOTH_LIBRARIES} ${Boost_LIBRARIES} pthread)
