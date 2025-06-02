include(FetchContent)

FetchContent_Declare(
  catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG v3.4.0
)

FetchContent_MakeAvailable(catch2)

add_executable(functional-reactive-cpp_tests test.cpp)
target_link_libraries(functional-reactive-cpp_tests PRIVATE functional-reactive-cpp Catch2::Catch2WithMain)

include(CTest)
include(Catch)
catch_discover_tests(functional-reactive-cpp_tests)
