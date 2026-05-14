#include <dandb/record/Layout.h>
#include <dandb/record/Schema.h>
#include <dandb/record/Value.h>

#include <catch2/catch_test_macros.hpp>

TEST_CASE("Layout aligns offsets to the requested boundary", "[record][layout]") {

    REQUIRE(dandb::record::layout::alignTo(0, 8) == 0);
    REQUIRE(dandb::record::layout::alignTo(8, 8) == 8);
    REQUIRE(dandb::record::layout::alignTo(9, 8) == 16);
    REQUIRE(dandb::record::layout::alignTo(13, 4) == 16);

}

TEST_CASE("Layout calculates null bitmap byte count", "[record][layout]") {

    REQUIRE(dandb::record::layout::nullBitmapSize(1) == 1);
    REQUIRE(dandb::record::layout::nullBitmapSize(8) == 1);
    REQUIRE(dandb::record::layout::nullBitmapSize(9) == 2);
    REQUIRE(dandb::record::layout::nullBitmapSize(255) == 32);

}

TEST_CASE("Layout reports fixed value sizes", "[record][layout]") {

    REQUIRE(dandb::record::layout::valueSize(
        dandb::record::Column{ "active", dandb::record::LogicalType::Boolean, false, false, 0 }
    ) == 1);
    REQUIRE(dandb::record::layout::valueSize(
        dandb::record::Column{ "rating", dandb::record::LogicalType::Byte, false, false, 0 }
    ) == 1);
    REQUIRE(dandb::record::layout::valueSize(
        dandb::record::Column{ "age", dandb::record::LogicalType::Int32, false, false, 0 }
    ) == 4);
    REQUIRE(dandb::record::layout::valueSize(
        dandb::record::Column{ "visits", dandb::record::LogicalType::Int64, false, false, 0 }
    ) == 8);
    REQUIRE(dandb::record::layout::valueSize(
        dandb::record::Column{ "balance", dandb::record::LogicalType::Double, false, false, 0 }
    ) == 8);
    REQUIRE(dandb::record::layout::valueSize(
        dandb::record::Column{ "name", dandb::record::LogicalType::String, false, false, 32 }
    ) == 32);

}

TEST_CASE("Layout reports value alignments", "[record][layout]") {

    REQUIRE(dandb::record::layout::valueAlignment(dandb::record::LogicalType::Boolean) == 1);
    REQUIRE(dandb::record::layout::valueAlignment(dandb::record::LogicalType::Byte) == 1);
    REQUIRE(dandb::record::layout::valueAlignment(dandb::record::LogicalType::Int32) == 4);
    REQUIRE(dandb::record::layout::valueAlignment(dandb::record::LogicalType::Int64) == 8);
    REQUIRE(dandb::record::layout::valueAlignment(dandb::record::LogicalType::Double) == 8);
    REQUIRE(dandb::record::layout::valueAlignment(dandb::record::LogicalType::String) == 1);

}

TEST_CASE("Layout calculates encoded row size with header bitmap values and padding", "[record][layout]") {

    auto schema = dandb::record::Schema::create({
        dandb::record::Column{ "active", dandb::record::LogicalType::Boolean, false, false, 0 },
        dandb::record::Column{ "rating", dandb::record::LogicalType::Byte, false, false, 0 },
        dandb::record::Column{ "age", dandb::record::LogicalType::Int32, false, false, 0 },
        dandb::record::Column{ "visits", dandb::record::LogicalType::Int64, false, false, 0 },
        dandb::record::Column{ "balance", dandb::record::LogicalType::Double, false, false, 0 },
        dandb::record::Column{ "name", dandb::record::LogicalType::String, false, false, 4 }
    });

    REQUIRE(schema.ok());
    REQUIRE(dandb::record::layout::encodedSize(schema.value()) == 40);

}

TEST_CASE("Layout accounts for alignment padding between mixed column types", "[record][layout]") {

    auto schema = dandb::record::Schema::create({
        dandb::record::Column{ "tiny", dandb::record::LogicalType::Byte, false, false, 0 },
        dandb::record::Column{ "wide", dandb::record::LogicalType::Int64, false, false, 0 },
        dandb::record::Column{ "medium", dandb::record::LogicalType::Int32, false, false, 0 }
    });

    REQUIRE(schema.ok());
    REQUIRE(dandb::record::layout::encodedSize(schema.value()) == 32);

}
