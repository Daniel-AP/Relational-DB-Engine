#include <dandb/record/Codec.h>
#include <dandb/record/Row.h>
#include <dandb/record/Schema.h>
#include <dandb/record/Value.h>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace {

    dandb::record::Schema allTypesSchema() {

        auto schema = dandb::record::Schema::create({
            dandb::record::Column{ "active", dandb::record::LogicalType::Boolean, false, false, 0 },
            dandb::record::Column{ "rating", dandb::record::LogicalType::Byte, false, false, 0 },
            dandb::record::Column{ "age", dandb::record::LogicalType::Int32, false, false, 0 },
            dandb::record::Column{ "visits", dandb::record::LogicalType::Int64, false, false, 0 },
            dandb::record::Column{ "balance", dandb::record::LogicalType::Double, false, false, 0 },
            dandb::record::Column{ "name", dandb::record::LogicalType::String, false, false, 4 }
        });

        REQUIRE(schema.ok());

        return schema.value();

    }

    dandb::record::Schema nullableSchema() {

        auto schema = dandb::record::Schema::create({
            dandb::record::Column{ "age", dandb::record::LogicalType::Int32, true, false, 0 },
            dandb::record::Column{ "name", dandb::record::LogicalType::String, true, false, 4 },
            dandb::record::Column{ "score", dandb::record::LogicalType::Double, true, false, 0 }
        });

        REQUIRE(schema.ok());

        return schema.value();

    }

    void writeUint32(std::vector<std::byte>& buffer, size_t offset, uint32_t value) {

        for(size_t i = 0; i < 4; i++) {
            buffer[offset+i] = static_cast<std::byte>((value>>(8*i))&0xFFu);
        }

    }

    void writeUint64(std::vector<std::byte>& buffer, size_t offset, uint64_t value) {

        for(size_t i = 0; i < 8; i++) {
            buffer[offset+i] = static_cast<std::byte>((value>>(8*i))&0xFFu);
        }

    }

    void writeDouble(std::vector<std::byte>& buffer, size_t offset, double value) {

        std::array<std::byte, 8> doubleBytes{};
        std::memcpy(doubleBytes.data(), &value, doubleBytes.size());

        for(size_t i = 0; i < doubleBytes.size(); i++) {
            buffer[offset+i] = doubleBytes[i];
        }

    }

}

TEST_CASE("Codec encodes all logical types with alignment and little-endian bytes", "[record][codec]") {

    const auto schema = allTypesSchema();
    const dandb::record::Row row({
        dandb::record::Value::boolean(true),
        dandb::record::Value::byte(static_cast<int8_t>(-7)),
        dandb::record::Value::int32(0x01020304),
        dandb::record::Value::int64(0x0102030405060708),
        dandb::record::Value::doubleValue(1.5),
        dandb::record::Value::string("Ana")
    });

    const auto encoded = dandb::record::Codec::encode(schema, row);

    REQUIRE(encoded.ok());

    std::vector<std::byte> expected(40, std::byte{0});
    expected[0] = std::byte{6};
    expected[1] = std::byte{0};
    expected[8] = std::byte{1};
    expected[9] = std::byte{249};
    writeUint32(expected, 12, 0x01020304);
    writeUint64(expected, 16, 0x0102030405060708);
    writeDouble(expected, 24, 1.5);
    expected[32] = std::byte{'A'};
    expected[33] = std::byte{'n'};
    expected[34] = std::byte{'a'};
    expected[35] = std::byte{0};

    REQUIRE(encoded.value() == expected);

}

TEST_CASE("Codec decodes all logical types from fixed row bytes", "[record][codec]") {

    const auto schema = allTypesSchema();

    std::vector<std::byte> bytes(40, std::byte{0});
    bytes[0] = std::byte{6};
    bytes[1] = std::byte{0};
    bytes[8] = std::byte{1};
    bytes[9] = std::byte{249};
    writeUint32(bytes, 12, 0x01020304);
    writeUint64(bytes, 16, 0x0102030405060708);
    writeDouble(bytes, 24, 1.5);
    bytes[32] = std::byte{'A'};
    bytes[33] = std::byte{'n'};
    bytes[34] = std::byte{'a'};

    const auto decoded = dandb::record::Codec::decode(schema, bytes);

    REQUIRE(decoded.ok());
    REQUIRE(decoded.value().valueCount() == 6);
    REQUIRE(decoded.value().value(0).asBoolean());
    REQUIRE(decoded.value().value(1).asByte() == static_cast<int8_t>(-7));
    REQUIRE(decoded.value().value(2).asInt32() == 0x01020304);
    REQUIRE(decoded.value().value(3).asInt64() == 0x0102030405060708);
    REQUIRE(decoded.value().value(4).asDouble() == 1.5);
    REQUIRE(decoded.value().value(5).asString() == "Ana");

}

TEST_CASE("Codec encodes null bitmap and zero payload bytes for null values", "[record][codec]") {

    const auto schema = nullableSchema();
    const dandb::record::Row row({
        dandb::record::Value::null(dandb::record::LogicalType::Int32),
        dandb::record::Value::string("A"),
        dandb::record::Value::null(dandb::record::LogicalType::Double)
    });

    const auto encoded = dandb::record::Codec::encode(schema, row);

    REQUIRE(encoded.ok());

    std::vector<std::byte> expected(24, std::byte{0});
    expected[0] = std::byte{3};
    expected[1] = std::byte{0b00000101};
    expected[12] = std::byte{'A'};

    REQUIRE(encoded.value() == expected);

}

TEST_CASE("Codec decodes null bitmap into typed null values", "[record][codec]") {

    const auto schema = nullableSchema();

    std::vector<std::byte> bytes(24, std::byte{0});
    bytes[0] = std::byte{3};
    bytes[1] = std::byte{0b00000101};
    bytes[12] = std::byte{'A'};

    const auto decoded = dandb::record::Codec::decode(schema, bytes);

    REQUIRE(decoded.ok());
    REQUIRE(decoded.value().value(0).type() == dandb::record::LogicalType::Int32);
    REQUIRE(decoded.value().value(0).isNull());
    REQUIRE(decoded.value().value(1).asString() == "A");
    REQUIRE(decoded.value().value(2).type() == dandb::record::LogicalType::Double);
    REQUIRE(decoded.value().value(2).isNull());

}

TEST_CASE("Codec encodes empty and full fixed-size strings", "[record][codec]") {

    auto schema = dandb::record::Schema::create({
        dandb::record::Column{ "empty", dandb::record::LogicalType::String, false, false, 4 },
        dandb::record::Column{ "full", dandb::record::LogicalType::String, false, false, 4 }
    });
    REQUIRE(schema.ok());

    const dandb::record::Row row({
        dandb::record::Value::string(""),
        dandb::record::Value::string("Luis")
    });

    const auto encoded = dandb::record::Codec::encode(schema.value(), row);

    REQUIRE(encoded.ok());

    std::vector<std::byte> expected(16, std::byte{0});
    expected[0] = std::byte{2};
    expected[1] = std::byte{0};
    expected[12] = std::byte{'L'};
    expected[13] = std::byte{'u'};
    expected[14] = std::byte{'i'};
    expected[15] = std::byte{'s'};

    REQUIRE(encoded.value() == expected);

}

TEST_CASE("Codec trims decoded strings at the first zero byte", "[record][codec]") {

    auto schema = dandb::record::Schema::create({
        dandb::record::Column{ "name", dandb::record::LogicalType::String, false, false, 8 }
    });
    REQUIRE(schema.ok());

    std::vector<std::byte> bytes(16, std::byte{0});
    bytes[0] = std::byte{1};
    bytes[1] = std::byte{0};
    bytes[8] = std::byte{'A'};
    bytes[9] = std::byte{'n'};
    bytes[10] = std::byte{'a'};

    const auto decoded = dandb::record::Codec::decode(schema.value(), bytes);

    REQUIRE(decoded.ok());
    REQUIRE(decoded.value().value(0).asString() == "Ana");

}

TEST_CASE("Codec rejects rows with the wrong number of values", "[record][codec]") {

    const auto schema = allTypesSchema();
    const dandb::record::Row tooShort({
        dandb::record::Value::boolean(true)
    });

    const auto encoded = dandb::record::Codec::encode(schema, tooShort);

    REQUIRE_FALSE(encoded.ok());

}

TEST_CASE("Codec rejects values with the wrong logical type", "[record][codec]") {

    auto schema = dandb::record::Schema::create({
        dandb::record::Column{ "age", dandb::record::LogicalType::Int32, false, false, 0 }
    });
    REQUIRE(schema.ok());

    const dandb::record::Row row({
        dandb::record::Value::string("not an int")
    });

    const auto encoded = dandb::record::Codec::encode(schema.value(), row);

    REQUIRE_FALSE(encoded.ok());

}

TEST_CASE("Codec rejects null for non-nullable columns", "[record][codec]") {

    auto schema = dandb::record::Schema::create({
        dandb::record::Column{ "age", dandb::record::LogicalType::Int32, false, false, 0 }
    });
    REQUIRE(schema.ok());

    const dandb::record::Row row({
        dandb::record::Value::null(dandb::record::LogicalType::Int32)
    });

    const auto encoded = dandb::record::Codec::encode(schema.value(), row);

    REQUIRE_FALSE(encoded.ok());

}

TEST_CASE("Codec rejects strings that exceed fixed STRING capacity", "[record][codec]") {

    auto schema = dandb::record::Schema::create({
        dandb::record::Column{ "name", dandb::record::LogicalType::String, false, false, 4 }
    });
    REQUIRE(schema.ok());

    const dandb::record::Row row({
        dandb::record::Value::string("Maria")
    });

    const auto encoded = dandb::record::Codec::encode(schema.value(), row);

    REQUIRE_FALSE(encoded.ok());

}

TEST_CASE("Codec rejects decoded bytes with the wrong column count", "[record][codec]") {

    const auto schema = allTypesSchema();

    std::vector<std::byte> bytes(40, std::byte{0});
    bytes[0] = std::byte{5};

    const auto decoded = dandb::record::Codec::decode(schema, bytes);

    REQUIRE_FALSE(decoded.ok());

}

TEST_CASE("Codec rejects decoded bytes with unused null bitmap bits set", "[record][codec]") {

    auto schema = dandb::record::Schema::create({
        dandb::record::Column{ "age", dandb::record::LogicalType::Int32, true, false, 0 }
    });
    REQUIRE(schema.ok());

    std::vector<std::byte> bytes(16, std::byte{0});
    bytes[0] = std::byte{1};
    bytes[1] = std::byte{0b10000000};

    const auto decoded = dandb::record::Codec::decode(schema.value(), bytes);

    REQUIRE_FALSE(decoded.ok());

}

TEST_CASE("Codec rejects decoded byte buffers with the wrong row size", "[record][codec]") {

    const auto schema = allTypesSchema();

    std::vector<std::byte> tooSmall(39, std::byte{0});
    tooSmall[0] = std::byte{6};

    std::vector<std::byte> tooLarge(41, std::byte{0});
    tooLarge[0] = std::byte{6};

    REQUIRE_FALSE(dandb::record::Codec::decode(schema, tooSmall).ok());
    REQUIRE_FALSE(dandb::record::Codec::decode(schema, tooLarge).ok());

}
