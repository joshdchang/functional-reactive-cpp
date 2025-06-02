add_executable(FlappyBird FlappyBird.cpp engine.cpp)

target_link_libraries(FlappyBird PRIVATE SDL3::SDL3)
target_link_libraries(FlappyBird PRIVATE SDL3_ttf::SDL3_ttf)
