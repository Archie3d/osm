cmake_minimum_required(VERSION 3.30)

project(osm_sdl LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_subdirectory(external)

include(CTest)
add_subdirectory(osm)
add_subdirectory(viewer)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
