#include <catch2/catch_test_macros.hpp>
#include "functional-reactive-cpp/foo.hpp"

TEST_CASE("Addition works") {
    REQUIRE(add(2, 3) == 5);
}
