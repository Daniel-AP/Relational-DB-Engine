#include <dandb/buffer/Page.h>

#include <string>
#include <algorithm>

namespace dandb {
    namespace buffer {

        Page::Page()
            : id_(dandb::core::INVALID_PAGE_ID),
              data_{},
              pinCount_(0),
              isDirty_(false)
        {}

        dandb::core::PageId Page::pageId() const {
            return id_;
        }

        void Page::setPageId(dandb::core::PageId id) {
            id_ = id;
        }

        std::array<std::byte, dandb::core::PAGE_SIZE_BYTES>& Page::data() {
            return data_;
        }

        const std::array<std::byte, dandb::core::PAGE_SIZE_BYTES>& Page::data() const {
            return data_;
        }

        uint32_t Page::pinCount() const {
            return pinCount_;
        }

        void Page::pin() {
            pinCount_++;
        }

        dandb::core::Status Page::unpin() {

            if(pinCount_ == 0) {
                return dandb::core::Status::InvalidArgument("Unable to unpin page " + std::to_string(id_) + ": page is unpinned");
            }

            pinCount_--;

            return dandb::core::Status::Ok();

        }

        bool Page::isDirty() const {
            return isDirty_;
        }

        void Page::setDirty(bool dirty) {
            isDirty_ = dirty;
        }

        void Page::reset() {

            id_ = dandb::core::INVALID_PAGE_ID;
            pinCount_ = 0;
            isDirty_ = false;
            std::fill(data_.begin(), data_.end(), std::byte{0});

        }

    }
}