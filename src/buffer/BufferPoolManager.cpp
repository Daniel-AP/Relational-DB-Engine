#include <dandb/buffer/BufferPoolManager.h>

#include <array>
#include <string>

namespace dandb {
    namespace buffer {

        BufferPoolManager::BufferPoolManager(size_t poolSize, dandb::storage::DiskManager& dm)
            : diskManager_(dm),
              pages_(poolSize),
              freeSlots_(poolSize),
              lruReplacer_(poolSize)
        {
            for(size_t i = 0; i < poolSize; i++) freeSlots_[i] = i;
        }

        BufferPoolManager::~BufferPoolManager() {
            static_cast<void>(saveAllPagesToDisk());
        }

        size_t BufferPoolManager::getPoolSize() const {
            return pages_.size();
        }

        dandb::core::Result<dandb::buffer::Page*> BufferPoolManager::newPage() {

            const auto freeSlotResult = getAvailableSlot();
            if(!freeSlotResult.ok()) {
                return freeSlotResult.status();
            }

            const auto availableSlot = freeSlotResult.value();
            size_t freeSlotId = availableSlot.slotId;

            const auto newPageResult = diskManager_.allocatePage();
            if(!newPageResult.ok()) {
                const auto restoreStatus = restoreAvailableSlot(availableSlot);
                if(!restoreStatus.ok()) {
                    return restoreStatus;
                }

                return newPageResult.status();
            }

            dandb::core::PageId newPageId = newPageResult.value();

            pages_[freeSlotId].reset();
            pages_[freeSlotId].setPageId(newPageId);
            pages_[freeSlotId].pin();

            pageSlot_[newPageId] = freeSlotId;

            return &pages_[freeSlotId];

        }

        dandb::core::Result<dandb::buffer::Page*> BufferPoolManager::fetchPage(dandb::core::PageId pageId) {

            const auto it = pageSlot_.find(pageId);

            if(it != pageSlot_.end()) {

                pages_[it->second].pin();
                const auto pinStatus = lruReplacer_.pin(it->second);
                if(!pinStatus.ok()) {
                    return pinStatus;
                }

                return &pages_[it->second];

            }

            const auto freeSlotResult = getAvailableSlot();
            if(!freeSlotResult.ok()) {
                return freeSlotResult.status();
            }

            const auto availableSlot = freeSlotResult.value();
            size_t freeSlotId = availableSlot.slotId;

            std::array<std::byte, dandb::core::PAGE_SIZE_BYTES> pageData{};
            const auto readStatus = diskManager_.readPage(pageId, pageData);
            if(!readStatus.ok()) {
                const auto restoreStatus = restoreAvailableSlot(availableSlot);
                if(!restoreStatus.ok()) {
                    return restoreStatus;
                }

                return readStatus;
            }

            pages_[freeSlotId].reset();
            pages_[freeSlotId].data() = pageData;
            pages_[freeSlotId].setPageId(pageId);
            pages_[freeSlotId].pin();

            pageSlot_.insert({ pageId, freeSlotId });

            return &pages_[freeSlotId];

        }

        dandb::core::Status BufferPoolManager::unpinPage(dandb::core::PageId pageId, bool isDirty) {

            const auto it = pageSlot_.find(pageId);

            if(it == pageSlot_.end()) {
                return dandb::core::Status::NotFound("Cannot unpin page "+std::to_string(pageId)+": page is not loaded into the buffer pool");
            }

            const auto unpinStatus = pages_[it->second].unpin();
            if(!unpinStatus.ok()) {
                return unpinStatus;
            }
            if(isDirty) pages_[it->second].setDirty(true);

            if(pages_[it->second].pinCount() == 0) {
                const auto replacerStatus = lruReplacer_.unpin(it->second);
                if(!replacerStatus.ok()) {
                    return replacerStatus;
                }
            }

            return dandb::core::Status::Ok();

        }

        dandb::core::Status BufferPoolManager::savePageToDisk(dandb::core::PageId pageId) {

            const auto it = pageSlot_.find(pageId);

            if(it == pageSlot_.end()) {
                return dandb::core::Status::NotFound("Cannot save page "+std::to_string(pageId)+" to disk: page is not loaded into the buffer pool");
            }

            if(pages_[it->second].isDirty()) {
                const auto saveStatus = diskManager_.writePage(pageId, pages_[it->second].data());
                if(!saveStatus.ok()) {
                    return saveStatus;
                }
                pages_[it->second].setDirty(false);
            }

            return dandb::core::Status::Ok();

        }

        dandb::core::Status BufferPoolManager::saveAllPagesToDisk() {

            for(const auto& page: pages_) {
                if(page.pageId() == dandb::core::INVALID_PAGE_ID) continue;
                const auto saveStatus = savePageToDisk(page.pageId());
                if(!saveStatus.ok()) {
                    return saveStatus;
                }
            }

            return dandb::core::Status::Ok();

        }

        dandb::core::Result<BufferPoolManager::AvailableSlot> BufferPoolManager::getAvailableSlot() {

            size_t freeSlotId;

            if(!freeSlots_.empty()) {

                freeSlotId = freeSlots_.back();
                freeSlots_.pop_back();

                return AvailableSlot{ freeSlotId, false, dandb::core::INVALID_PAGE_ID };

            }

            const auto freeSlotResult = lruReplacer_.getVictim();
            if(!freeSlotResult.ok()) {
                return dandb::core::Status::Internal("Cannot get an available slot: all buffer pool pages are pinned");
            }

            size_t victimSlotId = freeSlotResult.value();
            const auto victimPageId = pages_[victimSlotId].pageId();
            if(pages_[victimSlotId].isDirty()) {
                const auto savePageStatus = savePageToDisk(victimPageId);
                if(!savePageStatus.ok()) {
                    const auto restoreStatus = lruReplacer_.unpin(victimSlotId);
                    if(!restoreStatus.ok()) {
                        return restoreStatus;
                    }

                    return savePageStatus;
                }
            }

            pageSlot_.erase(victimPageId);

            return AvailableSlot{ victimSlotId, true, victimPageId };
            
        }

        dandb::core::Status BufferPoolManager::restoreAvailableSlot(const AvailableSlot& availableSlot) {

            if(!availableSlot.evictedPage) {
                freeSlots_.push_back(availableSlot.slotId);

                return dandb::core::Status::Ok();
            }

            pageSlot_[availableSlot.evictedPageId] = availableSlot.slotId;
            
            return lruReplacer_.unpin(availableSlot.slotId);

        }

    }
}
