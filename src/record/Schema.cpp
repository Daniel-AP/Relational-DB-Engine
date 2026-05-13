#include <dandb/record/Schema.h>
#include <dandb/core/Status.h>

#include <utility>
#include <unordered_set>

namespace dandb {
    namespace record {

        Schema::Schema(std::vector<Column> columns)
            : columns_(columns)
        {}

        dandb::core::Result<Schema> Schema::create(std::vector<Column> columns) {
            
            if(columns.empty()) {
                return dandb::core::Status::InvalidArgument("Cannot create schema with no columns");
            }

            if(columns.size() > 255) {
                return dandb::core::Status::InvalidArgument("Cannot create schema with more than 255 columns");
            }

            bool pk = false;
            std::unordered_set<std::string> seenNames;

            for(const auto& col: columns) {

                if(col.primaryKey) {
                    if(pk) {
                        return dandb::core::Status::InvalidArgument("Cannot create schema with more than 1 primary key");
                    } else {
                        pk = true;
                    }
                }

                if(col.primaryKey && col.nullable) {
                    return dandb::core::Status::InvalidArgument("Cannot create schema: a primary key cannot be nullable");
                }

                if(seenNames.find(col.name) != seenNames.end()) {
                    return dandb::core::Status::InvalidArgument("Cannot create schema: some columns have duplicate names");
                } else {
                    seenNames.insert(col.name);
                }

                if(col.type == LogicalType::String && col.stringCapacity == 0) {
                    return dandb::core::Status::InvalidArgument("Cannot create schema: some columns of type string have no string capacity");
                }

                if(col.type != LogicalType::String && col.stringCapacity != 0) {
                    return dandb::core::Status::InvalidArgument("Cannot create schema: some non-string columns string capacity");
                }

            }

            return Schema{std::move(columns)};

        }

        size_t Schema::columnCount() const {
            return columns_.size();
        }

        const Column& Schema::column(size_t index) const {
            return columns_[index];
        }

        const std::vector<Column>& Schema::columns() const {
            return columns_;
        }

    }
}