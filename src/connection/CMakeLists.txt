add_library(connection STATIC
    messages.cpp
    connection.cpp
    connection_table.cpp
    client_connector.cpp
)

target_include_directories(connection PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(connection PUBLIC transport)