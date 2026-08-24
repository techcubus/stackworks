cmake_minimum_required(VERSION 3.16)
project(stackworks LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

find_package(Qt6 REQUIRED COMPONENTS Widgets)

add_executable(stackworks
    src/main.cpp
    src/stack.c
    src/woba.c
    src/rsrc.c
    src/ui/mainwindow.cpp
    src/ui/cardview.cpp
)
set_target_properties(stackworks PROPERTIES AUTOMOC ON)
target_include_directories(stackworks PRIVATE src)
target_compile_definitions(stackworks PRIVATE _POSIX_C_SOURCE=200809L)
target_compile_options(stackworks PRIVATE -Wall -Wextra)
target_link_libraries(stackworks PRIVATE Qt6::Widgets)

add_executable(rsrcextract tools/rsrcextract.c src/rsrc.c)
target_compile_options(rsrcextract PRIVATE -Wall -Wextra)

add_executable(pict2ppm tools/pict2ppm.c)
target_compile_options(pict2ppm PRIVATE -Wall -Wextra)
