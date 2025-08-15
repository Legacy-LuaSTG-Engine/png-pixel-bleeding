cmake_minimum_required(VERSION 3.30)
project(png_pixel_bleed)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_compile_definitions(_UNICODE UNICODE)
if (CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    message(STATUS "CMAKE_CXX_COMPILER_ID: MSVC")
    message(STATUS "-- add compile option /utf-8 /W4 /sdl")
    add_compile_options(/utf-8 /W4 /sdl)
endif ()

add_subdirectory(external)
add_subdirectory(main)
