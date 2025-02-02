cmake_minimum_required(VERSION 3.20)

set(projectname xenon-fb-conversion)
project(${projectname})
add_executable(${projectname})

target_sources(${projectname} PRIVATE
  main.cpp
  glad/src/glad.c
)
target_include_directories(${projectname} PRIVATE
  glad/include
)

include(SDL3/x86_64-w64-mingw32/lib/cmake/SDL3/SDL3Config.cmake)

find_package(SDL3 REQUIRED)

find_package(OpenGL REQUIRED)

target_link_libraries(${projectname} PRIVATE SDL3::SDL3 OpenGL::GL)

target_link_options(${projectname} PRIVATE -static)