cmake_minimum_required(VERSION 3.13)

# borrowed from pico?
project(test_project C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)


project(sync-calendar)
add_executable(sync-calendar2 sync-calendar2.cpp)

# https://stackoverflow.com/questions/15657931/linking-curl-in-a-project-using-cmake
target_link_libraries(sync-calendar2 curl ical pisock)
