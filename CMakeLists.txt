cmake_minimum_required(VERSION 3.10)

project(FlappyBird)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(SDL3 CONFIG REQUIRED)
find_package(SDL3_ttf CONFIG REQUIRED)

add_executable(FlappyBird
    src/app.cpp
    src/engine.hpp
    src/bird.cpp
    src/pipes.cpp
    src/game.cpp
    src/pipes.cpp
    src/text.cpp
)

target_link_libraries(FlappyBird PRIVATE SDL3::SDL3)
target_link_libraries(FlappyBird PRIVATE SDL3_ttf::SDL3_ttf)
