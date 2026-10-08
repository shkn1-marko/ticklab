add_executable(receiver receiver.cpp)
target_link_libraries(receiver PRIVATE transport)

add_executable(sender sender.cpp)
target_link_libraries(sender PRIVATE transport)