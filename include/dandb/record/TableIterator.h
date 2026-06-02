#pragma once

#include <dandb/core/Constants.h>
#include <dandb/buffer/BufferPoolManager.h>
#include <dandb/record/Schema.h>
#include <dandb/record/RID.h>
#include <dandb/core/Result.h>
#include <dandb/record/Row.h>

#include <optional>
#include <utility>

namespace dandb {
    namespace record {

        struct TableIteratorEntry {
            RID rid;
            Row row;
        };
        
        class TableIterator {

            public:
                TableIterator(
                    dandb::buffer::BufferPoolManager& bpm,
                    const dandb::record::Schema& schema,
                    dandb::core::PageId firstPageId
                );

                dandb::core::Result<bool> hasNext();
                dandb::core::Result<TableIteratorEntry> next();

            private:
                dandb::buffer::BufferPoolManager& bpm_;
                dandb::record::Schema schema_;
                dandb::core::PageId nextPageId_;
                dandb::core::SlotId nextSlotId_ = 0;
                std::optional<TableIteratorEntry> cachedEntry_ = std::nullopt;

                dandb::core::Result<TableIteratorEntry> loadNext();

        };

    }
}