cmake_minimum_required(VERSION 3.25)
project(FiveMapper VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_compile_options(/W4 /permissive- /WX)

add_executable(FiveMapper
    src/main.cpp
)