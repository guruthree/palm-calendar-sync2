cmake_minimum_required(VERSION 3.13)
# https://cmake.org/cmake/help/latest/guide/tutorial/A%20Basic%20Starting%20Point.html#exercise-1-building-a-basic-project

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

set(CMAKE_BUILD_TYPE Debug)

project(sync-calendar C CXX)
add_executable(sync-calendar2 sync-calendar2.cpp)

# https://stackoverflow.com/questions/15657931/linking-curl-in-a-project-using-cmake
target_link_libraries(sync-calendar2 config++ curl ical pisock usb usb-1.0)
