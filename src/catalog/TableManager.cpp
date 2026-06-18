#include <dandb/catalog/TableManager.h>
#include <dandb/core/Status.h>
#include <dandb/buffer/Page.h>
#include <dandb/record/SlottedPage.h>
#include <dandb/core/Constants.h>
#include <dandb/record/PagedTable.h>
#include <dandb/record/TableIterator.h>
#include <dandb/core/Helper.h>
#include <dandb/record/Layout.h>
#include <dandb/record/Schema.h>
#include <dandb/record/LogicalType.h>

#include <utility>
#include <fstream>
#include <array>
#include <vector>
#include <cstddef>
#include <cstring>
#include <string>
#include <system_error>
#include <unordered_map>
#include <unordered_set>

namespace {

    constexpr std::array<char, 4> TABLES_META_MAGIC{ 'D', 'T', 'B', 'L' };
    constexpr size_t TABLES_META_HEADER_SIZE = 16;
    constexpr size_t TABLE_RESERVED_SIZE = 3;
    constexpr size_t COLUMN_NAME_ALIGNMENT = 4;
    constexpr size_t TABLE_ALIGNMENT = 8;
    constexpr std::byte COLUMN_NULLABLE_FLAG{0x01};
    constexpr std::byte COLUMN_PRIMARY_KEY_FLAG{0x02};
    constexpr std::byte COLUMN_KNOWN_FLAGS{0x03};

}

namespace dandb {
    namespace catalog {

        dandb::core::Result<TableManager> TableManager::open(std::filesystem::path tablesMetaPath, dandb::buffer::BufferPoolManager& bpm) {

            TableManager manager(tablesMetaPath, bpm);

            auto loadStatus = manager.load();
            if(!loadStatus.ok()) {
                return loadStatus;
            }

            return manager;

        }

        dandb::core::Status TableManager::createTable(const std::string& name, const dandb::record::Schema& schema) {

            if(name.empty()) {
                return dandb::core::Status::InvalidArgument("Cannot create a table with an empty name");
            }

            if(tableNameToMetadataIndex_.find(name) != tableNameToMetadataIndex_.end()) {
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

            tablesMetadata_.push_back({
                newTableId,
                name,
                schema,
                pageId,
                pageId
            });

            const auto saveStatus = save();
            if(!saveStatus.ok()) {
                nextTableId_ = newTableId;
                tablesMetadata_.pop_back();
                return saveStatus;
            }

            tableNameToMetadataIndex_[name] = tablesMetadata_.size()-1;

            return dandb::core::Status::Ok();

        }

        dandb::core::Result<dandb::record::RID> TableManager::insertRow(const std::string& tableName, const dandb::record::Row& row) {
            
            if(tableNameToMetadataIndex_.find(tableName) == tableNameToMetadataIndex_.end()) {
                return dandb::core::Status::NotFound("Cannot insert row into table with name '"+tableName+"': table does not exist");
            }

            TableMetadata& meta = tablesMetadata_[tableNameToMetadataIndex_[tableName]];
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
                    meta.lastPageId = oldLastPageId;
                    return saveStatus;
                }
            }

            return insertResult.value();

        }

        dandb::core::Result<dandb::record::Row> TableManager::readRow(const std::string& tableName, const dandb::record::RID& rid) {

            if(tableNameToMetadataIndex_.find(tableName) == tableNameToMetadataIndex_.end()) {
                return dandb::core::Status::NotFound("Cannot read row from table with name '"+tableName+"': table does not exist");
            }

            const TableMetadata& meta = tablesMetadata_[tableNameToMetadataIndex_[tableName]];
            dandb::record::PagedTable pagedTable(bpm_, meta.schema, meta.firstPageId, meta.lastPageId);

            auto readResult = pagedTable.readRow(rid);
            if(!readResult.ok()) {
                return readResult.status();
            }

            return std::move(readResult.value());

        }

        dandb::core::Status TableManager::updateRow(const std::string& tableName, const dandb::record::RID& rid, const dandb::record::Row& row) {

            if(tableNameToMetadataIndex_.find(tableName) == tableNameToMetadataIndex_.end()) {
                return dandb::core::Status::NotFound("Cannot update row from table with name '"+tableName+"': table does not exist");
            }

            const TableMetadata& meta = tablesMetadata_[tableNameToMetadataIndex_[tableName]];
            dandb::record::PagedTable pagedTable(bpm_, meta.schema, meta.firstPageId, meta.lastPageId);

            const auto updateStatus = pagedTable.updateRow(rid, row);
            
            return updateStatus;

        }
        
        dandb::core::Status TableManager::deleteRow(const std::string& tableName, const dandb::record::RID& rid) {

            if(tableNameToMetadataIndex_.find(tableName) == tableNameToMetadataIndex_.end()) {
                return dandb::core::Status::NotFound("Cannot delete row from table with name '"+tableName+"': table does not exist");
            }

            const TableMetadata& meta = tablesMetadata_[tableNameToMetadataIndex_[tableName]];
            dandb::record::PagedTable pagedTable(bpm_, meta.schema, meta.firstPageId, meta.lastPageId);

            const auto deleteStatus = pagedTable.deleteRow(rid);
            
            return deleteStatus;
            
        }

        dandb::core::Result<dandb::record::TableIterator> TableManager::scan(const std::string& tableName) {

            if(tableNameToMetadataIndex_.find(tableName) == tableNameToMetadataIndex_.end()) {
                return dandb::core::Status::NotFound("Cannot scan table with name '"+tableName+"': table does not exist");
            }

            const TableMetadata& meta = tablesMetadata_[tableNameToMetadataIndex_[tableName]];

            return dandb::record::TableIterator{
                bpm_,
                meta.schema,
                meta.firstPageId
            };

        }

        dandb::core::Status TableManager::load() {

            const auto ioFailure = [this](const std::string& message) {
                return dandb::core::Status::IOError("Cannot open tables metadata file '" + tablesMetaPath_.string() + "': " + message);
            };

            const auto corruptionFailure = [this](const std::string& message) {
                return dandb::core::Status::Corruption("Cannot load tables metadata from '" + tablesMetaPath_.string() + "': " + message);
            };

            std::error_code filesystemError;

            if(!std::filesystem::exists(tablesMetaPath_, filesystemError)) {

                if(filesystemError) {
                    return ioFailure(filesystemError.message());
                }

                const auto initStatus = initMetaFile();
                if(!initStatus.ok()) {
                    return initStatus;
                }

            }

            std::ifstream file(tablesMetaPath_, std::ios::binary);
            if(!file.is_open()) {
                return ioFailure("unable to open file");
            }

            const auto fileSize = std::filesystem::file_size(tablesMetaPath_);
            std::vector<std::byte> fileBytes(fileSize);

            file.read(reinterpret_cast<char*>(fileBytes.data()), static_cast<std::streamsize>(fileBytes.size()));

            uint32_t loadedNextTableId = 1;
            std::vector<TableMetadata> loadedTablesMetadata;
            std::unordered_map<std::string, size_t> loadedTableMetadataIndex;
            std::unordered_set<uint32_t> loadedTableIds;

            for(size_t i = 0; i < TABLES_META_MAGIC.size(); i++) {
                if(fileBytes[i] != static_cast<std::byte>(TABLES_META_MAGIC[i])) {
                    return corruptionFailure("invalid magic bytes");
                }
            }

            size_t offset = TABLES_META_MAGIC.size();

            loadedNextTableId = dandb::core::helper::readUint32(fileBytes, offset); offset += 4;

            uint32_t tableCount = dandb::core::helper::readUint32(fileBytes, offset); offset += 4;
            loadedTablesMetadata.reserve(tableCount);
            loadedTableMetadataIndex.reserve(tableCount);
            loadedTableIds.reserve(tableCount);

            offset += 4;

            uint32_t maxTableId = 0;

            for(size_t i = 0; i < tableCount; i++) {

                auto tableResult = readTable(fileBytes, offset);
                if(!tableResult.ok()) {
                    return tableResult.status();
                }

                TableMetadata meta = std::move(tableResult.value());

                if(loadedTableIds.find(meta.tableId) != loadedTableIds.end()) {
                    return corruptionFailure("duplicate table id");
                }
                loadedTableIds.insert(meta.tableId);

                if(loadedTableMetadataIndex.find(meta.name) != loadedTableMetadataIndex.end()) {
                    return corruptionFailure("duplicate table name '" + meta.name + "'");
                }

                if(meta.tableId > maxTableId) {
                    maxTableId = meta.tableId;
                }

                loadedTableMetadataIndex[meta.name] = loadedTablesMetadata.size();
                loadedTablesMetadata.push_back(std::move(meta));

            }

            if(loadedNextTableId <= maxTableId) {
                return corruptionFailure("next table id would reuse an existing table id");
            }

            nextTableId_ = loadedNextTableId;
            tablesMetadata_ = std::move(loadedTablesMetadata);
            tableNameToMetadataIndex_ = std::move(loadedTableMetadataIndex);

            return dandb::core::Status::Ok();

        }

        dandb::core::Status TableManager::save() const {
            
            std::ofstream file(tablesMetaPath_, std::ios::binary | std::ios::trunc);
            if(!file.is_open()) {
                return dandb::core::Status::IOError("Cannot save to tables metadata file '"+tablesMetaPath_.string() + "': unable to open file");
            }

            size_t fileSerializedSizeToSave = serializedSize();
            std::vector<std::byte> fileBytesToSave(fileSerializedSizeToSave);

            size_t offset = 0;

            for(size_t i = 0; i < TABLES_META_MAGIC.size(); i++) {
                fileBytesToSave[i] = static_cast<std::byte>(TABLES_META_MAGIC[i]);
            }
            offset += TABLES_META_MAGIC.size();
            dandb::core::helper::writeUint32(fileBytesToSave, offset, nextTableId_); offset += 4;
            dandb::core::helper::writeUint32(fileBytesToSave, offset, tablesMetadata_.size()); offset += 4;
            offset += 4; // reserved

            for(const TableMetadata& table: tablesMetadata_) {
                dandb::core::helper::writeUint32(fileBytesToSave, offset, table.tableId); offset += 4;
                dandb::core::helper::writeUint32(fileBytesToSave, offset, table.firstPageId); offset += 4;
                dandb::core::helper::writeUint32(fileBytesToSave, offset, table.lastPageId); offset += 4;
                fileBytesToSave[offset] = std::byte{static_cast<uint8_t>(table.schema.columnCount())}; offset++;
                offset += 3; // padding
                dandb::core::helper::writeUint16(fileBytesToSave, offset, table.name.length()); offset += 2;
                std::memcpy(fileBytesToSave.data()+offset, table.name.data(), table.name.length()); offset += table.name.length();
                offset = dandb::record::layout::alignTo(offset, 4);
                for(const dandb::record::Column& col: table.schema.columns()) {
                    fileBytesToSave[offset] = std::byte{dandb::record::logicalTypeCode(col.type)}; offset++;
                    fileBytesToSave[offset] |= std::byte{static_cast<uint8_t>(col.nullable+(col.primaryKey<<1))}; offset++;
                    dandb::core::helper::writeUint16(fileBytesToSave, offset, col.stringCapacity); offset += 2;
                    dandb::core::helper::writeUint16(fileBytesToSave, offset, col.name.length()); offset += 2;
                    std::memcpy(fileBytesToSave.data()+offset, col.name.data(), col.name.length()); offset += col.name.length();
                    offset = dandb::record::layout::alignTo(offset, 4); 
                }
                offset = dandb::record::layout::alignTo(offset, 8);
            }

            file.write(reinterpret_cast<char*>(fileBytesToSave.data()), fileSerializedSizeToSave);
            if(!file) {
                return dandb::core::Status::IOError("Cannot save to tables metadata file '"+tablesMetaPath_.string() + "': unable to write to file");
            }

            return dandb::core::Status::Ok();

        }

        dandb::core::Status TableManager::initMetaFile() const {

            std::error_code filesystemError;

            const std::filesystem::path parentPath = tablesMetaPath_.parent_path();
            if(!parentPath.empty()) {
                std::filesystem::create_directories(parentPath, filesystemError);
                if(filesystemError) {
                    return dandb::core::Status::IOError("Cannot initialize tables metadata file '" + tablesMetaPath_.string() + "': unable to create parent directories: " + filesystemError.message());
                }
            }

            std::ofstream newFile(tablesMetaPath_, std::ios::binary | std::ios::trunc);
            if(!newFile.is_open()) {
                return dandb::core::Status::IOError("Cannot initialize tables metadata file '" + tablesMetaPath_.string() + "': unable to create file");
            }

            std::array<std::byte, TABLES_META_HEADER_SIZE> header{};
            for(size_t i = 0; i < TABLES_META_MAGIC.size(); i++) {
                header[i] = static_cast<std::byte>(TABLES_META_MAGIC[i]);
            }
            dandb::core::helper::writeUint32(header, TABLES_META_MAGIC.size(), 1);
            dandb::core::helper::writeUint32(header, TABLES_META_MAGIC.size()+4, 0);

            newFile.write(reinterpret_cast<const char*>(header.data()), static_cast<std::streamsize>(header.size()));
            if(!newFile) {
                return dandb::core::Status::IOError("Cannot initialize tables metadata file '" + tablesMetaPath_.string() + "': unable to write initial metadata bytes");
            }

            return dandb::core::Status::Ok();

        }

        dandb::core::Result<dandb::record::Column> TableManager::readColumn(const std::vector<std::byte>& fileBytes, size_t& offset) const {

            const auto corruptionFailure = [this](const std::string& message) {
                return dandb::core::Status::Corruption("Cannot load tables metadata from '" + tablesMetaPath_.string() + "': " + message);
            };

            dandb::record::Column col;

            uint8_t typeCode = static_cast<uint8_t>(fileBytes[offset]); offset++;

            const auto logicalTypeResult = dandb::record::logicalTypeFromCode(typeCode);
            if(!logicalTypeResult.ok()) {
                return corruptionFailure("invalid column logical type");
            }
            col.type = logicalTypeResult.value();

            const std::byte columnFlags = fileBytes[offset];
            if((columnFlags&~COLUMN_KNOWN_FLAGS) != std::byte{0}) {
                return corruptionFailure("invalid column flags");
            }

            col.nullable = (columnFlags&COLUMN_NULLABLE_FLAG) != std::byte{0};
            col.primaryKey = (columnFlags&COLUMN_PRIMARY_KEY_FLAG) != std::byte{0};
            offset++;

            col.stringCapacity = dandb::core::helper::readUint16(fileBytes, offset); offset += 2;

            uint16_t colNameLength = dandb::core::helper::readUint16(fileBytes, offset); offset += 2;

            col.name.resize(colNameLength);
            std::memcpy(col.name.data(), fileBytes.data()+offset, colNameLength);
            offset += colNameLength;

            const size_t alignedColumnNameOffset = dandb::record::layout::alignTo(offset, COLUMN_NAME_ALIGNMENT);
            offset = alignedColumnNameOffset;

            return col;

        }

        dandb::core::Result<TableMetadata> TableManager::readTable(const std::vector<std::byte>& fileBytes, size_t& offset) const {

            const auto corruptionFailure = [this](const std::string& message) {
                return dandb::core::Status::Corruption("Cannot load tables metadata from '" + tablesMetaPath_.string() + "': " + message);
            };

            uint32_t tableId = dandb::core::helper::readUint32(fileBytes, offset); offset += 4;
            dandb::core::PageId firstPageId = dandb::core::helper::readUint32(fileBytes, offset); offset += 4;
            dandb::core::PageId lastPageId = dandb::core::helper::readUint32(fileBytes, offset); offset += 4;
            uint8_t columnCount = static_cast<uint8_t>(fileBytes[offset]);
            offset += 1+TABLE_RESERVED_SIZE;

            if(
                firstPageId == 0 ||
                firstPageId == dandb::core::INVALID_PAGE_ID ||
                lastPageId == 0 ||
                lastPageId == dandb::core::INVALID_PAGE_ID
            ) {
                return corruptionFailure("invalid table page id");
            }

            uint16_t tableNameLength = dandb::core::helper::readUint16(fileBytes, offset); offset += 2;

            std::string tableName; tableName.resize(tableNameLength);
            std::memcpy(tableName.data(), fileBytes.data()+offset, tableNameLength);
            offset += tableNameLength;

            const size_t alignedTableNameOffset = dandb::record::layout::alignTo(offset, COLUMN_NAME_ALIGNMENT);
            offset = alignedTableNameOffset;

            std::vector<dandb::record::Column> columns; columns.reserve(columnCount);
            for(size_t j = 0; j < columnCount; j++) {

                auto columnResult = readColumn(fileBytes, offset);
                if(!columnResult.ok()) {
                    return columnResult.status();
                }

                columns.push_back(std::move(columnResult.value()));

            }

            auto schemaResult = dandb::record::Schema::create(std::move(columns));
            if(!schemaResult.ok()) {
                return corruptionFailure("invalid table schema: " + schemaResult.status().message());
            }

            TableMetadata meta{
                tableId,
                tableName,
                std::move(schemaResult.value()),
                firstPageId,
                lastPageId
            };

            const size_t alignedTableOffset = dandb::record::layout::alignTo(offset, TABLE_ALIGNMENT);
            offset = alignedTableOffset;

            return meta;

        }

        size_t TableManager::serializedSize() const {

            size_t size = 0;

            size += 4; // magic bytes
            size += 4; // next table id
            size += 4; // table count
            size += 4; // reserved

            for(const TableMetadata& table: tablesMetadata_) {
                size += 4; // table id
                size += 4; // first table page id
                size += 4; // last table page id
                size += 1; // column count
                size += 3; // padding
                size += 2; // table name byte length
                size += table.name.length();
                size = dandb::record::layout::alignTo(size, 4);
                for(const dandb::record::Column& col: table.schema.columns()) {
                    size += 1; // logical type code
                    size += 1; // column flags
                    size += 2; // string capacity (bytes)
                    size += 2; // column name byte length
                    size += col.name.length();
                    size = dandb::record::layout::alignTo(size, 4);
                }
                size = dandb::record::layout::alignTo(size, 8);
            }

            return size;

        }

    }
}
