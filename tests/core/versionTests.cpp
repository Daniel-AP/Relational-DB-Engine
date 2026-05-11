#include <dandb/core/version.h>
#include <catch2/catch_test_macros.hpp>

TEST_CASE("Project name and version is correct", "[version]") {

    REQUIRE(dandb::core::projectName() == "dandb");
    REQUIRE(dandb::core::projectVersion() == "0.0.0");

}
