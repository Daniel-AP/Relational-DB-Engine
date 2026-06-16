#pragma once

#include <dandb/core/Status.h>
#include <dandb/core/Result.h>
#include <dandb/buffer/BufferPoolManager.h>
#include <dandb/record/Schema.h>
#include <dandb/record/RID.h>
#include <dandb/record/Row.h>
#include <dandb/record/TableIterator.h>
#include <dandb/core/Constants.h>

#include <filesystem>
#include <string>
#include <vector>
#include <unordered_map>
#include <utility>
#include <cstddef>
#include <cstdint>

namespace dandb {
    namespace catalog {

        struct TableMetadata {
            uint32_t tableId;
            std::string name;
            dandb::record::Schema schema;
            dandb::core::PageId firstPageId;
            dandb::core::PageId lastPageId;
            uint64_t liveRowCount;
        };

        class TableManager {
            public:
                TableManager(const TableManager&) = delete;
                TableManager& operator=(const TableManager&) = delete;
                TableManager(TableManager&&) = delete;
                TableManager& operator=(TableManager&&) = delete;

                static dandb::core::Result<TableManager> open(std::filesystem::path tablesMetaPath, dandb::buffer::BufferPoolManager& bpm);
                dandb::core::Status createTable(const std::string& name, const dandb::record::Schema& schema);
                dandb::core::Result<dandb::record::RID> insertRow(const std::string& tableName, const dandb::record::Row& row);
                dandb::core::Result<dandb::record::Row> readRow(const std::string& tableName, const dandb::record::RID& rid);
                dandb::core::Status updateRow(const std::string& tableName, const dandb::record::RID& rid, const dandb::record::Row& row);
                dandb::core::Status deleteRow(const std::string& tableName, const dandb::record::RID& rid);
                dandb::core::Result<dandb::record::TableIterator> scan(const std::string& tableName);

            private:
                TableManager(std::filesystem::path tablesMetaPath, dandb::buffer::BufferPoolManager& bpm)
                    : tablesMetaPath_(std::move(tablesMetaPath)), bpm_(bpm)
                {}
                dandb::core::Status load();
                dandb::core::Status save() const;

                std::filesystem::path tablesMetaPath_;
                dandb::buffer::BufferPoolManager& bpm_;
                uint32_t nextTableId_;
                std::vector<TableMetadata> tableMetadata_;
                std::unordered_map<std::string, size_t> tableMetadataIndex_;
        };

    }
}