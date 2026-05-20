#include <dandb/buffer/BufferPoolManager.h>
#include <dandb/core/Constants.h>
#include <dandb/core/Status.h>
#include <dandb/record/PagedTable.h>
#include <dandb/record/RID.h>
#include <dandb/record/Row.h>
#include <dandb/record/Schema.h>
#include <dandb/record/SlottedPage.h>
#include <dandb/record/Value.h>
#include <dandb/storage/DiskManager.h>

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>
#include <vector>

namespace {

    class TempDir {
        public:
            explicit TempDir(std::string name)
                : path_(
                    std::filesystem::temp_directory_path()/("dandb_paged_table_"+std::move(name)+"_"+std::to_string(nextId_++))
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

    dandb::record::Schema userSchema() {

        auto schema = dandb::record::Schema::create({
            dandb::record::Column{ "id", dandb::record::LogicalType::Int32, false, true, 0 },
            dandb::record::Column{ "active", dandb::record::LogicalType::Boolean, false, false, 0 },
            dandb::record::Column{ "name", dandb::record::LogicalType::String, false, false, 8 }
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

    dandb::record::Row userRow(int32_t id, bool active, std::string name) {

        return dandb::record::Row({
            dandb::record::Value::int32(id),
            dandb::record::Value::boolean(active),
            dandb::record::Value::string(std::move(name))
        });

    }

    dandb::record::Row largeRow(int32_t id) {

        return dandb::record::Row({
            dandb::record::Value::int32(id),
            dandb::record::Value::string("row_"+std::to_string(id))
        });

    }

    dandb::core::PageId initializeFirstTablePage(dandb::buffer::BufferPoolManager& bufferPool) {

        const auto pageResult = bufferPool.newPage();
        REQUIRE(pageResult.ok());

        auto* page = pageResult.value();
        const auto pageId = page->pageId();

        dandb::record::SlottedPage::initialize(page->data(), pageId);
        REQUIRE(bufferPool.unpinPage(pageId, true).ok());

        return pageId;

    }

    void requireUserRow(const dandb::record::Row& row, int32_t id, bool active, const std::string& name) {

        REQUIRE(row.valueCount() == 3);
        REQUIRE(row.value(0).asInt32() == id);
        REQUIRE(row.value(1).asBoolean() == active);
        REQUIRE(row.value(2).asString() == name);

    }

}

TEST_CASE("PagedTable inserts and reads one row from the first table page", "[record][paged-table]") {

    const TempDir tempDir("single_row");
    dandb::storage::DiskManager disk(tempDir.path()/"data.pages", { 'D', 'P', 'A', 'G' });
    dandb::buffer::BufferPoolManager bufferPool(2, disk);
    const auto schema = userSchema();
    const auto firstPageId = initializeFirstTablePage(bufferPool);
    dandb::record::PagedTable table(bufferPool, schema, firstPageId, firstPageId);

    const auto rid = table.insertRow(userRow(1, true, "Ana"));

    REQUIRE(rid.ok());
    REQUIRE(rid.value().pageId == firstPageId);
    REQUIRE(rid.value().slotId == 0);

    const auto stored = table.readRow(rid.value());

    REQUIRE(stored.ok());
    requireUserRow(stored.value(), 1, true, "Ana");

}

TEST_CASE("PagedTable keeps multiple rows addressable by their RID", "[record][paged-table]") {

    const TempDir tempDir("multiple_rows");
    dandb::storage::DiskManager disk(tempDir.path()/"data.pages", { 'D', 'P', 'A', 'G' });
    dandb::buffer::BufferPoolManager bufferPool(2, disk);
    const auto schema = userSchema();
    const auto firstPageId = initializeFirstTablePage(bufferPool);
    dandb::record::PagedTable table(bufferPool, schema, firstPageId, firstPageId);

    const auto firstRid = table.insertRow(userRow(10, true, "Ana"));
    const auto secondRid = table.insertRow(userRow(20, false, "Luis"));

    REQUIRE(firstRid.ok());
    REQUIRE(secondRid.ok());
    REQUIRE(firstRid.value().pageId == firstPageId);
    REQUIRE(secondRid.value().pageId == firstPageId);
    REQUIRE(firstRid.value().slotId == 0);
    REQUIRE(secondRid.value().slotId == 1);

    const auto firstStored = table.readRow(firstRid.value());
    const auto secondStored = table.readRow(secondRid.value());

    REQUIRE(firstStored.ok());
    REQUIRE(secondStored.ok());
    requireUserRow(firstStored.value(), 10, true, "Ana");
    requireUserRow(secondStored.value(), 20, false, "Luis");

}

TEST_CASE("PagedTable rejects rows that Codec cannot encode", "[record][paged-table]") {

    const TempDir tempDir("invalid_insert");
    dandb::storage::DiskManager disk(tempDir.path()/"data.pages", { 'D', 'P', 'A', 'G' });
    dandb::buffer::BufferPoolManager bufferPool(2, disk);
    const auto schema = userSchema();
    const auto firstPageId = initializeFirstTablePage(bufferPool);
    dandb::record::PagedTable table(bufferPool, schema, firstPageId, firstPageId);

    const dandb::record::Row wrongType({
        dandb::record::Value::string("not an int"),
        dandb::record::Value::boolean(true),
        dandb::record::Value::string("Ana")
    });

    const auto rid = table.insertRow(wrongType);

    REQUIRE_FALSE(rid.ok());
    REQUIRE(rid.status().code() == dandb::core::StatusCode::InvalidArgument);

    const auto firstPage = bufferPool.fetchPage(firstPageId);
    REQUIRE(firstPage.ok());

    const dandb::record::SlottedPage slottedPage(firstPage.value()->data());

    REQUIRE(slottedPage.slotCount() == 0);
    REQUIRE(bufferPool.unpinPage(firstPageId, false).ok());

}

TEST_CASE("PagedTable allocates and links a new page when the current table page is full", "[record][paged-table]") {

    const TempDir tempDir("page_chain");
    dandb::storage::DiskManager disk(tempDir.path()/"data.pages", { 'D', 'P', 'A', 'G' });
    dandb::buffer::BufferPoolManager bufferPool(1, disk);
    const auto schema = largeRowSchema();
    const auto firstPageId = initializeFirstTablePage(bufferPool);
    dandb::record::PagedTable table(bufferPool, schema, firstPageId, firstPageId);

    std::vector<dandb::record::RID> rids;
    dandb::record::RID firstRidOnSecondPage;

    for(int32_t i = 0; i < 20; i++) {
        const auto rid = table.insertRow(largeRow(i));
        REQUIRE(rid.ok());

        rids.push_back(rid.value());

        if(rid.value().pageId != firstPageId) {
            firstRidOnSecondPage = rid.value();
            break;
        }
    }

    REQUIRE(firstRidOnSecondPage.pageId != dandb::core::INVALID_PAGE_ID);
    REQUIRE(firstRidOnSecondPage.pageId != firstPageId);
    REQUIRE(firstRidOnSecondPage.slotId == 0);

    const auto firstPage = bufferPool.fetchPage(firstPageId);
    REQUIRE(firstPage.ok());

    const dandb::record::SlottedPage firstSlottedPage(firstPage.value()->data());

    REQUIRE(firstSlottedPage.nextPageId() == firstRidOnSecondPage.pageId);
    REQUIRE(bufferPool.unpinPage(firstPageId, false).ok());

    const auto stored = table.readRow(firstRidOnSecondPage);

    REQUIRE(stored.ok());
    REQUIRE(stored.value().value(0).asInt32() == static_cast<int32_t>(rids.size()-1));

}

TEST_CASE("PagedTable delete creates a tombstone and later reads return NotFound", "[record][paged-table]") {

    const TempDir tempDir("delete_row");
    dandb::storage::DiskManager disk(tempDir.path()/"data.pages", { 'D', 'P', 'A', 'G' });
    dandb::buffer::BufferPoolManager bufferPool(2, disk);
    const auto schema = userSchema();
    const auto firstPageId = initializeFirstTablePage(bufferPool);
    dandb::record::PagedTable table(bufferPool, schema, firstPageId, firstPageId);

    const auto rid = table.insertRow(userRow(7, true, "Maria"));
    REQUIRE(rid.ok());

    const auto deleteStatus = table.deleteRow(rid.value());
    const auto readDeleted = table.readRow(rid.value());
    const auto deleteAgain = table.deleteRow(rid.value());

    REQUIRE(deleteStatus.ok());
    REQUIRE_FALSE(readDeleted.ok());
    REQUIRE(readDeleted.status().code() == dandb::core::StatusCode::NotFound);
    REQUIRE_FALSE(deleteAgain.ok());
    REQUIRE(deleteAgain.code() == dandb::core::StatusCode::NotFound);

}

TEST_CASE("PagedTable updates a row in place and preserves its RID", "[record][paged-table]") {

    const TempDir tempDir("update_row");
    dandb::storage::DiskManager disk(tempDir.path()/"data.pages", { 'D', 'P', 'A', 'G' });
    dandb::buffer::BufferPoolManager bufferPool(2, disk);
    const auto schema = userSchema();
    const auto firstPageId = initializeFirstTablePage(bufferPool);
    dandb::record::PagedTable table(bufferPool, schema, firstPageId, firstPageId);

    const auto rid = table.insertRow(userRow(3, false, "Ana"));
    REQUIRE(rid.ok());

    const auto updateStatus = table.updateRow(rid.value(), userRow(3, true, "Lucia"));
    const auto stored = table.readRow(rid.value());

    REQUIRE(updateStatus.ok());
    REQUIRE(stored.ok());
    requireUserRow(stored.value(), 3, true, "Lucia");

}

TEST_CASE("PagedTable rejects invalid updates and preserves the old row", "[record][paged-table]") {

    const TempDir tempDir("invalid_update");
    dandb::storage::DiskManager disk(tempDir.path()/"data.pages", { 'D', 'P', 'A', 'G' });
    dandb::buffer::BufferPoolManager bufferPool(2, disk);
    const auto schema = userSchema();
    const auto firstPageId = initializeFirstTablePage(bufferPool);
    dandb::record::PagedTable table(bufferPool, schema, firstPageId, firstPageId);

    const auto rid = table.insertRow(userRow(5, true, "Ana"));
    REQUIRE(rid.ok());

    const auto updateStatus = table.updateRow(rid.value(), userRow(5, false, "TooLongName"));
    const auto stored = table.readRow(rid.value());

    REQUIRE_FALSE(updateStatus.ok());
    REQUIRE(updateStatus.code() == dandb::core::StatusCode::InvalidArgument);
    REQUIRE(stored.ok());
    requireUserRow(stored.value(), 5, true, "Ana");

}
