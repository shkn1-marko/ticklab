add_library(transport STATIC
    socket.cpp
    frame.cpp
)

target_include_directories(transport PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

if(WIN32)
    target_link_libraries(transport PUBLIC ws2_32)
endif()