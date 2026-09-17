add_library(transport STATIC
    socket.cpp
    frame.cpp
)

target_include_directories(transport PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})