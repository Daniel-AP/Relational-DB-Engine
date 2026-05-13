#include <dandb/buffer/BufferPoolManager.h>
#include <dandb/core/Constants.h>
#include <dandb/core/Status.h>
#include <dandb/storage/DiskManager.h>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <filesystem>
#include <string>

namespace {

    class TempDir {
        public:
            explicit TempDir(std::string name)
                : path_(
                    std::filesystem::temp_directory_path()/("dandb_buffer_"+std::move(name)+"_"+std::to_string(nextId_++))
                ) {

                std::filesystem::create_directories(path_);

            }

            ~TempDir() {

                std::error_code ignored;
                std::filesystem::remove_all(path_, ignored);

            }

            const std::filesystem::path& path() const {

                return path_;

            }

        private:
            std::filesystem::path path_;
            inline static int nextId_ = 0;
    };

    std::array<std::byte, dandb::core::PAGE_SIZE_BYTES> makePage(std::byte seed) {

        std::array<std::byte, dandb::core::PAGE_SIZE_BYTES> page{};

        for(std::size_t index = 0; index < page.size(); index++) {
            page[index] = static_cast<std::byte>(
                (static_cast<unsigned int>(seed)+static_cast<unsigned int>(index))%256
            );
        }

        return page;

    }

    bool isZeroed(const std::array<std::byte, dandb::core::PAGE_SIZE_BYTES>& data) {

        for(const auto byte : data) {
            if(byte != std::byte{ 0 }) {
                return false;
            }
        }

        return true;

    }

}

TEST_CASE("BufferPoolManager reports its fixed pool size", "[buffer][bpm]") {

    const TempDir tempDir("pool_size");
    const auto filePath = tempDir.path()/"data.pages";
    dandb::storage::DiskManager disk(filePath, { 'D', 'P', 'A', 'G' });
    const dandb::buffer::BufferPoolManager bufferPool(3, disk);

    REQUIRE(bufferPool.getPoolSize() == 3);

}

TEST_CASE("newPage allocates a disk page and returns it pinned in memory", "[buffer][bpm]") {

    const TempDir tempDir("new_page");
    const auto filePath = tempDir.path()/"data.pages";
    dandb::storage::DiskManager disk(filePath, { 'D', 'P', 'A', 'G' });
    dandb::buffer::BufferPoolManager bufferPool(2, disk);

    const auto pageResult = bufferPool.newPage();

    REQUIRE(pageResult.ok());

    auto* page = pageResult.value();

    REQUIRE(page != nullptr);
    REQUIRE(page->pageId() == dandb::core::FIRST_ALLOCATABLE_PAGE_ID);
    REQUIRE(page->pinCount() == 1);
    REQUIRE_FALSE(page->isDirty());
    REQUIRE(isZeroed(page->data()));
    REQUIRE(disk.pageCount() == 2);

}

TEST_CASE("fetchPage reads an existing disk page into a pinned buffer frame", "[buffer][bpm]") {

    const TempDir tempDir("fetch_existing");
    const auto filePath = tempDir.path()/"data.pages";
    dandb::storage::DiskManager disk(filePath, { 'D', 'P', 'A', 'G' });
    dandb::buffer::BufferPoolManager bufferPool(2, disk);

    const auto allocatedPage = disk.allocatePage();
    REQUIRE(allocatedPage.ok());

    const auto expected = makePage(std::byte{ 17 });
    REQUIRE(disk.writePage(allocatedPage.value(), expected).ok());

    const auto pageResult = bufferPool.fetchPage(allocatedPage.value());

    REQUIRE(pageResult.ok());

    auto* page = pageResult.value();

    REQUIRE(page != nullptr);
    REQUIRE(page->pageId() == allocatedPage.value());
    REQUIRE(page->pinCount() == 1);
    REQUIRE_FALSE(page->isDirty());
    REQUIRE(page->data() == expected);

}

TEST_CASE("fetchPage returns the cached page and increments its pin count", "[buffer][bpm]") {

    const TempDir tempDir("fetch_cached");
    const auto filePath = tempDir.path()/"data.pages";
    dandb::storage::DiskManager disk(filePath, { 'D', 'P', 'A', 'G' });
    dandb::buffer::BufferPoolManager bufferPool(2, disk);

    const auto allocatedPage = disk.allocatePage();
    REQUIRE(allocatedPage.ok());

    const auto firstFetch = bufferPool.fetchPage(allocatedPage.value());
    const auto secondFetch = bufferPool.fetchPage(allocatedPage.value());

    REQUIRE(firstFetch.ok());
    REQUIRE(secondFetch.ok());
    REQUIRE(firstFetch.value() == secondFetch.value());
    REQUIRE(firstFetch.value()->pinCount() == 2);

}

TEST_CASE("unpinPage decrements pin count and records dirty changes", "[buffer][bpm]") {

    const TempDir tempDir("unpin_dirty");
    const auto filePath = tempDir.path()/"data.pages";
    dandb::storage::DiskManager disk(filePath, { 'D', 'P', 'A', 'G' });
    dandb::buffer::BufferPoolManager bufferPool(2, disk);

    const auto pageResult = bufferPool.newPage();
    REQUIRE(pageResult.ok());

    auto* page = pageResult.value();
    page->data()[0] = std::byte{ 99 };

    const auto unpinStatus = bufferPool.unpinPage(page->pageId(), true);

    REQUIRE(unpinStatus.ok());
    REQUIRE(page->pinCount() == 0);
    REQUIRE(page->isDirty());

}

TEST_CASE("unpinPage rejects pages that are not loaded", "[buffer][bpm]") {

    const TempDir tempDir("unpin_missing");
    const auto filePath = tempDir.path()/"data.pages";
    dandb::storage::DiskManager disk(filePath, { 'D', 'P', 'A', 'G' });
    dandb::buffer::BufferPoolManager bufferPool(2, disk);

    const auto status = bufferPool.unpinPage(dandb::core::FIRST_ALLOCATABLE_PAGE_ID, false);

    REQUIRE_FALSE(status.ok());
    REQUIRE(status.code() == dandb::core::StatusCode::NotFound);

}

TEST_CASE("unpinPage rejects a loaded page whose pin count is already zero", "[buffer][bpm]") {

    const TempDir tempDir("unpin_zero");
    const auto filePath = tempDir.path()/"data.pages";
    dandb::storage::DiskManager disk(filePath, { 'D', 'P', 'A', 'G' });
    dandb::buffer::BufferPoolManager bufferPool(2, disk);

    const auto pageResult = bufferPool.newPage();
    REQUIRE(pageResult.ok());

    const auto pageId = pageResult.value()->pageId();
    REQUIRE(bufferPool.unpinPage(pageId, false).ok());

    const auto secondUnpin = bufferPool.unpinPage(pageId, false);

    REQUIRE_FALSE(secondUnpin.ok());
    REQUIRE(secondUnpin.code() == dandb::core::StatusCode::InvalidArgument);

}

TEST_CASE("Dirty page is flushed before its frame is evicted", "[buffer][bpm]") {

    const TempDir tempDir("dirty_eviction");
    const auto filePath = tempDir.path()/"data.pages";
    dandb::storage::DiskManager disk(filePath, { 'D', 'P', 'A', 'G' });
    dandb::buffer::BufferPoolManager bufferPool(1, disk);

    const auto firstPageResult = bufferPool.newPage();
    REQUIRE(firstPageResult.ok());

    auto* firstPage = firstPageResult.value();
    const auto firstPageId = firstPage->pageId();
    const auto expected = makePage(std::byte{ 42 });
    firstPage->data() = expected;

    REQUIRE(bufferPool.unpinPage(firstPageId, true).ok());

    const auto secondPageResult = bufferPool.newPage();

    REQUIRE(secondPageResult.ok());
    REQUIRE(secondPageResult.value()->pageId() != firstPageId);

    std::array<std::byte, dandb::core::PAGE_SIZE_BYTES> actual{};
    REQUIRE(disk.readPage(firstPageId, actual).ok());
    REQUIRE(actual == expected);

}

TEST_CASE("All pinned pages make fetchPage return an error when the pool is full", "[buffer][bpm]") {

    const TempDir tempDir("fetch_all_pinned");
    const auto filePath = tempDir.path()/"data.pages";
    dandb::storage::DiskManager disk(filePath, { 'D', 'P', 'A', 'G' });
    dandb::buffer::BufferPoolManager bufferPool(1, disk);

    const auto firstPage = disk.allocatePage();
    const auto secondPage = disk.allocatePage();
    REQUIRE(firstPage.ok());
    REQUIRE(secondPage.ok());

    REQUIRE(bufferPool.fetchPage(firstPage.value()).ok());

    const auto secondFetch = bufferPool.fetchPage(secondPage.value());

    REQUIRE_FALSE(secondFetch.ok());
    REQUIRE(secondFetch.status().code() == dandb::core::StatusCode::Internal);

}

TEST_CASE("fetchPage keeps the evicted page cached when reading the replacement fails", "[buffer][bpm]") {

    const TempDir tempDir("fetch_rollback");
    const auto filePath = tempDir.path()/"data.pages";
    dandb::storage::DiskManager disk(filePath, { 'D', 'P', 'A', 'G' });
    dandb::buffer::BufferPoolManager bufferPool(1, disk);

    const auto cachedPageResult = bufferPool.newPage();
    REQUIRE(cachedPageResult.ok());

    auto* cachedPage = cachedPageResult.value();
    const auto cachedPageId = cachedPage->pageId();
    const auto expected = makePage(std::byte{ 13 });
    cachedPage->data() = expected;

    REQUIRE(bufferPool.unpinPage(cachedPageId, false).ok());

    const auto missingFetch = bufferPool.fetchPage(cachedPageId+1);

    REQUIRE_FALSE(missingFetch.ok());

    const auto cachedFetch = bufferPool.fetchPage(cachedPageId);

    REQUIRE(cachedFetch.ok());
    REQUIRE(cachedFetch.value() == cachedPage);
    REQUIRE(cachedFetch.value()->pageId() == cachedPageId);
    REQUIRE(cachedFetch.value()->pinCount() == 1);
    REQUIRE(cachedFetch.value()->data() == expected);

}

TEST_CASE("All pinned pages make newPage return an error when the pool is full", "[buffer][bpm]") {

    const TempDir tempDir("new_all_pinned");
    const auto filePath = tempDir.path()/"data.pages";
    dandb::storage::DiskManager disk(filePath, { 'D', 'P', 'A', 'G' });
    dandb::buffer::BufferPoolManager bufferPool(1, disk);

    REQUIRE(bufferPool.newPage().ok());

    const auto secondPage = bufferPool.newPage();

    REQUIRE_FALSE(secondPage.ok());
    REQUIRE(secondPage.status().code() == dandb::core::StatusCode::Internal);

}

TEST_CASE("savePageToDisk writes one dirty page and keeps other dirty pages in memory only", "[buffer][bpm]") {

    const TempDir tempDir("save_one");
    const auto filePath = tempDir.path()/"data.pages";
    dandb::storage::DiskManager disk(filePath, { 'D', 'P', 'A', 'G' });
    dandb::buffer::BufferPoolManager bufferPool(2, disk);

    const auto firstPageResult = bufferPool.newPage();
    const auto secondPageResult = bufferPool.newPage();
    REQUIRE(firstPageResult.ok());
    REQUIRE(secondPageResult.ok());

    auto* firstPage = firstPageResult.value();
    auto* secondPage = secondPageResult.value();
    const auto firstPageId = firstPage->pageId();
    const auto secondPageId = secondPage->pageId();
    const auto firstExpected = makePage(std::byte{ 8 });
    const auto secondExpected = makePage(std::byte{ 9 });

    firstPage->data() = firstExpected;
    secondPage->data() = secondExpected;

    REQUIRE(bufferPool.unpinPage(firstPageId, true).ok());
    REQUIRE(bufferPool.unpinPage(secondPageId, true).ok());

    const auto saveStatus = bufferPool.savePageToDisk(firstPageId);

    REQUIRE(saveStatus.ok());
    REQUIRE_FALSE(firstPage->isDirty());
    REQUIRE(secondPage->isDirty());

    std::array<std::byte, dandb::core::PAGE_SIZE_BYTES> firstActual{};
    std::array<std::byte, dandb::core::PAGE_SIZE_BYTES> secondActual{};
    REQUIRE(disk.readPage(firstPageId, firstActual).ok());
    REQUIRE(disk.readPage(secondPageId, secondActual).ok());
    REQUIRE(firstActual == firstExpected);
    REQUIRE(secondActual != secondExpected);

}

TEST_CASE("savePageToDisk rejects pages that are not loaded", "[buffer][bpm]") {

    const TempDir tempDir("save_missing");
    const auto filePath = tempDir.path()/"data.pages";
    dandb::storage::DiskManager disk(filePath, { 'D', 'P', 'A', 'G' });
    dandb::buffer::BufferPoolManager bufferPool(2, disk);

    const auto status = bufferPool.savePageToDisk(dandb::core::FIRST_ALLOCATABLE_PAGE_ID);

    REQUIRE_FALSE(status.ok());
    REQUIRE(status.code() == dandb::core::StatusCode::NotFound);

}

TEST_CASE("saveAllPagesToDisk writes every dirty loaded page", "[buffer][bpm]") {

    const TempDir tempDir("save_all");
    const auto filePath = tempDir.path()/"data.pages";
    dandb::storage::DiskManager disk(filePath, { 'D', 'P', 'A', 'G' });
    dandb::buffer::BufferPoolManager bufferPool(2, disk);

    const auto firstPageResult = bufferPool.newPage();
    const auto secondPageResult = bufferPool.newPage();
    REQUIRE(firstPageResult.ok());
    REQUIRE(secondPageResult.ok());

    auto* firstPage = firstPageResult.value();
    auto* secondPage = secondPageResult.value();
    const auto firstPageId = firstPage->pageId();
    const auto secondPageId = secondPage->pageId();
    const auto firstExpected = makePage(std::byte{ 51 });
    const auto secondExpected = makePage(std::byte{ 77 });

    firstPage->data() = firstExpected;
    secondPage->data() = secondExpected;

    REQUIRE(bufferPool.unpinPage(firstPageId, true).ok());
    REQUIRE(bufferPool.unpinPage(secondPageId, true).ok());

    const auto saveStatus = bufferPool.saveAllPagesToDisk();

    REQUIRE(saveStatus.ok());
    REQUIRE_FALSE(firstPage->isDirty());
    REQUIRE_FALSE(secondPage->isDirty());

    std::array<std::byte, dandb::core::PAGE_SIZE_BYTES> firstActual{};
    std::array<std::byte, dandb::core::PAGE_SIZE_BYTES> secondActual{};
    REQUIRE(disk.readPage(firstPageId, firstActual).ok());
    REQUIRE(disk.readPage(secondPageId, secondActual).ok());
    REQUIRE(firstActual == firstExpected);
    REQUIRE(secondActual == secondExpected);

}
