#pragma once

#include <dandb/core/Constants.h>
#include <dandb/storage/DiskManager.h>
#include <dandb/buffer/Page.h>
#include <dandb/buffer/LRUReplacer.h>
#include <dandb/core/Status.h>
#include <dandb/core/Result.h>

#include <cstddef>
#include <vector>
#include <unordered_map>

namespace dandb {
    namespace buffer {

        class BufferPoolManager {

            public:
                BufferPoolManager(size_t poolSize, dandb::storage::DiskManager& bpm);
                ~BufferPoolManager();

                size_t getPoolSize() const;

                dandb::core::Result<dandb::buffer::Page*> newPage();
                dandb::core::Result<dandb::buffer::Page*> fetchPage(dandb::core::PageId pageId);
                dandb::core::Status unpinPage(dandb::core::PageId pageId, bool isDirty);
                dandb::core::Status savePageToDisk(dandb::core::PageId pageId);
                dandb::core::Status saveAllPagesToDisk();

            private:
                struct AvailableSlot {
                    size_t slotId;
                    bool evictedPage;
                    dandb::core::PageId evictedPageId;
                };

                dandb::storage::DiskManager& diskManager_;
                std::vector<dandb::buffer::Page> pages_;
                std::vector<size_t> freeSlots_;
                std::unordered_map<dandb::core::PageId, size_t> pageSlot_;
                dandb::buffer::LRUReplacer lruReplacer_;

                dandb::core::Result<AvailableSlot> getAvailableSlot();
                dandb::core::Status restoreAvailableSlot(const AvailableSlot& availableSlot);

        };

    }
}
