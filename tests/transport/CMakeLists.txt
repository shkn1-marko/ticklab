include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.18.0
)
FetchContent_MakeAvailable(googletest)

add_executable(frame_test frame_test.cpp)
target_link_libraries(frame_test PRIVATE transport GTest::gtest_main)

add_executable(socket_test socket_test.cpp)
target_link_libraries(socket_test PRIVATE transport GTest::gtest_main)

add_executable(sequence_tracker_test sequence_tracker_test.cpp)
target_link_libraries(sequence_tracker_test PRIVATE transport GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(frame_test)
gtest_discover_tests(socket_test PROPERTIES TIMEOUT 10)
gtest_discover_tests(sequence_tracker_test)