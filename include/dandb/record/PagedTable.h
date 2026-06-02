#pragma once

#include <dandb/buffer/BufferPoolManager.h>
#include <dandb/record/Schema.h>
#include <dandb/core/Constants.h>
#include <dandb/core/Result.h>
#include <dandb/core/Status.h>
#include <dandb/record/RID.h>
#include <dandb/record/Row.h>

namespace dandb {
    namespace record {

        class PagedTable {

            public:

                PagedTable(
                    dandb::buffer::BufferPoolManager& bpm,
                    dandb::record::Schema schema,
                    dandb::core::PageId firstPageId,
                    dandb::core::PageId lastPageId
                );

                PagedTable(const PagedTable&) = delete;
                PagedTable& operator=(const PagedTable&) = delete;
                PagedTable(PagedTable&&) = delete;
                PagedTable& operator=(PagedTable&&) = delete;

                [[nodiscard]] dandb::core::Result<RID> insertRow(const Row& row);
                [[nodiscard]] dandb::core::Result<Row> readRow(const RID& rid);
                [[nodiscard]] dandb::core::Status deleteRow(const RID& rid);
                [[nodiscard]] dandb::core::Status updateRow(const RID& rid, const Row& row);

                dandb::core::PageId firstPageId() const;
                dandb::core::PageId lastPageId() const;

            private:

                dandb::buffer::BufferPoolManager& bpm_;
                dandb::record::Schema schema_;
                dandb::core::PageId firstPageId_;
                dandb::core::PageId lastPageId_;

        };

    }
}