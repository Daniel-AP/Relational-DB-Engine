#include <dandb/record/Row.h>
#include <dandb/record/Schema.h>
#include <dandb/record/Value.h>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <string>
#include <vector>

TEST_CASE("Value factories store type, nullness, and payload", "[record][value]") {

    const auto active = dandb::record::Value::boolean(true);
    const auto rating = dandb::record::Value::byte(static_cast<std::int8_t>(-7));
    const auto age = dandb::record::Value::int32(42);
    const auto visits = dandb::record::Value::int64(9000000000);
    const auto balance = dandb::record::Value::doubleValue(19.5);
    const auto name = dandb::record::Value::string("Ana");
    const auto missingName = dandb::record::Value::null(dandb::record::LogicalType::String);

    REQUIRE(active.type() == dandb::record::LogicalType::Boolean);
    REQUIRE_FALSE(active.isNull());
    REQUIRE(active.asBoolean());

    REQUIRE(rating.type() == dandb::record::LogicalType::Byte);
    REQUIRE(rating.asByte() == static_cast<std::int8_t>(-7));

    REQUIRE(age.type() == dandb::record::LogicalType::Int32);
    REQUIRE(age.asInt32() == 42);

    REQUIRE(visits.type() == dandb::record::LogicalType::Int64);
    REQUIRE(visits.asInt64() == 9000000000);

    REQUIRE(balance.type() == dandb::record::LogicalType::Double);
    REQUIRE(balance.asDouble() == 19.5);

    REQUIRE(name.type() == dandb::record::LogicalType::String);
    REQUIRE(name.asString() == "Ana");

    REQUIRE(missingName.type() == dandb::record::LogicalType::String);
    REQUIRE(missingName.isNull());

}

TEST_CASE("Schema accepts valid ordered columns", "[record][schema]") {

    const auto schemaResult = dandb::record::Schema::create({
        dandb::record::Column{ "id", dandb::record::LogicalType::Int32, false, true, 0 },
        dandb::record::Column{ "name", dandb::record::LogicalType::String, true, false, 32 },
        dandb::record::Column{ "age", dandb::record::LogicalType::Int32, true, false, 0 }
    });

    REQUIRE(schemaResult.ok());

    const auto& schema = schemaResult.value();

    REQUIRE(schema.columnCount() == 3);
    REQUIRE(schema.column(0).name == "id");
    REQUIRE(schema.column(0).primaryKey);
    REQUIRE_FALSE(schema.column(0).nullable);
    REQUIRE(schema.column(1).type == dandb::record::LogicalType::String);
    REQUIRE(schema.column(1).stringCapacity == 32);
    REQUIRE(schema.column(2).name == "age");

}

TEST_CASE("Schema rejects invalid column definitions", "[record][schema]") {

    const auto emptySchema = dandb::record::Schema::create({});
    const auto duplicateNames = dandb::record::Schema::create({
        dandb::record::Column{ "id", dandb::record::LogicalType::Int32, false, false, 0 },
        dandb::record::Column{ "id", dandb::record::LogicalType::Int64, false, false, 0 }
    });
    const auto stringWithoutCapacity = dandb::record::Schema::create({
        dandb::record::Column{ "name", dandb::record::LogicalType::String, true, false, 0 }
    });
    const auto nonStringWithCapacity = dandb::record::Schema::create({
        dandb::record::Column{ "age", dandb::record::LogicalType::Int32, true, false, 4 }
    });
    const auto twoPrimaryKeys = dandb::record::Schema::create({
        dandb::record::Column{ "id", dandb::record::LogicalType::Int32, false, true, 0 },
        dandb::record::Column{ "email", dandb::record::LogicalType::String, false, true, 64 }
    });

    REQUIRE_FALSE(emptySchema.ok());
    REQUIRE_FALSE(duplicateNames.ok());
    REQUIRE_FALSE(stringWithoutCapacity.ok());
    REQUIRE_FALSE(nonStringWithCapacity.ok());
    REQUIRE_FALSE(twoPrimaryKeys.ok());

}

TEST_CASE("Schema allows at most 255 columns", "[record][schema]") {

    std::vector<dandb::record::Column> maxColumns;
    for(int index = 0; index < 255; index++) {
        maxColumns.push_back(
            dandb::record::Column{
                "column_" + std::to_string(index),
                dandb::record::LogicalType::Int32,
                true,
                false,
                0
            }
        );
    }

    auto tooManyColumns = maxColumns;
    tooManyColumns.push_back(
        dandb::record::Column{
            "column_255",
            dandb::record::LogicalType::Int32,
            true,
            false,
            0
        }
    );

    REQUIRE(dandb::record::Schema::create(maxColumns).ok());
    REQUIRE_FALSE(dandb::record::Schema::create(tooManyColumns).ok());

}

TEST_CASE("Row stores values in column order", "[record][row]") {

    const dandb::record::Row row({
        dandb::record::Value::int32(1),
        dandb::record::Value::string("Ana"),
        dandb::record::Value::null(dandb::record::LogicalType::Int32)
    });

    REQUIRE(row.valueCount() == 3);
    REQUIRE(row.value(0).asInt32() == 1);
    REQUIRE(row.value(1).asString() == "Ana");
    REQUIRE(row.value(2).type() == dandb::record::LogicalType::Int32);
    REQUIRE(row.value(2).isNull());

}
