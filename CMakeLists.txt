cmake_minimum_required(VERSION 3.13)
# https://cmake.org/cmake/help/latest/guide/tutorial/A%20Basic%20Starting%20Point.html#exercise-1-building-a-basic-project

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Choose Release or Debug" FORCE)
endif()

project(sync-calendar C CXX)
add_executable(sync-calendar2 sync-calendar2.cpp)

# https://stackoverflow.com/questions/15657931/linking-curl-in-a-project-using-cmake
target_link_libraries(sync-calendar2 config++ curl ical pisock usb usb-1.0)

# copy the datebook cfg to build to make running for debugging easy
set(datebookcfgfile "datebook.cfg")
add_custom_target(${datebookcfgfile} 
    BYPRODUCTS ${CMAKE_CURRENT_BINARY_DIR}/${datebookcfgfile}
    COMMAND ${CMAKE_COMMAND} -E create_symlink
#        ${CMAKE_CURRENT_SOURCE_DIR}/${datebookcfgfile}
#        ${CMAKE_CURRENT_BINARY_DIR}/${datebookcfgfile}
        $(CMAKE_SOURCE_DIR)/${datebookcfgfile}
        $(CMAKE_BINARY_DIR)/${datebookcfgfile} # more resilent to odd characters in path
)
add_dependencies(sync-calendar2 ${datebookcfgfile})

# add the git revision
find_package(Git REQUIRED)
if(GIT_FOUND AND CMAKE_BUILD_TYPE MATCHES Release)
    execute_process(COMMAND "${GIT_EXECUTABLE}" --git-dir ../.git rev-parse --short HEAD OUTPUT_VARIABLE SYNCVERSION OUTPUT_STRIP_TRAILING_WHITESPACE RESULT_VARIABLE ret)
    if(NOT ret EQUAL "0")
        string(TIMESTAMP now "%Y%m%d%H%M")
        cmake_host_system_information(RESULT host QUERY HOSTNAME)
        set(SYNCVERSION "${host} ${now}")
    endif()
    add_definitions(-DSYNCVERSION="${SYNCVERSION}")
else()
    add_definitions(-DSYNCVERSION="debug build")
endif()
