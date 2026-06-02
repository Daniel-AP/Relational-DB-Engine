#include <dandb/buffer/BufferPoolManager.h>
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

    struct ScannedRow {
        dandb::record::RID rid;
        int32_t id;
        bool active;
        std::string name;
        std::string payload;
    };

    class TempDir {
        public:
            explicit TempDir(std::string name)
                : path_(
                    std::filesystem::temp_directory_path()/("dandb_record_integration_"+std::move(name)+"_"+std::to_string(nextId_++))
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

    dandb::record::Schema integrationSchema() {

        auto schema = dandb::record::Schema::create({
            dandb::record::Column{ "id", dandb::record::LogicalType::Int32, false, true, 0 },
            dandb::record::Column{ "active", dandb::record::LogicalType::Boolean, false, false, 0 },
            dandb::record::Column{ "name", dandb::record::LogicalType::String, false, false, 16 },
            dandb::record::Column{ "payload", dandb::record::LogicalType::String, false, false, 512 }
        });

        REQUIRE(schema.ok());

        return schema.value();

    }

    dandb::record::Row integrationRow(int32_t id, bool active, std::string name, std::string payload) {

        return dandb::record::Row({
            dandb::record::Value::int32(id),
            dandb::record::Value::boolean(active),
            dandb::record::Value::string(std::move(name)),
            dandb::record::Value::string(std::move(payload))
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

    ScannedRow scannedRow(const dandb::record::TableIteratorEntry& entry) {

        const auto& row = entry.row;

        REQUIRE(row.valueCount() == 4);

        return ScannedRow{
            entry.rid,
            row.value(0).asInt32(),
            row.value(1).asBoolean(),
            row.value(2).asString(),
            row.value(3).asString()
        };

    }

    std::vector<ScannedRow> scanTable(
        dandb::buffer::BufferPoolManager& bufferPool,
        const dandb::record::Schema& schema,
        dandb::core::PageId firstPageId
    ) {

        dandb::record::TableIterator iterator(bufferPool, schema, firstPageId);
        std::vector<ScannedRow> rows;

        while(true) {
            const auto hasNext = iterator.hasNext();
            REQUIRE(hasNext.ok());

            if(!hasNext.value()) break;

            const auto entry = iterator.next();
            REQUIRE(entry.ok());

            rows.push_back(scannedRow(entry.value()));
        }

        const auto exhausted = iterator.next();
        REQUIRE_FALSE(exhausted.ok());
        REQUIRE(exhausted.status().code() == dandb::core::StatusCode::NotFound);

        return rows;

    }

    void requireScannedRow(
        const ScannedRow& row,
        dandb::record::RID rid,
        int32_t id,
        bool active,
        const std::string& name,
        const std::string& payload
    ) {

        REQUIRE(row.rid == rid);
        REQUIRE(row.id == id);
        REQUIRE(row.active == active);
        REQUIRE(row.name == name);
        REQUIRE(row.payload == payload);

    }

}

TEST_CASE("Record layer scans only live updated rows across linked pages", "[record][integration]") {

    const TempDir tempDir("scan_mutated_rows");
    dandb::storage::DiskManager disk(tempDir.path()/"data.pages", { 'D', 'P', 'A', 'G' });
    dandb::buffer::BufferPoolManager bufferPool(1, disk);
    const auto schema = integrationSchema();
    const auto firstPageId = initializeFirstTablePage(bufferPool);
    dandb::record::PagedTable table(bufferPool, schema, firstPageId, firstPageId);
    std::vector<dandb::record::RID> rids;

    for(int32_t i = 0; i < 10; i++) {
        const auto rid = table.insertRow(integrationRow(i, false, "row_"+std::to_string(i), "payload_"+std::to_string(i)));
        REQUIRE(rid.ok());
        rids.push_back(rid.value());
    }

    REQUIRE(rids.front().pageId == firstPageId);
    REQUIRE(rids.back().pageId != firstPageId);

    REQUIRE(table.updateRow(rids[1], integrationRow(1, true, "updated_1", "payload_updated_1")).ok());
    REQUIRE(table.updateRow(rids[8], integrationRow(8, true, "updated_8", "payload_updated_8")).ok());
    REQUIRE(table.deleteRow(rids[0]).ok());
    REQUIRE(table.deleteRow(rids[7]).ok());

    const auto rows = scanTable(bufferPool, schema, firstPageId);

    REQUIRE(rows.size() == 8);
    requireScannedRow(rows[0], rids[1], 1, true, "updated_1", "payload_updated_1");
    requireScannedRow(rows[1], rids[2], 2, false, "row_2", "payload_2");
    requireScannedRow(rows[2], rids[3], 3, false, "row_3", "payload_3");
    requireScannedRow(rows[3], rids[4], 4, false, "row_4", "payload_4");
    requireScannedRow(rows[4], rids[5], 5, false, "row_5", "payload_5");
    requireScannedRow(rows[5], rids[6], 6, false, "row_6", "payload_6");
    requireScannedRow(rows[6], rids[8], 8, true, "updated_8", "payload_updated_8");
    requireScannedRow(rows[7], rids[9], 9, false, "row_9", "payload_9");

}

TEST_CASE("Record layer persists updates and tombstones before scanning after reload", "[record][integration]") {

    const TempDir tempDir("scan_after_reload");
    dandb::storage::DiskManager disk(tempDir.path()/"data.pages", { 'D', 'P', 'A', 'G' });
    const auto schema = integrationSchema();
    dandb::core::PageId firstPageId;
    dandb::core::PageId lastPageId;
    std::vector<dandb::record::RID> rids;

    {
        dandb::buffer::BufferPoolManager bufferPool(2, disk);
        firstPageId = initializeFirstTablePage(bufferPool);
        dandb::record::PagedTable table(bufferPool, schema, firstPageId, firstPageId);

        for(int32_t i = 0; i < 10; i++) {
            const auto rid = table.insertRow(integrationRow(i, false, "row_"+std::to_string(i), "payload_"+std::to_string(i)));
            REQUIRE(rid.ok());
            rids.push_back(rid.value());
        }

        lastPageId = table.lastPageId();

        REQUIRE(table.updateRow(rids[8], integrationRow(8, true, "reloaded_8", "payload_reloaded_8")).ok());
        REQUIRE(table.deleteRow(rids[2]).ok());
        REQUIRE(table.deleteRow(rids[7]).ok());
        REQUIRE(bufferPool.saveAllPagesToDisk().ok());
    }

    dandb::buffer::BufferPoolManager reloadedBufferPool(1, disk);
    dandb::record::PagedTable reloadedTable(reloadedBufferPool, schema, firstPageId, lastPageId);

    const auto deletedFirstPageRow = reloadedTable.readRow(rids[2]);
    const auto deletedSecondPageRow = reloadedTable.readRow(rids[7]);
    const auto updatedSecondPageRow = reloadedTable.readRow(rids[8]);
    const auto rows = scanTable(reloadedBufferPool, schema, firstPageId);

    REQUIRE_FALSE(deletedFirstPageRow.ok());
    REQUIRE(deletedFirstPageRow.status().code() == dandb::core::StatusCode::NotFound);
    REQUIRE_FALSE(deletedSecondPageRow.ok());
    REQUIRE(deletedSecondPageRow.status().code() == dandb::core::StatusCode::NotFound);
    REQUIRE(updatedSecondPageRow.ok());
    REQUIRE(updatedSecondPageRow.value().value(1).asBoolean());
    REQUIRE(updatedSecondPageRow.value().value(2).asString() == "reloaded_8");
    REQUIRE(rows.size() == 8);
    requireScannedRow(rows[0], rids[0], 0, false, "row_0", "payload_0");
    requireScannedRow(rows[1], rids[1], 1, false, "row_1", "payload_1");
    requireScannedRow(rows[2], rids[3], 3, false, "row_3", "payload_3");
    requireScannedRow(rows[3], rids[4], 4, false, "row_4", "payload_4");
    requireScannedRow(rows[4], rids[5], 5, false, "row_5", "payload_5");
    requireScannedRow(rows[5], rids[6], 6, false, "row_6", "payload_6");
    requireScannedRow(rows[6], rids[8], 8, true, "reloaded_8", "payload_reloaded_8");
    requireScannedRow(rows[7], rids[9], 9, false, "row_9", "payload_9");

}
