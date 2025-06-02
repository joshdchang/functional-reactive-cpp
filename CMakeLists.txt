cmake_minimum_required(VERSION 3.16)
project(functional-reactive-cpp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(functional-reactive-cpp src/main.cpp)
target_include_directories(functional-reactive-cpp PUBLIC ${PROJECT_SOURCE_DIR}/include)

add_executable(functional-reactive-cpp_exe src/main.cpp)
target_link_libraries(functional-reactive-cpp_exe PRIVATE functional-reactive-cpp)

enable_testing()
add_subdirectory(tests)
