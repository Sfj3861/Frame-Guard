cmake_minimum_required(VERSION 3.16)
project(FrameGuard VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(frame-guard src/main.cpp)
target_include_directories(frame-guard PRIVATE include)

if(MSVC)
  target_compile_options(frame-guard PRIVATE /W4 /permissive-)
else()
  target_compile_options(frame-guard PRIVATE -Wall -Wextra -Wpedantic)
endif()
