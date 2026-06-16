#include <dandb/catalog/TableManager.h>
#include <dandb/core/Status.h>
#include <dandb/buffer/Page.h>
#include <dandb/record/SlottedPage.h>
#include <dandb/core/Constants.h>
#include <dandb/record/PagedTable.h>

#include <utility>

namespace dandb {
    namespace catalog {

        dandb::core::Result<TableManager> TableManager::open(std::filesystem::path tablesMetaPath, dandb::buffer::BufferPoolManager& bpm) {

            TableManager manager(tablesMetaPath, bpm);

            auto loadStatus = manager.load();
            if(!loadStatus.ok()) {
                return loadStatus;
            }

            return std::move(manager);

        }

        dandb::core::Status TableManager::createTable(const std::string& name, const dandb::record::Schema& schema) {

            if(name.empty()) {
                return dandb::core::Status::InvalidArgument("Cannot create a table with an empty name");
            }

            if(tableMetadataIndex_.find(name) != tableMetadataIndex_.end()) {
                return dandb::core::Status::AlreadyExists("Cannot create table with name '"+name+"': a table with this name already exists");
            }

            if(name.size() > 0xFFFFu) {
                return dandb::core::Status::InvalidArgument("Cannot create table with name '"+name+"': table name must be at most "+std::to_string(0xFFFFu)+" bytes");
            }

            const auto newPageResult = bpm_.newPage();
            if(!newPageResult.ok()) {
                return newPageResult.status();
            }

            dandb::buffer::Page* page = newPageResult.value();
            dandb::core::PageId pageId = page->pageId();

            dandb::record::SlottedPage::initialize(page->data(), pageId);

            const auto unpinStatus = bpm_.unpinPage(pageId, true);
            if(!unpinStatus.ok()) {
                return unpinStatus;
            }

            uint32_t newTableId = nextTableId_;
            nextTableId_++;

            tableMetadata_.push_back({
                newTableId,
                name,
                schema,
                pageId,
                pageId
            });

            const auto saveStatus = save();
            if(!saveStatus.ok()) {
                nextTableId_ = newTableId;
                tableMetadata_.pop_back();
                return saveStatus;
            }

            tableMetadataIndex_[name] = tableMetadata_.size()-1;

            return dandb::core::Status::Ok();

        }

        dandb::core::Result<dandb::record::RID> TableManager::insertRow(const std::string& tableName, const dandb::record::Row& row) {
            
            if(tableMetadataIndex_.find(tableName) == tableMetadataIndex_.end()) {
                return dandb::core::Status::NotFound("Cannot insert row into table with name '"+tableName+"': table does not exist");
            }

            TableMetadata& meta = tableMetadata_[tableMetadataIndex_[tableName]];
            dandb::core::PageId oldLastPageId = meta.lastPageId;

            dandb::record::PagedTable pagedTable(bpm_, meta.schema, meta.firstPageId, meta.lastPageId);

            const auto insertResult = pagedTable.insertRow(row);
            if(!insertResult.ok()) {
                return insertResult.status();
            }

            meta.lastPageId = pagedTable.lastPageId();

            if(oldLastPageId != meta.lastPageId) {
                const auto saveStatus = save();
                if(!saveStatus.ok()) {
                    return saveStatus;
                }
            }

            return insertResult.value();

        }

    }
}
