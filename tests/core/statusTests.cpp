#include <dandb/core/Status.h>
#include <catch2/catch_test_macros.hpp>

TEST_CASE("Ok status has no error message", "[core][status]")
{
	const auto status = dandb::core::Status::Ok();

	REQUIRE(status.ok());
	REQUIRE(status.code() == dandb::core::StatusCode::Ok);
	REQUIRE(status.message().empty());
}

TEST_CASE("Error status stores code and message", "[core][status]")
{
	const auto status = dandb::core::Status::InvalidArgument("table name is empty");

	REQUIRE_FALSE(status.ok());
	REQUIRE(status.code() == dandb::core::StatusCode::InvalidArgument);
	REQUIRE(status.message() == "table name is empty");
}