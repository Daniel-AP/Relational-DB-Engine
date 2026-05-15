#include <dandb/core/Helper.h>
#include <dandb/core/Constants.h>
#include <dandb/core/Status.h>
#include <dandb/record/SlottedPage.h>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <vector>

namespace {

    std::vector<std::byte> bytes(std::initializer_list<unsigned int> values) {

        std::vector<std::byte> result;
        result.reserve(values.size());

        for(const auto value : values) {
            result.push_back(static_cast<std::byte>(value));
        }

        return result;

    }

    bool isZeroedFrom(const std::array<std::byte, dandb::core::PAGE_SIZE_BYTES>& page, size_t offset) {

        for(size_t index = offset; index < page.size(); index++) {
            if(page[index] != std::byte{0}) {
                return false;
            }
        }

        return true;

    }

}

TEST_CASE("SlottedPage initializes empty table page bytes", "[record][slotted-page]") {

    std::array<std::byte, dandb::core::PAGE_SIZE_BYTES> pageBytes{};
    pageBytes.fill(std::byte{0xAB});

    dandb::record::SlottedPage::initialize(pageBytes, 7);
    const dandb::record::SlottedPage page(pageBytes);

    REQUIRE(dandb::core::helper::readUint32(pageBytes, 0) == 7);
    REQUIRE(dandb::core::helper::readUint16(pageBytes, 4) == 1);
    REQUIRE(dandb::core::helper::readUint32(pageBytes, 8) == dandb::core::INVALID_PAGE_ID);
    REQUIRE(dandb::core::helper::readUint16(pageBytes, 12) == 0);
    REQUIRE(dandb::core::helper::readUint16(pageBytes, 14) == 24);
    REQUIRE(dandb::core::helper::readUint16(pageBytes, 16) == dandb::core::PAGE_SIZE_BYTES);
    REQUIRE(isZeroedFrom(pageBytes, 18));

    REQUIRE(page.pageId() == 7);
    REQUIRE(page.nextPageId() == dandb::core::INVALID_PAGE_ID);
    REQUIRE(page.slotCount() == 0);
    REQUIRE(page.freeStartOffset() == 24);
    REQUIRE(page.freeEndOffset() == dandb::core::PAGE_SIZE_BYTES);

}

TEST_CASE("SlottedPage inserts one row and stores it behind a slot entry", "[record][slotted-page]") {

    std::array<std::byte, dandb::core::PAGE_SIZE_BYTES> pageBytes{};
    dandb::record::SlottedPage::initialize(pageBytes, 9);
    dandb::record::SlottedPage page(pageBytes);

    const auto row = bytes({ 10, 20, 30, 40 });

    const auto slot = page.insertRow(row);

    REQUIRE(slot.ok());
    REQUIRE(slot.value() == 0);
    REQUIRE(page.slotCount() == 1);
    REQUIRE(page.freeStartOffset() == 28);
    REQUIRE(page.freeEndOffset() == dandb::core::PAGE_SIZE_BYTES-row.size());
    REQUIRE(dandb::core::helper::readUint16(pageBytes, 24) == dandb::core::PAGE_SIZE_BYTES-row.size());
    REQUIRE(dandb::core::helper::readUint16(pageBytes, 26) == row.size());

    const auto stored = page.readRow(slot.value());

    REQUIRE(stored.ok());
    REQUIRE(stored.value() == row);

}

TEST_CASE("SlottedPage appends slots upward and row payloads downward", "[record][slotted-page]") {

    std::array<std::byte, dandb::core::PAGE_SIZE_BYTES> pageBytes{};
    dandb::record::SlottedPage::initialize(pageBytes, 11);
    dandb::record::SlottedPage page(pageBytes);

    const auto first = bytes({ 1, 2, 3 });
    const auto second = bytes({ 4, 5, 6, 7, 8 });

    const auto firstSlot = page.insertRow(first);
    const auto secondSlot = page.insertRow(second);

    REQUIRE(firstSlot.ok());
    REQUIRE(secondSlot.ok());
    REQUIRE(firstSlot.value() == 0);
    REQUIRE(secondSlot.value() == 1);

    const uint16_t firstOffset = static_cast<uint16_t>(dandb::core::PAGE_SIZE_BYTES-first.size());
    const uint16_t secondOffset = static_cast<uint16_t>(firstOffset-second.size());

    REQUIRE(page.slotCount() == 2);
    REQUIRE(page.freeStartOffset() == 32);
    REQUIRE(page.freeEndOffset() == secondOffset);
    REQUIRE(dandb::core::helper::readUint16(pageBytes, 24) == firstOffset);
    REQUIRE(dandb::core::helper::readUint16(pageBytes, 26) == first.size());
    REQUIRE(dandb::core::helper::readUint16(pageBytes, 28) == secondOffset);
    REQUIRE(dandb::core::helper::readUint16(pageBytes, 30) == second.size());

    REQUIRE(page.readRow(firstSlot.value()).value() == first);
    REQUIRE(page.readRow(secondSlot.value()).value() == second);

}

TEST_CASE("SlottedPage accepts the largest first row that exactly fits", "[record][slotted-page]") {

    std::array<std::byte, dandb::core::PAGE_SIZE_BYTES> pageBytes{};
    dandb::record::SlottedPage::initialize(pageBytes, 12);
    dandb::record::SlottedPage page(pageBytes);

    const size_t largestFirstRowSize = dandb::core::PAGE_SIZE_BYTES-24-4;
    const std::vector<std::byte> row(largestFirstRowSize, std::byte{0xCA});

    const auto slot = page.insertRow(row);
    const auto extraSlot = page.insertRow(bytes({ 1 }));

    REQUIRE(slot.ok());
    REQUIRE(slot.value() == 0);
    REQUIRE(page.slotCount() == 1);
    REQUIRE(page.freeStartOffset() == page.freeEndOffset());
    REQUIRE(page.readRow(slot.value()).value() == row);

    REQUIRE_FALSE(extraSlot.ok());
    REQUIRE(extraSlot.status().code() == dandb::core::StatusCode::InvalidArgument);

}

TEST_CASE("SlottedPage rejects an insert that does not fit without mutating the page", "[record][slotted-page]") {

    std::array<std::byte, dandb::core::PAGE_SIZE_BYTES> pageBytes{};
    dandb::record::SlottedPage::initialize(pageBytes, 13);
    dandb::record::SlottedPage page(pageBytes);

    const auto before = pageBytes;
    const std::vector<std::byte> tooLarge(dandb::core::PAGE_SIZE_BYTES, std::byte{0xEE});

    const auto slot = page.insertRow(tooLarge);

    REQUIRE_FALSE(slot.ok());
    REQUIRE(slot.status().code() == dandb::core::StatusCode::InvalidArgument);
    REQUIRE(pageBytes == before);

}

TEST_CASE("SlottedPage rejects reads and deletes for slots outside the slot directory", "[record][slotted-page]") {

    std::array<std::byte, dandb::core::PAGE_SIZE_BYTES> pageBytes{};
    dandb::record::SlottedPage::initialize(pageBytes, 14);
    dandb::record::SlottedPage page(pageBytes);

    const auto row = page.readRow(0);
    const auto deleted = page.deleteRow(0);

    REQUIRE_FALSE(row.ok());
    REQUIRE(row.status().code() == dandb::core::StatusCode::InvalidArgument);
    REQUIRE_FALSE(deleted.ok());
    REQUIRE(deleted.code() == dandb::core::StatusCode::InvalidArgument);

}

TEST_CASE("SlottedPage delete marks the slot as a tombstone and preserves the old row offset", "[record][slotted-page]") {

    std::array<std::byte, dandb::core::PAGE_SIZE_BYTES> pageBytes{};
    dandb::record::SlottedPage::initialize(pageBytes, 15);
    dandb::record::SlottedPage page(pageBytes);

    const auto row = bytes({ 90, 91, 92, 93 });
    const auto slot = page.insertRow(row);
    REQUIRE(slot.ok());

    const uint16_t oldOffset = dandb::core::helper::readUint16(pageBytes, 24);

    const auto deleteStatus = page.deleteRow(slot.value());
    const auto readDeleted = page.readRow(slot.value());
    const auto deleteAgain = page.deleteRow(slot.value());

    REQUIRE(deleteStatus.ok());
    REQUIRE(dandb::core::helper::readUint16(pageBytes, 24) == oldOffset);
    REQUIRE(dandb::core::helper::readUint16(pageBytes, 26) == 0);

    REQUIRE_FALSE(readDeleted.ok());
    REQUIRE(readDeleted.status().code() == dandb::core::StatusCode::NotFound);
    REQUIRE_FALSE(deleteAgain.ok());
    REQUIRE(deleteAgain.code() == dandb::core::StatusCode::NotFound);

}

TEST_CASE("SlottedPage updates a row in place when the new payload is not larger", "[record][slotted-page]") {

    std::array<std::byte, dandb::core::PAGE_SIZE_BYTES> pageBytes{};
    dandb::record::SlottedPage::initialize(pageBytes, 16);
    dandb::record::SlottedPage page(pageBytes);

    const auto slot = page.insertRow(bytes({ 1, 2, 3, 4 }));
    REQUIRE(slot.ok());

    const auto updated = bytes({ 9, 8 });
    const auto updateStatus = page.updateRow(slot.value(), updated);
    const auto stored = page.readRow(slot.value());

    REQUIRE(updateStatus.ok());
    REQUIRE(stored.ok());
    REQUIRE(stored.value() == updated);
    REQUIRE(dandb::core::helper::readUint16(pageBytes, 26) == updated.size());

}

TEST_CASE("SlottedPage updates a row with a larger payload when free space is available", "[record][slotted-page]") {

    std::array<std::byte, dandb::core::PAGE_SIZE_BYTES> pageBytes{};
    dandb::record::SlottedPage::initialize(pageBytes, 17);
    dandb::record::SlottedPage page(pageBytes);

    const auto firstSlot = page.insertRow(bytes({ 1, 1, 1 }));
    const auto secondSlot = page.insertRow(bytes({ 2, 2, 2 }));
    REQUIRE(firstSlot.ok());
    REQUIRE(secondSlot.ok());

    const auto larger = bytes({ 9, 9, 9, 9, 9, 9 });
    const auto updateStatus = page.updateRow(firstSlot.value(), larger);

    REQUIRE(updateStatus.ok());
    REQUIRE(page.readRow(firstSlot.value()).value() == larger);
    REQUIRE(page.readRow(secondSlot.value()).value() == bytes({ 2, 2, 2 }));
    REQUIRE(dandb::core::helper::readUint16(pageBytes, 26) == larger.size());

}

TEST_CASE("SlottedPage rejects an update that does not fit and preserves the original row", "[record][slotted-page]") {

    std::array<std::byte, dandb::core::PAGE_SIZE_BYTES> pageBytes{};
    dandb::record::SlottedPage::initialize(pageBytes, 18);
    dandb::record::SlottedPage page(pageBytes);

    const auto original = bytes({ 1, 2, 3, 4 });
    const auto slot = page.insertRow(original);
    REQUIRE(slot.ok());

    const auto before = pageBytes;
    const std::vector<std::byte> tooLarge(dandb::core::PAGE_SIZE_BYTES, std::byte{0xEF});

    const auto updateStatus = page.updateRow(slot.value(), tooLarge);

    REQUIRE_FALSE(updateStatus.ok());
    REQUIRE(updateStatus.code() == dandb::core::StatusCode::InvalidArgument);
    REQUIRE(pageBytes == before);
    REQUIRE(page.readRow(slot.value()).value() == original);

}

TEST_CASE("SlottedPage stores and updates the next table page id", "[record][slotted-page]") {

    std::array<std::byte, dandb::core::PAGE_SIZE_BYTES> pageBytes{};
    dandb::record::SlottedPage::initialize(pageBytes, 19);
    dandb::record::SlottedPage page(pageBytes);

    page.setNextPageId(33);

    REQUIRE(page.nextPageId() == 33);
    REQUIRE(dandb::core::helper::readUint32(pageBytes, 8) == 33);

}
