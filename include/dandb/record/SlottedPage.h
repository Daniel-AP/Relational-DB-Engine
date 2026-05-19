#pragma once

#include <dandb/core/Constants.h>
#include <dandb/core/Result.h>
#include <dandb/core/Status.h>

#include <array>
#include <span>
#include <vector>

namespace dandb {
    namespace record {

        class SlottedPage {

            public:
                SlottedPage(std::array<std::byte, dandb::core::PAGE_SIZE_BYTES>& page);

                static void initialize(std::array<std::byte, dandb::core::PAGE_SIZE_BYTES>& page, dandb::core::PageId pageId);

                dandb::core::PageId pageId() const;
                dandb::core::PageId nextPageId() const;
                void setNextPageId(dandb::core::PageId pageId);

                uint16_t slotCount() const;
                uint16_t freeStartOffset() const;
                uint16_t freeEndOffset() const;

                dandb::core::Result<dandb::core::SlotId> insertRow(std::span<const std::byte> payload);
                dandb::core::Result<std::vector<std::byte>> readRow(dandb::core::SlotId slotId);
                dandb::core::Status deleteRow(dandb::core::SlotId slotId);
                dandb::core::Status updateRow(dandb::core::SlotId slotId, std::span<const std::byte> payload);

            private:
                std::array<std::byte, dandb::core::PAGE_SIZE_BYTES>& page_;

                uint16_t slotOffset(dandb::core::SlotId slotId) const;
                bool hasSpaceFor(size_t payloadSize) const;

        };

    }
}