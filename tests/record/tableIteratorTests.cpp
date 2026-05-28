#include <dandb/buffer/BufferPoolManager.h>
#include <dandb/core/Constants.h>
#include <dandb/core/Status.h>
#include <dandb/record/PagedTable.h>
#include <dandb/record/RID.h>
#include <dandb/record/Row.h>
#include <dandb/record/Schema.h>
#include <dandb/record/SlottedPage.h>
#include <dandb/record/TableIterator.h>
#include <dandb/record/Value.h>
#include <dandb/storage/DiskManager.h>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <filesystem>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace {

    class TempDir {
        public:
            explicit TempDir(std::string name)
                : path_(
                    std::filesystem::temp_directory_path()/("dandb_table_iterator_"+std::move(name)+"_"+std::to_string(nextId_++))
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

TEST_CASE("TableIterator reports an empty table as finished", "[record][table-iterator]") {

    const TempDir tempDir("empty_table");
    dandb::storage::DiskManager disk(tempDir.path()/"data.pages", { 'D', 'P', 'A', 'G' });
    dandb::buffer::BufferPoolManager bufferPool(2, disk);
    const auto schema = userSchema();
    const auto firstPageId = initializeFirstTablePage(bufferPool);
    dandb::record::TableIterator iterator(bufferPool, schema, firstPageId);

    const auto hasNext = iterator.hasNext();
    REQUIRE(hasNext.ok());
    REQUIRE_FALSE(hasNext.value());

    const auto entry = iterator.next();
    REQUIRE_FALSE(entry.ok());
    REQUIRE(entry.status().code() == dandb::core::StatusCode::NotFound);

}

TEST_CASE("TableIterator hasNext does not consume the next row", "[record][table-iterator]") {

    const TempDir tempDir("has_next_idempotent");
    dandb::storage::DiskManager disk(tempDir.path()/"data.pages", { 'D', 'P', 'A', 'G' });
    dandb::buffer::BufferPoolManager bufferPool(2, disk);
    const auto schema = userSchema();
    const auto firstPageId = initializeFirstTablePage(bufferPool);
    dandb::record::PagedTable table(bufferPool, schema, firstPageId, firstPageId);
    const auto rid = table.insertRow(userRow(1, true, "Ana"));
    REQUIRE(rid.ok());

    dandb::record::TableIterator iterator(bufferPool, schema, firstPageId);

    const auto firstCheck = iterator.hasNext();
    const auto secondCheck = iterator.hasNext();
    REQUIRE(firstCheck.ok());
    REQUIRE(secondCheck.ok());
    REQUIRE(firstCheck.value());
    REQUIRE(secondCheck.value());

    const auto entry = iterator.next();
    REQUIRE(entry.ok());
    REQUIRE(entry.value().rid == rid.value());
    requireUserRow(entry.value().row, 1, true, "Ana");

    const auto afterRow = iterator.hasNext();
    REQUIRE(afterRow.ok());
    REQUIRE_FALSE(afterRow.value());

}

TEST_CASE("TableIterator returns rows from one page in slot order", "[record][table-iterator]") {

    const TempDir tempDir("single_page_order");
    dandb::storage::DiskManager disk(tempDir.path()/"data.pages", { 'D', 'P', 'A', 'G' });
    dandb::buffer::BufferPoolManager bufferPool(2, disk);
    const auto schema = userSchema();
    const auto firstPageId = initializeFirstTablePage(bufferPool);
    dandb::record::PagedTable table(bufferPool, schema, firstPageId, firstPageId);

    const auto firstRid = table.insertRow(userRow(10, true, "Ana"));
    const auto secondRid = table.insertRow(userRow(20, false, "Luis"));
    const auto thirdRid = table.insertRow(userRow(30, true, "Maria"));

    REQUIRE(firstRid.ok());
    REQUIRE(secondRid.ok());
    REQUIRE(thirdRid.ok());

    dandb::record::TableIterator iterator(bufferPool, schema, firstPageId);

    const auto firstEntry = iterator.next();
    const auto secondEntry = iterator.next();
    const auto thirdEntry = iterator.next();
    const auto finished = iterator.next();

    REQUIRE(firstEntry.ok());
    REQUIRE(secondEntry.ok());
    REQUIRE(thirdEntry.ok());
    REQUIRE(firstEntry.value().rid == firstRid.value());
    REQUIRE(secondEntry.value().rid == secondRid.value());
    REQUIRE(thirdEntry.value().rid == thirdRid.value());
    requireUserRow(firstEntry.value().row, 10, true, "Ana");
    requireUserRow(secondEntry.value().row, 20, false, "Luis");
    requireUserRow(thirdEntry.value().row, 30, true, "Maria");
    REQUIRE_FALSE(finished.ok());
    REQUIRE(finished.status().code() == dandb::core::StatusCode::NotFound);

}

TEST_CASE("TableIterator skips tombstones", "[record][table-iterator]") {

    const TempDir tempDir("skip_tombstones");
    dandb::storage::DiskManager disk(tempDir.path()/"data.pages", { 'D', 'P', 'A', 'G' });
    dandb::buffer::BufferPoolManager bufferPool(2, disk);
    const auto schema = userSchema();
    const auto firstPageId = initializeFirstTablePage(bufferPool);
    dandb::record::PagedTable table(bufferPool, schema, firstPageId, firstPageId);

    const auto firstRid = table.insertRow(userRow(1, true, "Ana"));
    const auto deletedRid = table.insertRow(userRow(2, false, "Luis"));
    const auto thirdRid = table.insertRow(userRow(3, true, "Maria"));

    REQUIRE(firstRid.ok());
    REQUIRE(deletedRid.ok());
    REQUIRE(thirdRid.ok());
    REQUIRE(table.deleteRow(deletedRid.value()).ok());

    dandb::record::TableIterator iterator(bufferPool, schema, firstPageId);

    const auto firstEntry = iterator.next();
    const auto secondEntry = iterator.next();
    const auto finished = iterator.next();

    REQUIRE(firstEntry.ok());
    REQUIRE(secondEntry.ok());
    REQUIRE(firstEntry.value().rid == firstRid.value());
    REQUIRE(secondEntry.value().rid == thirdRid.value());
    requireUserRow(firstEntry.value().row, 1, true, "Ana");
    requireUserRow(secondEntry.value().row, 3, true, "Maria");
    REQUIRE_FALSE(finished.ok());
    REQUIRE(finished.status().code() == dandb::core::StatusCode::NotFound);

}

TEST_CASE("TableIterator continues across linked table pages", "[record][table-iterator]") {

    const TempDir tempDir("linked_pages");
    dandb::storage::DiskManager disk(tempDir.path()/"data.pages", { 'D', 'P', 'A', 'G' });
    dandb::buffer::BufferPoolManager bufferPool(1, disk);
    const auto schema = largeRowSchema();
    const auto firstPageId = initializeFirstTablePage(bufferPool);
    dandb::record::PagedTable table(bufferPool, schema, firstPageId, firstPageId);

    std::vector<dandb::record::RID> rids;

    for(int32_t i = 0; i < 20; i++) {
        const auto rid = table.insertRow(largeRow(i));
        REQUIRE(rid.ok());

        rids.push_back(rid.value());

        if(rid.value().pageId != firstPageId) {
            break;
        }
    }

    REQUIRE_FALSE(rids.empty());
    REQUIRE(rids.back().pageId != firstPageId);

    dandb::record::TableIterator iterator(bufferPool, schema, firstPageId);

    for(size_t i = 0; i < rids.size(); i++) {
        const auto entry = iterator.next();
        REQUIRE(entry.ok());
        REQUIRE(entry.value().rid == rids[i]);
        REQUIRE(entry.value().row.value(0).asInt32() == static_cast<int32_t>(i));
    }

    const auto finished = iterator.next();
    REQUIRE_FALSE(finished.ok());
    REQUIRE(finished.status().code() == dandb::core::StatusCode::NotFound);

}

TEST_CASE("TableIterator skips a fully tombstoned page before later linked pages", "[record][table-iterator]") {

    const TempDir tempDir("tombstoned_first_page");
    dandb::storage::DiskManager disk(tempDir.path()/"data.pages", { 'D', 'P', 'A', 'G' });
    dandb::buffer::BufferPoolManager bufferPool(2, disk);
    const auto schema = largeRowSchema();
    const auto firstPageId = initializeFirstTablePage(bufferPool);
    dandb::record::PagedTable table(bufferPool, schema, firstPageId, firstPageId);

    std::vector<dandb::record::RID> rids;
    bool reachedSecondPage = false;
    size_t firstSecondPageIndex = 0;

    for(int32_t i = 0; i < 20; i++) {
        const auto rid = table.insertRow(largeRow(i));
        REQUIRE(rid.ok());

        rids.push_back(rid.value());

        if(rid.value().pageId != firstPageId) {
            reachedSecondPage = true;
            firstSecondPageIndex = rids.size()-1;
            break;
        }
    }

    REQUIRE(reachedSecondPage);

    for(size_t i = 0; i < firstSecondPageIndex; i++) {
        REQUIRE(table.deleteRow(rids[i]).ok());
    }

    dandb::record::TableIterator iterator(bufferPool, schema, firstPageId);

    const auto entry = iterator.next();
    REQUIRE(entry.ok());
    REQUIRE(entry.value().rid == rids[firstSecondPageIndex]);
    REQUIRE(entry.value().row.value(0).asInt32() == static_cast<int32_t>(firstSecondPageIndex));

    const auto finished = iterator.next();
    REQUIRE_FALSE(finished.ok());
    REQUIRE(finished.status().code() == dandb::core::StatusCode::NotFound);

}
