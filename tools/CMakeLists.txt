add_executable(receiver receiver.cpp)
target_link_libraries(receiver PRIVATE transport)

add_executable(sender sender.cpp)
target_link_libraries(sender PRIVATE transport)

add_executable(server server.cpp)
target_link_libraries(server PRIVATE connection)

add_executable(client client.cpp)
target_link_libraries(client PRIVATE connection)