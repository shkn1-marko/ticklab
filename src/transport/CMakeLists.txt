if(WIN32)
    set(SOCKET_IMPL socket_win.cpp)
else()
    set(SOCKET_IMPL socket_posix.cpp)
endif()

add_library(transport STATIC
    frame.cpp
    sequence_tracker.cpp
    ${SOCKET_IMPL}
)

target_include_directories(transport PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

if(WIN32)
    target_link_libraries(transport PUBLIC ws2_32)
endif()