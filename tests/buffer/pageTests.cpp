#include <dandb/buffer/Page.h>
#include <dandb/core/Constants.h>
#include <dandb/core/Status.h>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>

namespace {

    bool isZeroed(const std::array<std::byte, dandb::core::PAGE_SIZE_BYTES>& data) {

        for(const auto byte : data) {
            if(byte != std::byte{ 0 }) {
                return false;
            }
        }

        return true;

    }

}

TEST_CASE("New Page starts invalid, unpinned, clean, and zeroed", "[buffer][page]") {

    const dandb::buffer::Page page;

    REQUIRE(page.pageId() == dandb::core::INVALID_PAGE_ID);
    REQUIRE(page.pinCount() == 0);
    REQUIRE_FALSE(page.isDirty());
    REQUIRE(page.data().size() == dandb::core::PAGE_SIZE_BYTES);
    REQUIRE(isZeroed(page.data()));

}

TEST_CASE("Page exposes a mutable page-sized byte buffer", "[buffer][page]") {

    dandb::buffer::Page page;

    page.data()[0] = std::byte{ 11 };
    page.data()[128] = std::byte{ 22 };
    page.data()[dandb::core::PAGE_SIZE_BYTES-1] = std::byte{ 33 };

    REQUIRE(page.data().size() == dandb::core::PAGE_SIZE_BYTES);
    REQUIRE(page.data()[0] == std::byte{ 11 });
    REQUIRE(page.data()[128] == std::byte{ 22 });
    REQUIRE(page.data()[dandb::core::PAGE_SIZE_BYTES-1] == std::byte{ 33 });

}

TEST_CASE("Page metadata can be assigned independently from page bytes", "[buffer][page]") {

    dandb::buffer::Page page;

    page.data()[0] = std::byte{ 44 };
    page.setPageId(7);
    page.setDirty(true);

    REQUIRE(page.pageId() == 7);
    REQUIRE(page.isDirty());
    REQUIRE(page.data()[0] == std::byte{ 44 });

    page.setDirty(false);

    REQUIRE_FALSE(page.isDirty());
    REQUIRE(page.pageId() == 7);
    REQUIRE(page.data()[0] == std::byte{ 44 });

}

TEST_CASE("Pinning a Page increments the pin count", "[buffer][page]") {

    dandb::buffer::Page page;

    page.pin();
    page.pin();

    REQUIRE(page.pinCount() == 2);

}

TEST_CASE("Unpinning a pinned Page decrements the pin count", "[buffer][page]") {

    dandb::buffer::Page page;

    page.pin();
    page.pin();

    const auto firstUnpin = page.unpin();
    const auto secondUnpin = page.unpin();

    REQUIRE(firstUnpin.ok());
    REQUIRE(secondUnpin.ok());
    REQUIRE(page.pinCount() == 0);

}

TEST_CASE("Unpinning an unpinned Page returns an error and does not underflow", "[buffer][page]") {

    dandb::buffer::Page page;

    const auto status = page.unpin();

    REQUIRE_FALSE(status.ok());
    REQUIRE(status.code() == dandb::core::StatusCode::InvalidArgument);
    REQUIRE(page.pinCount() == 0);

}

TEST_CASE("Reset clears Page metadata and bytes", "[buffer][page]") {

    dandb::buffer::Page page;

    page.setPageId(42);
    page.setDirty(true);
    page.pin();
    page.pin();
    page.data()[0] = std::byte{ 1 };
    page.data()[dandb::core::PAGE_SIZE_BYTES-1] = std::byte{ 2 };

    page.reset();

    REQUIRE(page.pageId() == dandb::core::INVALID_PAGE_ID);
    REQUIRE(page.pinCount() == 0);
    REQUIRE_FALSE(page.isDirty());
    REQUIRE(isZeroed(page.data()));

}
