#include <dandb/core/Helper.h>
#include <dandb/storage/DiskManager.h>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

    class TempDir {
    public:
        explicit TempDir(std::string name)
            : path_(
                std::filesystem::temp_directory_path()/("dandb_"+std::move(name)+"_"+std::to_string(nextId_++))
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

    uint32_t readLittleEndian32(const std::filesystem::path& filePath, std::streamoff offset) {

        std::ifstream file(filePath, std::ios::binary);
        REQUIRE(file.is_open());

        std::array<std::byte, 4> bytes{};
        file.seekg(offset);
        file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        REQUIRE(file.gcount() == static_cast<std::streamsize>(bytes.size()));

        return dandb::core::helper::readUint32(bytes, 0);

    }

    std::array<char, 4> readMagic(const std::filesystem::path& filePath) {

        std::ifstream file(filePath, std::ios::binary);
        REQUIRE(file.is_open());

        std::array<char, 4> magic{};
        file.read(magic.data(), static_cast<std::streamsize>(magic.size()));
        REQUIRE(file.gcount() == static_cast<std::streamsize>(magic.size()));

        return magic;

    }

    std::array<std::byte, dandb::core::PAGE_SIZE_BYTES> makePage(std::byte seed) {

        std::array<std::byte, dandb::core::PAGE_SIZE_BYTES> page{};

        for(size_t index = 0; index < page.size(); index++) {
            page[index] = static_cast<std::byte>(
                (static_cast<unsigned int>(seed)+static_cast<unsigned int>(index))%256
            );
        }

        return page;

    }

}

TEST_CASE("New page file creates header page", "[storage][disk]") {

    const TempDir tempDir("disk_new_file_header");
    const auto filePath = tempDir.path()/"data.pages";

    const dandb::storage::DiskManager disk(filePath, { 'D', 'P', 'A', 'G' });

    REQUIRE(std::filesystem::exists(filePath));
    REQUIRE(std::filesystem::file_size(filePath) == dandb::core::PAGE_SIZE_BYTES);
    REQUIRE(readMagic(filePath) == std::array<char, 4>{ 'D', 'P', 'A', 'G' });
    REQUIRE(readLittleEndian32(filePath, 4) == dandb::core::PAGE_SIZE_BYTES);
    REQUIRE(readLittleEndian32(filePath, 8) == 1);
    REQUIRE(readLittleEndian32(filePath, 12) == static_cast<uint32_t>(dandb::core::INVALID_PAGE_ID));
    REQUIRE(readLittleEndian32(filePath, 16) == 0);
    REQUIRE(disk.pageCount() == 1);

}

TEST_CASE("Reopening page file loads existing header", "[storage][disk]") {

    const TempDir tempDir("disk_reopen_header");
    const auto filePath = tempDir.path()/"data.pages";

    {
        dandb::storage::DiskManager disk(filePath, { 'D', 'P', 'A', 'G' });

        const auto page1 = disk.allocatePage();
        REQUIRE(page1.ok());
        REQUIRE(page1.value() == dandb::core::FIRST_ALLOCATABLE_PAGE_ID);

        const auto page2 = disk.allocatePage();
        REQUIRE(page2.ok());
        REQUIRE(page2.value() == dandb::core::FIRST_ALLOCATABLE_PAGE_ID+1);
        REQUIRE(disk.pageCount() == 3);
    }

    const dandb::storage::DiskManager reopened(filePath, { 'D', 'P', 'A', 'G' });

    REQUIRE(reopened.pageCount() == 3);
    REQUIRE(readLittleEndian32(filePath, 8) == 3);

}

TEST_CASE("Allocate page returns sequential page ids and extends the file", "[storage][disk]") {
    const TempDir tempDir("disk_allocate_sequential");
    const auto filePath = tempDir.path()/"data.pages";
    dandb::storage::DiskManager disk(filePath, { 'D', 'P', 'A', 'G' });

    const auto page1 = disk.allocatePage();
    const auto page2 = disk.allocatePage();
    const auto page3 = disk.allocatePage();

    REQUIRE(page1.ok());
    REQUIRE(page2.ok());
    REQUIRE(page3.ok());
    REQUIRE(page1.value() == 1);
    REQUIRE(page2.value() == 2);
    REQUIRE(page3.value() == 3);
    REQUIRE(disk.pageCount() == 4);
    REQUIRE(std::filesystem::file_size(filePath) == dandb::core::PAGE_SIZE_BYTES*4);
    REQUIRE(readLittleEndian32(filePath, 8) == 4);
}

TEST_CASE("Write page and read page roundtrip exact bytes", "[storage][disk]") {
    const TempDir tempDir("disk_roundtrip_page");
    const auto filePath = tempDir.path()/"data.pages";
    dandb::storage::DiskManager disk(filePath, { 'D', 'P', 'A', 'G' });

    const auto allocatedPage = disk.allocatePage();
    REQUIRE(allocatedPage.ok());

    const auto expected = makePage(std::byte{ 17 });

    const auto writeStatus = disk.writePage(allocatedPage.value(), expected);
    REQUIRE(writeStatus.ok());

    std::array<std::byte, dandb::core::PAGE_SIZE_BYTES> actual{};
    const auto readStatus = disk.readPage(allocatedPage.value(), actual);

    REQUIRE(readStatus.ok());
    REQUIRE(actual == expected);
}

TEST_CASE("Written page bytes persist after reopening the file", "[storage][disk]") {
    const TempDir tempDir("disk_persist_page");
    const auto filePath = tempDir.path()/"data.pages";
    const auto expected = makePage(std::byte{ 91 });

    {
        dandb::storage::DiskManager disk(filePath, { 'D', 'P', 'A', 'G' });

        const auto allocatedPage = disk.allocatePage();
        REQUIRE(allocatedPage.ok());
        REQUIRE(disk.writePage(allocatedPage.value(), expected).ok());
    }

    dandb::storage::DiskManager reopened(filePath, { 'D', 'P', 'A', 'G' });

    std::array<std::byte, dandb::core::PAGE_SIZE_BYTES> actual{};
    const auto readStatus = reopened.readPage(dandb::core::FIRST_ALLOCATABLE_PAGE_ID, actual);

    REQUIRE(readStatus.ok());
    REQUIRE(actual == expected);
}

TEST_CASE("Reading unallocated page returns an error", "[storage][disk]") {
    const TempDir tempDir("disk_read_unallocated");
    const auto filePath = tempDir.path()/"data.pages";
    dandb::storage::DiskManager disk(filePath, { 'D', 'P', 'A', 'G' });

    std::array<std::byte, dandb::core::PAGE_SIZE_BYTES> page{};
    const auto status = disk.readPage(dandb::core::FIRST_ALLOCATABLE_PAGE_ID, page);

    REQUIRE_FALSE(status.ok());
    REQUIRE(status.code() == dandb::core::StatusCode::InvalidArgument);
}

TEST_CASE("Writing unallocated page returns an error", "[storage][disk]") {
    const TempDir tempDir("disk_write_unallocated");
    const auto filePath = tempDir.path()/"data.pages";
    dandb::storage::DiskManager disk(filePath, { 'D', 'P', 'A', 'G' });

    const auto page = makePage(std::byte{ 33 });
    const auto status = disk.writePage(dandb::core::FIRST_ALLOCATABLE_PAGE_ID, page);

    REQUIRE_FALSE(status.ok());
    REQUIRE(status.code() == dandb::core::StatusCode::InvalidArgument);
}

TEST_CASE("Page zero cannot be overwritten through writePage", "[storage][disk]") {
    const TempDir tempDir("disk_reject_page_zero_write");
    const auto filePath = tempDir.path()/"data.pages";
    dandb::storage::DiskManager disk(filePath, { 'D', 'P', 'A', 'G' });

    const auto page = makePage(std::byte{ 44 });
    const auto status = disk.writePage(0, page);

    REQUIRE_FALSE(status.ok());
    REQUIRE(readMagic(filePath) == std::array<char, 4>{ 'D', 'P', 'A', 'G' });
}

TEST_CASE("Free page is reused by the next allocation", "[storage][disk]") {
    const TempDir tempDir("disk_free_reuse");
    const auto filePath = tempDir.path()/"data.pages";
    dandb::storage::DiskManager disk(filePath, { 'D', 'P', 'A', 'G' });

    const auto page1 = disk.allocatePage();
    const auto page2 = disk.allocatePage();
    REQUIRE(page1.ok());
    REQUIRE(page2.ok());
    REQUIRE(page1.value() == 1);
    REQUIRE(page2.value() == 2);

    const auto freeStatus = disk.freePage(page1.value());
    REQUIRE(freeStatus.ok());
    REQUIRE(readLittleEndian32(filePath, 12) == static_cast<uint32_t>(page1.value()));
    REQUIRE(readLittleEndian32(filePath, 16) == 1);

    const auto reusedPage = disk.allocatePage();
    REQUIRE(reusedPage.ok());
    REQUIRE(reusedPage.value() == page1.value());
    REQUIRE(disk.pageCount() == 3);
    REQUIRE(readLittleEndian32(filePath, 12) == static_cast<uint32_t>(dandb::core::INVALID_PAGE_ID));
    REQUIRE(readLittleEndian32(filePath, 16) == 0);
}

TEST_CASE("Freeing invalid page id returns an error", "[storage][disk]") {
    const TempDir tempDir("disk_free_invalid");
    const auto filePath = tempDir.path()/"data.pages";
    dandb::storage::DiskManager disk(filePath, { 'D', 'P', 'A', 'G' });

    REQUIRE_FALSE(disk.freePage(0).ok());
    REQUIRE_FALSE(disk.freePage(dandb::core::INVALID_PAGE_ID).ok());
    REQUIRE_FALSE(disk.freePage(dandb::core::FIRST_ALLOCATABLE_PAGE_ID).ok());
}

TEST_CASE("Different magic bytes are stored for different page files", "[storage][disk]") {
    const TempDir tempDir("disk_magic");
    const auto dataPath = tempDir.path()/"data.pages";
    const auto indexPath = tempDir.path()/"index.pages";

    const dandb::storage::DiskManager dataDisk(dataPath, { 'D', 'P', 'A', 'G' });
    const dandb::storage::DiskManager indexDisk(indexPath, { 'I', 'P', 'A', 'G' });

    REQUIRE(readMagic(dataPath) == std::array<char, 4>{ 'D', 'P', 'A', 'G' });
    REQUIRE(readMagic(indexPath) == std::array<char, 4>{ 'I', 'P', 'A', 'G' });
    REQUIRE(dataDisk.pageCount() == 1);
    REQUIRE(indexDisk.pageCount() == 1);
}
