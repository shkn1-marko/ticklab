add_executable(messages_test messages_test.cpp)
target_link_libraries(messages_test PRIVATE connection GTest::gtest_main)

gtest_discover_tests(messages_test)