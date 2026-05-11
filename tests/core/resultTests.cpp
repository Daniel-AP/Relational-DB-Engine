#include <dandb/core/Result.h>
#include <catch2/catch_test_macros.hpp>

#include <stdexcept>
#include <string>

TEST_CASE("Successful result stores a value", "[core][result]") {

    const dandb::core::Result<int> result(42);

    REQUIRE(result.ok());
    REQUIRE(result.value() == 42);
    REQUIRE(result.status().ok());

}

TEST_CASE("Failed result stores a status", "[core][result]") {

    const dandb::core::Result<std::string> result(
        dandb::core::Status::NotFound("database was not found")
    );

    REQUIRE_FALSE(result.ok());
    REQUIRE(result.status().code() == dandb::core::StatusCode::NotFound);
    REQUIRE(result.status().message() == "database was not found");

}

TEST_CASE("Result cannot be constructed from Ok status", "[core][result]") {

    REQUIRE_THROWS_AS(
        dandb::core::Result<int>(dandb::core::Status::Ok()),
        std::invalid_argument
    );

}
