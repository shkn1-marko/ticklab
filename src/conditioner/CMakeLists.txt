add_library(conditioner STATIC
    conditioner.cpp
)

target_include_directories(conditioner PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
