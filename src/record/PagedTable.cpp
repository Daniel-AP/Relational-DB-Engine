#include <dandb/record/PagedTable.h>
#include <dandb/record/Codec.h>
#include <dandb/record/SlottedPage.h>
#include <dandb/buffer/Page.h>

#include <vector>

namespace dandb {
    namespace record {

        PagedTable::PagedTable(
            dandb::buffer::BufferPoolManager& bpm,
            dandb::record::Schema schema,
            dandb::core::PageId firstPageId,
            dandb::core::PageId lastPageId
        ) : 
            bpm_(bpm),
            schema_(schema),
            firstPageId_(firstPageId),
            lastPageId_(lastPageId)
        {}

        dandb::core::Result<RID> PagedTable::insertRow(const Row& row) {

            const auto encodedRowResult = dandb::record::Codec::encode(schema_, row);
            if(!encodedRowResult.ok()) {
                return encodedRowResult.status();
            }

            std::vector<std::byte> encodedRow = encodedRowResult.value();
            const dandb::core::PageId oldLastPageId = lastPageId_;

            const auto lastPageResult = bpm_.fetchPage(oldLastPageId);
            if(!lastPageResult.ok()) {
                return lastPageResult.status();
            }

            dandb::buffer::Page* lastPage = lastPageResult.value();
            dandb::record::SlottedPage slottedLastPage(lastPage->data());

            const auto insertLastPageResult = slottedLastPage.insertRow(encodedRow);
            if(insertLastPageResult.ok()) {

                dandb::core::SlotId insertSlotId = insertLastPageResult.value();
                const auto unpinStatus = bpm_.unpinPage(oldLastPageId, true);
                if(!unpinStatus.ok()) {
                    return unpinStatus;
                }

                return RID{oldLastPageId, insertSlotId};
            }

            const auto unpinLastPageStatus = bpm_.unpinPage(oldLastPageId, false);
            if(!unpinLastPageStatus.ok()) {
                return unpinLastPageStatus;
            }

            const auto newPageResult = bpm_.newPage();
            if(!newPageResult.ok()) {
                return newPageResult.status();
            }

            dandb::buffer::Page* newPage = newPageResult.value();
            const dandb::core::PageId newPageId = newPage->pageId();

            dandb::record::SlottedPage::initialize(newPage->data(), newPageId);
            dandb::record::SlottedPage slottedNewPage(newPage->data());
            
            const auto insertNewPageResult = slottedNewPage.insertRow(encodedRow);
            if(!insertNewPageResult.ok()) {
                const auto unpinNewPageStatus = bpm_.unpinPage(newPageId, true);
                if(!unpinNewPageStatus.ok()) {
                    return unpinNewPageStatus;
                }

                return insertNewPageResult.status();
            }

            dandb::core::SlotId insertSlotId = insertNewPageResult.value();
            const auto unpinNewPageStatus = bpm_.unpinPage(newPageId, true);
            if(!unpinNewPageStatus.ok()) {
                return unpinNewPageStatus;
            }

            const auto oldLastPageResult = bpm_.fetchPage(oldLastPageId);
            if(!oldLastPageResult.ok()) {
                return oldLastPageResult.status();
            }

            dandb::buffer::Page* oldLastPage = oldLastPageResult.value();
            dandb::record::SlottedPage oldSlottedLastPage(oldLastPage->data());
            oldSlottedLastPage.setNextPageId(newPageId);

            const auto unpinOldLastPageStatus = bpm_.unpinPage(oldLastPageId, true);
            if(!unpinOldLastPageStatus.ok()) {
                return unpinOldLastPageStatus;
            }

            lastPageId_ = newPageId;

            return RID{newPageId, insertSlotId};

        }

    }
}
