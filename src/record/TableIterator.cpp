#include <dandb/record/TableIterator.h>
#include <dandb/record/SlottedPage.h>
#include <dandb/record/Codec.h>

namespace dandb {
    namespace record {

        TableIterator::TableIterator(
            dandb::buffer::BufferPoolManager& bpm,
            const dandb::record::Schema& schema,
            dandb::core::PageId firstPageId
        ) :
            bpm_(bpm),
            schema_(schema),
            nextPageId_(firstPageId)
        {}

        dandb::core::Result<bool> TableIterator::hasNext() {

            if(cachedEntry_.has_value()) return true;

            const auto loadNextResult = loadNext();

            if(!loadNextResult.ok()) {
                if(loadNextResult.status().code() == dandb::core::StatusCode::NotFound) {
                    return false;
                } else {
                    return loadNextResult.status();
                }
            }

            cachedEntry_ = loadNextResult.value();

            return true;
            
        }

        dandb::core::Result<TableIteratorEntry> TableIterator::next() {

            const auto hasNextResult = hasNext();
            if(!hasNextResult.ok()) {
                return hasNextResult.status();
            }

            if(!hasNextResult.value()) {
                return dandb::core::Status::NotFound("Cannot load next table row: iterator is exhausted");
            }

            TableIteratorEntry entry = *cachedEntry_;
            cachedEntry_.reset();

            return entry;


        }

        dandb::core::Result<TableIteratorEntry> TableIterator::loadNext() {

            while(nextPageId_ != dandb::core::INVALID_PAGE_ID && nextSlotId_ != dandb::core::INVALID_SLOT_ID) {

                const auto fetchPageResult = bpm_.fetchPage(nextPageId_);
                if(!fetchPageResult.ok()) {
                    return fetchPageResult.status();
                }

                dandb::buffer::Page* page = fetchPageResult.value();
                SlottedPage slottedPage(page->data());

                if(nextSlotId_ >= slottedPage.slotCount()) {
                    nextPageId_ = slottedPage.nextPageId();
                    nextSlotId_ = (nextPageId_ == dandb::core::INVALID_PAGE_ID ? dandb::core::INVALID_SLOT_ID : 0);
                    const auto unpinStatus = bpm_.unpinPage(page->pageId(), false);
                    if(!unpinStatus.ok()) {
                        return unpinStatus;
                    }
                    continue;
                }

                const auto readRowResult = slottedPage.readRow(nextSlotId_);

                if(!readRowResult.ok()) {
                    if(readRowResult.status().code() != dandb::core::StatusCode::NotFound) {
                        const auto unpinStatus = bpm_.unpinPage(page->pageId(), false);
                        if(!unpinStatus.ok()) {
                            return unpinStatus;
                        }
                        return readRowResult.status();
                    }
                    // it's a tombstone (deleted row)
                    const auto unpinStatus = bpm_.unpinPage(page->pageId(), false);
                    if(!unpinStatus.ok()) {
                        return unpinStatus;
                    }
                    nextSlotId_++;
                    continue;
                }

                const std::vector<std::byte> rawRow = readRowResult.value();
                const auto decodedRowResult = dandb::record::Codec::decode(schema_, rawRow);
                if(!decodedRowResult.ok()) {
                    const auto unpinStatus = bpm_.unpinPage(page->pageId(), false);
                    if(!unpinStatus.ok()) {
                        return unpinStatus;
                    }
                    return decodedRowResult.status();
                }

                const Row row = decodedRowResult.value();

                const auto unpinStatus = bpm_.unpinPage(page->pageId(), false);
                if(!unpinStatus.ok()) {
                    return unpinStatus;
                }
                
                return TableIteratorEntry{
                    RID{nextPageId_, nextSlotId_++},
                    row
                };

            }

            return dandb::core::Status::NotFound("Cannot load next table row: iterator is exhausted");

        }

    }
}