include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.18.0
)
FetchContent_MakeAvailable(googletest)

include(GoogleTest)

add_subdirectory(transport)
add_subdirectory(connection)
add_subdirectory(conditioner)