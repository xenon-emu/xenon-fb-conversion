# Copyright 2025 Xenon Emulator Project

cmake_minimum_required(VERSION 3.20)

set(CMAKE_C_STANDARD 17)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED TRUE)
set(CMAKE_CXX_STANDARD_REQUIRED TRUE)

if (NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)

project(xenon-fb-conversion)

add_subdirectory(third_party)

find_package(SDL3 3.2.4 CONFIG)
find_package(OpenGL REQUIRED)

# GLAD and OpenGL
set(OPENGL third_party/glad/src/glad.c)
include_directories(Xenon-fb-conversion third_party/glad/include)

add_executable(xenon-fb-conversion ${OPENGL} src/main.cpp)

target_link_libraries(xenon-fb-conversion PRIVATE SDL3::SDL3 OpenGL::GL)

if (WIN32)
  target_sources(xenon-fb-conversion PRIVATE src/Xenon.rc)
endif()
