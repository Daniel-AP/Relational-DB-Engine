#include <dandb/core/Helper.h>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <cstdint>

TEST_CASE("Helper writes and reads little-endian unsigned integers", "[core][helper]") {

    std::array<std::byte, 16> bytes{};

    dandb::core::helper::writeUint16(bytes, 0, 0x1234);
    dandb::core::helper::writeUint32(bytes, 2, 0x01020304);
    dandb::core::helper::writeUint64(bytes, 6, 0x0102030405060708);

    REQUIRE(bytes[0] == std::byte{0x34});
    REQUIRE(bytes[1] == std::byte{0x12});
    REQUIRE(bytes[2] == std::byte{0x04});
    REQUIRE(bytes[3] == std::byte{0x03});
    REQUIRE(bytes[4] == std::byte{0x02});
    REQUIRE(bytes[5] == std::byte{0x01});
    REQUIRE(bytes[6] == std::byte{0x08});
    REQUIRE(bytes[7] == std::byte{0x07});
    REQUIRE(bytes[8] == std::byte{0x06});
    REQUIRE(bytes[9] == std::byte{0x05});
    REQUIRE(bytes[10] == std::byte{0x04});
    REQUIRE(bytes[11] == std::byte{0x03});
    REQUIRE(bytes[12] == std::byte{0x02});
    REQUIRE(bytes[13] == std::byte{0x01});

    REQUIRE(dandb::core::helper::readUint16(bytes, 0) == 0x1234);
    REQUIRE(dandb::core::helper::readUint32(bytes, 2) == 0x01020304);
    REQUIRE(dandb::core::helper::readUint64(bytes, 6) == 0x0102030405060708);

}

TEST_CASE("Helper writes and reads double bytes", "[core][helper]") {

    std::array<std::byte, 8> bytes{};

    dandb::core::helper::writeDouble(bytes, 0, 1.5);

    REQUIRE(dandb::core::helper::readDouble(bytes, 0) == 1.5);

}
