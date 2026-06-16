#include <dandb/buffer/BufferPoolManager.h>
#include <dandb/catalog/TableManager.h>
#include <dandb/core/Constants.h>
#include <dandb/core/Helper.h>
#include <dandb/core/Status.h>
#include <dandb/record/Layout.h>
#include <dandb/record/RID.h>
#include <dandb/record/Row.h>
#include <dandb/record/Schema.h>
#include <dandb/record/SlottedPage.h>
#include <dandb/record/TableIterator.h>
#include <dandb/record/Value.h>
#include <dandb/storage/DiskManager.h>

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace {

    constexpr uint32_t FIRST_TABLE_ID = 1;

    constexpr uint8_t TYPE_BOOLEAN = 0x01;
    constexpr uint8_t TYPE_INT32 = 0x03;
    constexpr uint8_t TYPE_STRING = 0x06;

    constexpr uint8_t FLAG_NULLABLE = 0x01;
    constexpr uint8_t FLAG_PRIMARY_KEY = 0x02;

    class TempDir {
        public:
            explicit TempDir(std::string name)
                : path_(
                    std::filesystem::temp_directory_path()/("dandb_table_manager_"+std::move(name)+"_"+std::to_string(nextId_++))
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

    struct ParsedColumn {
        uint8_t typeCode;
        uint8_t flags;
        uint16_t stringCapacity;
        std::string name;
    };

    struct ParsedTable {
        uint32_t tableId;
        dandb::core::PageId firstPageId;
        dandb::core::PageId lastPageId;
        uint8_t columnCount;
        std::string name;
        std::vector<ParsedColumn> columns;
    };

    struct ParsedTablesMeta {
        uint32_t nextTableId;
        uint32_t tableCount;
        std::vector<ParsedTable> tables;
    };

    std::vector<std::byte> readFileBytes(const std::filesystem::path& filePath) {

        std::ifstream file(filePath, std::ios::binary);
        REQUIRE(file.is_open());

        file.seekg(0, std::ios::end);
        const auto fileSize = file.tellg();
        REQUIRE(fileSize >= 0);

        std::vector<std::byte> bytes(static_cast<size_t>(fileSize));

        file.seekg(0, std::ios::beg);
        file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        REQUIRE(file.gcount() == static_cast<std::streamsize>(bytes.size()));

        return bytes;

    }

    void writeFileBytes(const std::filesystem::path& filePath, std::initializer_list<unsigned int> values) {

        std::ofstream file(filePath, std::ios::binary | std::ios::trunc);
        REQUIRE(file.is_open());

        for(const auto value: values) {
            const auto byte = static_cast<std::byte>(value);
            file.write(reinterpret_cast<const char*>(&byte), 1);
        }

        REQUIRE(file.good());

    }

    std::string readText(const std::vector<std::byte>& bytes, size_t& offset) {

        REQUIRE(offset+2 <= bytes.size());

        const uint16_t length = dandb::core::helper::readUint16(bytes, offset);
        offset += 2;

        REQUIRE(offset+length <= bytes.size());

        std::string text;
        text.reserve(length);

        for(size_t i = 0; i < length; i++) {
            text.push_back(static_cast<char>(bytes[offset+i]));
        }

        offset += length;

        const size_t alignedOffset = dandb::record::layout::alignTo(offset, 4);
        REQUIRE(alignedOffset <= bytes.size());

        for(size_t i = offset; i < alignedOffset; i++) {
            REQUIRE(bytes[i] == std::byte{0});
        }

        offset = alignedOffset;

        return text;

    }

    ParsedTablesMeta readTablesMeta(const std::filesystem::path& filePath) {

        const auto bytes = readFileBytes(filePath);

        REQUIRE(bytes.size() >= 16);
        REQUIRE(bytes[0] == std::byte{'D'});
        REQUIRE(bytes[1] == std::byte{'T'});
        REQUIRE(bytes[2] == std::byte{'B'});
        REQUIRE(bytes[3] == std::byte{'L'});
        REQUIRE(dandb::core::helper::readUint32(bytes, 12) == 0);

        ParsedTablesMeta meta{
            dandb::core::helper::readUint32(bytes, 4),
            dandb::core::helper::readUint32(bytes, 8),
            {}
        };

        size_t offset = 16;

        for(uint32_t i = 0; i < meta.tableCount; i++) {
            REQUIRE(offset+16 <= bytes.size());

            ParsedTable table{
                dandb::core::helper::readUint32(bytes, offset),
                dandb::core::helper::readUint32(bytes, offset+4),
                dandb::core::helper::readUint32(bytes, offset+8),
                std::to_integer<uint8_t>(bytes[offset+12]),
                "",
                {}
            };

            REQUIRE(bytes[offset+13] == std::byte{0});
            REQUIRE(bytes[offset+14] == std::byte{0});
            REQUIRE(bytes[offset+15] == std::byte{0});

            offset += 16;
            table.name = readText(bytes, offset);

            for(uint8_t j = 0; j < table.columnCount; j++) {
                REQUIRE(offset+4 <= bytes.size());

                ParsedColumn column{
                    std::to_integer<uint8_t>(bytes[offset]),
                    std::to_integer<uint8_t>(bytes[offset+1]),
                    dandb::core::helper::readUint16(bytes, offset+2),
                    ""
                };

                offset += 4;
                column.name = readText(bytes, offset);
                table.columns.push_back(std::move(column));
            }

            const size_t alignedOffset = dandb::record::layout::alignTo(offset, 8);
            REQUIRE(alignedOffset <= bytes.size());

            for(size_t j = offset; j < alignedOffset; j++) {
                REQUIRE(bytes[j] == std::byte{0});
            }

            offset = alignedOffset;
            meta.tables.push_back(std::move(table));
        }

        REQUIRE(offset == bytes.size());

        return meta;

    }

    dandb::record::Schema userSchema() {

        auto schema = dandb::record::Schema::create({
            dandb::record::Column{ "id", dandb::record::LogicalType::Int32, false, true, 0 },
            dandb::record::Column{ "active", dandb::record::LogicalType::Boolean, false, false, 0 },
            dandb::record::Column{ "nickname", dandb::record::LogicalType::String, true, false, 16 }
        });

        REQUIRE(schema.ok());

        return schema.value();

    }

    dandb::record::Schema largeRowSchema() {

        auto schema = dandb::record::Schema::create({
            dandb::record::Column{ "id", dandb::record::LogicalType::Int32, false, true, 0 },
            dandb::record::Column{ "payload", dandb::record::LogicalType::String, false, false, 512 }
        });

        REQUIRE(schema.ok());

        return schema.value();

    }

    dandb::record::Row userRow(int32_t id, bool active, std::string nickname) {

        return dandb::record::Row({
            dandb::record::Value::int32(id),
            dandb::record::Value::boolean(active),
            dandb::record::Value::string(std::move(nickname))
        });

    }

    dandb::record::Row largeRow(int32_t id) {

        return dandb::record::Row({
            dandb::record::Value::int32(id),
            dandb::record::Value::string("row_"+std::to_string(id))
        });

    }

    void requireUserRow(const dandb::record::Row& row, int32_t id, bool active, const std::string& nickname) {

        REQUIRE(row.valueCount() == 3);
        REQUIRE(row.value(0).asInt32() == id);
        REQUIRE(row.value(1).asBoolean() == active);
        REQUIRE(row.value(2).asString() == nickname);

    }

    void requireColumn(
        const ParsedColumn& column,
        const std::string& name,
        uint8_t typeCode,
        uint8_t flags,
        uint16_t stringCapacity
    ) {

        REQUIRE(column.name == name);
        REQUIRE(column.typeCode == typeCode);
        REQUIRE(column.flags == flags);
        REQUIRE(column.stringCapacity == stringCapacity);

    }

}

TEST_CASE("TableManager open initializes an empty tables.meta file", "[catalog][table-manager]") {

    const TempDir tempDir("open_empty");
    dandb::storage::DiskManager disk(tempDir.path()/"data.pages", { 'D', 'P', 'A', 'G' });
    dandb::buffer::BufferPoolManager bufferPool(2, disk);

    const auto manager = dandb::catalog::TableManager::open(tempDir.path()/"tables.meta", bufferPool);

    REQUIRE(manager.ok());

    const auto meta = readTablesMeta(tempDir.path()/"tables.meta");

    REQUIRE(meta.nextTableId == FIRST_TABLE_ID);
    REQUIRE(meta.tableCount == 0);
    REQUIRE(meta.tables.empty());
    REQUIRE(disk.pageCount() == 1);

}

TEST_CASE("TableManager createTable persists schema metadata and initializes the first table page", "[catalog][table-manager]") {

    const TempDir tempDir("create_table");
    dandb::storage::DiskManager disk(tempDir.path()/"data.pages", { 'D', 'P', 'A', 'G' });
    dandb::buffer::BufferPoolManager bufferPool(2, disk);

    auto manager = dandb::catalog::TableManager::open(tempDir.path()/"tables.meta", bufferPool);
    REQUIRE(manager.ok());

    const auto createStatus = manager.value().createTable("users", userSchema());

    REQUIRE(createStatus.ok());

    const auto meta = readTablesMeta(tempDir.path()/"tables.meta");

    REQUIRE(meta.nextTableId == FIRST_TABLE_ID+1);
    REQUIRE(meta.tableCount == 1);
    REQUIRE(meta.tables.size() == 1);

    const auto& table = meta.tables[0];

    REQUIRE(table.tableId == FIRST_TABLE_ID);
    REQUIRE(table.name == "users");
    REQUIRE(table.firstPageId == dandb::core::FIRST_ALLOCATABLE_PAGE_ID);
    REQUIRE(table.lastPageId == table.firstPageId);
    REQUIRE(table.columnCount == 3);
    REQUIRE(table.columns.size() == 3);
    requireColumn(table.columns[0], "id", TYPE_INT32, FLAG_PRIMARY_KEY, 0);
    requireColumn(table.columns[1], "active", TYPE_BOOLEAN, 0, 0);
    requireColumn(table.columns[2], "nickname", TYPE_STRING, FLAG_NULLABLE, 16);

    const auto pageResult = bufferPool.fetchPage(table.firstPageId);
    REQUIRE(pageResult.ok());

    const dandb::record::SlottedPage firstPage(pageResult.value()->data());

    REQUIRE(firstPage.pageId() == table.firstPageId);
    REQUIRE(firstPage.nextPageId() == dandb::core::INVALID_PAGE_ID);
    REQUIRE(firstPage.slotCount() == 0);
    REQUIRE(firstPage.freeStartOffset() == 24);
    REQUIRE(firstPage.freeEndOffset() == dandb::core::PAGE_SIZE_BYTES);
    REQUIRE(bufferPool.unpinPage(table.firstPageId, false).ok());

}

TEST_CASE("TableManager createTable rejects duplicate table names without changing metadata", "[catalog][table-manager]") {

    const TempDir tempDir("duplicate_table");
    dandb::storage::DiskManager disk(tempDir.path()/"data.pages", { 'D', 'P', 'A', 'G' });
    dandb::buffer::BufferPoolManager bufferPool(2, disk);

    auto manager = dandb::catalog::TableManager::open(tempDir.path()/"tables.meta", bufferPool);
    REQUIRE(manager.ok());
    REQUIRE(manager.value().createTable("users", userSchema()).ok());

    const auto beforeBytes = readFileBytes(tempDir.path()/"tables.meta");
    const auto pageCountBefore = disk.pageCount();

    const auto duplicateStatus = manager.value().createTable("users", userSchema());

    REQUIRE_FALSE(duplicateStatus.ok());
    REQUIRE(duplicateStatus.code() == dandb::core::StatusCode::AlreadyExists);
    REQUIRE(readFileBytes(tempDir.path()/"tables.meta") == beforeBytes);
    REQUIRE(disk.pageCount() == pageCountBefore);

}

TEST_CASE("TableManager createTable rejects empty table names without allocating a page", "[catalog][table-manager]") {

    const TempDir tempDir("empty_table_name");
    dandb::storage::DiskManager disk(tempDir.path()/"data.pages", { 'D', 'P', 'A', 'G' });
    dandb::buffer::BufferPoolManager bufferPool(2, disk);

    auto manager = dandb::catalog::TableManager::open(tempDir.path()/"tables.meta", bufferPool);
    REQUIRE(manager.ok());

    const auto status = manager.value().createTable("", userSchema());

    REQUIRE_FALSE(status.ok());
    REQUIRE(status.code() == dandb::core::StatusCode::InvalidArgument);

    const auto meta = readTablesMeta(tempDir.path()/"tables.meta");

    REQUIRE(meta.nextTableId == FIRST_TABLE_ID);
    REQUIRE(meta.tableCount == 0);
    REQUIRE(disk.pageCount() == 1);

}

TEST_CASE("TableManager operations return NotFound for missing tables", "[catalog][table-manager]") {

    const TempDir tempDir("missing_table");
    dandb::storage::DiskManager disk(tempDir.path()/"data.pages", { 'D', 'P', 'A', 'G' });
    dandb::buffer::BufferPoolManager bufferPool(2, disk);

    auto manager = dandb::catalog::TableManager::open(tempDir.path()/"tables.meta", bufferPool);
    REQUIRE(manager.ok());

    const dandb::record::RID rid{ dandb::core::FIRST_ALLOCATABLE_PAGE_ID, 0 };

    const auto insertResult = manager.value().insertRow("missing", userRow(1, true, "Ana"));
    const auto readResult = manager.value().readRow("missing", rid);
    const auto updateStatus = manager.value().updateRow("missing", rid, userRow(1, false, "Luis"));
    const auto deleteStatus = manager.value().deleteRow("missing", rid);
    const auto scanResult = manager.value().scan("missing");

    REQUIRE_FALSE(insertResult.ok());
    REQUIRE(insertResult.status().code() == dandb::core::StatusCode::NotFound);
    REQUIRE_FALSE(readResult.ok());
    REQUIRE(readResult.status().code() == dandb::core::StatusCode::NotFound);
    REQUIRE_FALSE(updateStatus.ok());
    REQUIRE(updateStatus.code() == dandb::core::StatusCode::NotFound);
    REQUIRE_FALSE(deleteStatus.ok());
    REQUIRE(deleteStatus.code() == dandb::core::StatusCode::NotFound);
    REQUIRE_FALSE(scanResult.ok());
    REQUIRE(scanResult.status().code() == dandb::core::StatusCode::NotFound);
    REQUIRE(disk.pageCount() == 1);

}

TEST_CASE("TableManager inserts rows and reloads metadata after restart", "[catalog][table-manager]") {

    const TempDir tempDir("reload_inserted_row");
    const auto tablesMetaPath = tempDir.path()/"tables.meta";
    const auto dataPath = tempDir.path()/"data.pages";
    dandb::record::RID rid;

    {
        dandb::storage::DiskManager disk(dataPath, { 'D', 'P', 'A', 'G' });
        dandb::buffer::BufferPoolManager bufferPool(2, disk);

        auto manager = dandb::catalog::TableManager::open(tablesMetaPath, bufferPool);
        REQUIRE(manager.ok());
        REQUIRE(manager.value().createTable("users", userSchema()).ok());

        const auto insertResult = manager.value().insertRow("users", userRow(1, true, "Ana"));
        REQUIRE(insertResult.ok());

        rid = insertResult.value();

        const auto meta = readTablesMeta(tablesMetaPath);
        REQUIRE(meta.tableCount == 1);
        REQUIRE(meta.tables[0].lastPageId == meta.tables[0].firstPageId);

        REQUIRE(bufferPool.saveAllPagesToDisk().ok());
    }

    {
        dandb::storage::DiskManager disk(dataPath, { 'D', 'P', 'A', 'G' });
        dandb::buffer::BufferPoolManager bufferPool(1, disk);

        auto manager = dandb::catalog::TableManager::open(tablesMetaPath, bufferPool);
        REQUIRE(manager.ok());

        const auto stored = manager.value().readRow("users", rid);
        REQUIRE(stored.ok());
        requireUserRow(stored.value(), 1, true, "Ana");

        auto scanResult = manager.value().scan("users");
        REQUIRE(scanResult.ok());

        auto& iterator = scanResult.value();

        const auto hasFirst = iterator.hasNext();
        REQUIRE(hasFirst.ok());
        REQUIRE(hasFirst.value());

        const auto entry = iterator.next();
        REQUIRE(entry.ok());
        REQUIRE(entry.value().rid == rid);
        requireUserRow(entry.value().row, 1, true, "Ana");

        const auto hasSecond = iterator.hasNext();
        REQUIRE(hasSecond.ok());
        REQUIRE_FALSE(hasSecond.value());
    }

}

TEST_CASE("TableManager persists the new last page id when inserts grow a table", "[catalog][table-manager]") {

    const TempDir tempDir("grow_table");
    dandb::storage::DiskManager disk(tempDir.path()/"data.pages", { 'D', 'P', 'A', 'G' });
    dandb::buffer::BufferPoolManager bufferPool(1, disk);

    auto manager = dandb::catalog::TableManager::open(tempDir.path()/"tables.meta", bufferPool);
    REQUIRE(manager.ok());
    REQUIRE(manager.value().createTable("events", largeRowSchema()).ok());

    const auto createdMeta = readTablesMeta(tempDir.path()/"tables.meta");
    const auto firstPageId = createdMeta.tables[0].firstPageId;

    std::vector<dandb::record::RID> rids;

    for(int32_t i = 0; i < 20; i++) {
        const auto insertResult = manager.value().insertRow("events", largeRow(i));
        REQUIRE(insertResult.ok());

        rids.push_back(insertResult.value());

        if(insertResult.value().pageId != firstPageId) {
            break;
        }
    }

    REQUIRE_FALSE(rids.empty());
    REQUIRE(rids.back().pageId != firstPageId);

    const auto meta = readTablesMeta(tempDir.path()/"tables.meta");
    REQUIRE(meta.tableCount == 1);
    REQUIRE(meta.tables[0].firstPageId == firstPageId);
    REQUIRE(meta.tables[0].lastPageId == rids.back().pageId);

    const auto firstPageResult = bufferPool.fetchPage(firstPageId);
    REQUIRE(firstPageResult.ok());

    const dandb::record::SlottedPage firstPage(firstPageResult.value()->data());
    REQUIRE(firstPage.nextPageId() == rids.back().pageId);
    REQUIRE(bufferPool.unpinPage(firstPageId, false).ok());

}

TEST_CASE("TableManager delegates update delete and scan", "[catalog][table-manager]") {

    const TempDir tempDir("mutate_rows");
    const auto tablesMetaPath = tempDir.path()/"tables.meta";
    const auto dataPath = tempDir.path()/"data.pages";
    dandb::record::RID updatedRid;
    dandb::record::RID deletedRid;

    {
        dandb::storage::DiskManager disk(dataPath, { 'D', 'P', 'A', 'G' });
        dandb::buffer::BufferPoolManager bufferPool(2, disk);

        auto manager = dandb::catalog::TableManager::open(tablesMetaPath, bufferPool);
        REQUIRE(manager.ok());
        REQUIRE(manager.value().createTable("users", userSchema()).ok());

        const auto firstInsert = manager.value().insertRow("users", userRow(1, false, "Ana"));
        const auto secondInsert = manager.value().insertRow("users", userRow(2, true, "Luis"));
        REQUIRE(firstInsert.ok());
        REQUIRE(secondInsert.ok());

        updatedRid = firstInsert.value();
        deletedRid = secondInsert.value();

        REQUIRE(manager.value().updateRow("users", updatedRid, userRow(1, true, "Lucia")).ok());
        REQUIRE(manager.value().deleteRow("users", deletedRid).ok());

        const auto deleted = manager.value().readRow("users", deletedRid);
        REQUIRE_FALSE(deleted.ok());
        REQUIRE(deleted.status().code() == dandb::core::StatusCode::NotFound);

        REQUIRE(bufferPool.saveAllPagesToDisk().ok());
    }

    {
        dandb::storage::DiskManager disk(dataPath, { 'D', 'P', 'A', 'G' });
        dandb::buffer::BufferPoolManager bufferPool(1, disk);

        auto manager = dandb::catalog::TableManager::open(tablesMetaPath, bufferPool);
        REQUIRE(manager.ok());

        const auto updated = manager.value().readRow("users", updatedRid);
        const auto deleted = manager.value().readRow("users", deletedRid);

        REQUIRE(updated.ok());
        requireUserRow(updated.value(), 1, true, "Lucia");
        REQUIRE_FALSE(deleted.ok());
        REQUIRE(deleted.status().code() == dandb::core::StatusCode::NotFound);

        auto scanResult = manager.value().scan("users");
        REQUIRE(scanResult.ok());

        auto& iterator = scanResult.value();
        const auto entry = iterator.next();
        const auto exhausted = iterator.next();

        REQUIRE(entry.ok());
        REQUIRE(entry.value().rid == updatedRid);
        requireUserRow(entry.value().row, 1, true, "Lucia");
        REQUIRE_FALSE(exhausted.ok());
        REQUIRE(exhausted.status().code() == dandb::core::StatusCode::NotFound);
    }

}

TEST_CASE("TableManager open rejects corrupt tables.meta magic bytes", "[catalog][table-manager]") {

    const TempDir tempDir("corrupt_magic");
    dandb::storage::DiskManager disk(tempDir.path()/"data.pages", { 'D', 'P', 'A', 'G' });
    dandb::buffer::BufferPoolManager bufferPool(2, disk);

    writeFileBytes(tempDir.path()/"tables.meta", { 'N', 'O', 'P', 'E', 1, 0, 0, 0 });

    const auto manager = dandb::catalog::TableManager::open(tempDir.path()/"tables.meta", bufferPool);

    REQUIRE_FALSE(manager.ok());
    REQUIRE(manager.status().code() == dandb::core::StatusCode::Corruption);

}
