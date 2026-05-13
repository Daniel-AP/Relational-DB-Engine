#pragma once

#include <dandb/core/Constants.h>
#include <dandb/core/Status.h>

#include <array>
#include <cstddef>
#include <cstdint>

namespace dandb {
    namespace buffer {

        class Page {

            public:
                Page();

                dandb::core::PageId pageId() const;
                void setPageId(dandb::core::PageId id);

                std::array<std::byte, dandb::core::PAGE_SIZE_BYTES>& data();
                const std::array<std::byte, dandb::core::PAGE_SIZE_BYTES>& data() const;

                uint32_t pinCount() const;
                void pin();
                [[nodiscard]] dandb::core::Status unpin();

                bool isDirty() const;
                void setDirty(bool dirty);

                void reset();

            private:
                dandb::core::PageId id_;
                std::array<std::byte, dandb::core::PAGE_SIZE_BYTES> data_;
                uint32_t pinCount_;
                bool isDirty_;
                
        };

    }
}