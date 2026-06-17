#include <dandb/record/SlottedPage.h>
#include <dandb/core/Helper.h>

#include <string>
#include <algorithm>
#include <cstring>

namespace {

    constexpr uint16_t TABLE_PAGE_KIND = 1;
    constexpr uint16_t TABLE_PAGE_HEADER_SIZE = 24;
    constexpr uint16_t SLOT_ENTRY_SIZE = 4;

    constexpr size_t PAGE_ID_OFFSET = 0;
    constexpr size_t PAGE_KIND_OFFSET = 4;
    constexpr size_t NEXT_PAGE_ID_OFFSET = 8;
    constexpr size_t SLOT_COUNT_OFFSET = 12;
    constexpr size_t FREE_START_OFFSET = 14;
    constexpr size_t FREE_END_OFFSET = 16;

}

namespace dandb {
    namespace record {

        SlottedPage::SlottedPage(std::array<std::byte, dandb::core::PAGE_SIZE_BYTES>& page)
            : page_(page)
        {}

        void SlottedPage::initialize(std::array<std::byte, dandb::core::PAGE_SIZE_BYTES>& page, dandb::core::PageId pageId) {

            page.fill(std::byte{0});

            size_t offset = PAGE_ID_OFFSET;
            
            dandb::core::helper::writeUint32(page, offset, pageId); offset += 4;
            dandb::core::helper::writeUint16(page, PAGE_KIND_OFFSET, TABLE_PAGE_KIND); offset += 2;
            offset += 2;
            dandb::core::helper::writeUint32(page, offset, dandb::core::INVALID_PAGE_ID); offset += 4;
            dandb::core::helper::writeUint16(page, offset, 0); offset += 2;
            dandb::core::helper::writeUint16(page, offset, TABLE_PAGE_HEADER_SIZE); offset += 2;
            dandb::core::helper::writeUint16(page, offset, static_cast<uint16_t>(dandb::core::PAGE_SIZE_BYTES)); offset += 2;
            offset += 2;
            offset += 4;

        }

        dandb::core::PageId SlottedPage::pageId() const {
            return dandb::core::helper::readUint32(page_, PAGE_ID_OFFSET);
        }

        dandb::core::PageId SlottedPage::nextPageId() const {
            return dandb::core::helper::readUint32(page_, NEXT_PAGE_ID_OFFSET);
        }

        void SlottedPage::setNextPageId(dandb::core::PageId pageId) {
            dandb::core::helper::writeUint32(page_, NEXT_PAGE_ID_OFFSET, pageId);
        }

        uint16_t SlottedPage::slotCount() const {
            return dandb::core::helper::readUint16(page_, SLOT_COUNT_OFFSET);
        }

        uint16_t SlottedPage::freeStartOffset() const {
            return dandb::core::helper::readUint16(page_, FREE_START_OFFSET);
        }

        uint16_t SlottedPage::freeEndOffset() const {
            return dandb::core::helper::readUint16(page_, FREE_END_OFFSET);
        }

        dandb::core::Result<dandb::core::SlotId> SlottedPage::insertRow(std::span<const std::byte> payload) {

            const size_t payloadSize = payload.size();

            if(!hasSpaceFor(payloadSize)) {
                return dandb::core::Status::InvalidArgument("Cannot insert row in page "+std::to_string(pageId())+": available space is insufficient");
            }

            const dandb::core::SlotId newSlotId = slotCount();
            const uint16_t newSlotOffset = slotOffset(newSlotId);
            const uint16_t newRowOffset = freeEndOffset()-payloadSize;

            std::copy(payload.begin(), payload.end(), page_.begin()+newRowOffset);
            dandb::core::helper::writeUint16(page_, newSlotOffset, newRowOffset);
            dandb::core::helper::writeUint16(page_, newSlotOffset+2, payloadSize);

            dandb::core::helper::writeUint16(page_, SLOT_COUNT_OFFSET, slotCount()+1);
            dandb::core::helper::writeUint16(page_, FREE_START_OFFSET, freeStartOffset()+SLOT_ENTRY_SIZE);
            dandb::core::helper::writeUint16(page_, FREE_END_OFFSET, freeEndOffset()-payloadSize);

            return newSlotId;

        }

        dandb::core::Result<std::vector<std::byte>> SlottedPage::readRow(dandb::core::SlotId slotId) {

            if(slotId >= slotCount()) {
                return dandb::core::Status::InvalidArgument("Cannot read row with : "+std::to_string(slotId)+" is not a valid slot id");
            }

            uint16_t offset = slotOffset(slotId);
            uint16_t rowOffset = dandb::core::helper::readUint16(page_, offset);
            size_t rowSize = static_cast<size_t>(dandb::core::helper::readUint16(page_, offset+2));

            if(rowSize == 0) {
                return dandb::core::Status::NotFound("Cannot read row: slot "+std::to_string(slotId)+" no longer contains a row");
            }

            std::vector<std::byte> row(rowSize);
            std::memcpy(row.data(), page_.data()+rowOffset, rowSize);

            return row;

        }

        dandb::core::Status SlottedPage::deleteRow(dandb::core::SlotId slotId) {

            if(slotId >= slotCount()) {
                return dandb::core::Status::InvalidArgument("Cannot delete row: "+std::to_string(slotId)+" is not a valid slot id");
            }

            uint16_t offset = slotOffset(slotId);
            size_t rowSize = static_cast<size_t>(dandb::core::helper::readUint16(page_, offset+2));

            if(rowSize == 0) {
                return dandb::core::Status::NotFound("Cannot delete row: slot "+std::to_string(slotId)+" no longer contains a row");
            }

            dandb::core::helper::writeUint16(page_, offset+2, 0);

            return dandb::core::Status::Ok();

        }

        dandb::core::Status SlottedPage::updateRow(dandb::core::SlotId slotId, std::span<const std::byte> payload) {

            if(slotId >= slotCount()) {
                return dandb::core::Status::InvalidArgument("Cannot update row : "+std::to_string(slotId)+" is not a valid slot id");
            }

            uint16_t offset = slotOffset(slotId);
            uint16_t rowOffset = dandb::core::helper::readUint16(page_, offset);
            size_t rowSize = static_cast<size_t>(dandb::core::helper::readUint16(page_, offset+2));

            if(rowSize == 0) {
                return dandb::core::Status::NotFound("Cannot update row: slot "+std::to_string(slotId)+" no longer contains a row");
            }

            const size_t rowStart = rowOffset;
            const size_t rowEnd = rowStart+rowSize;

            if(rowEnd > dandb::core::PAGE_SIZE_BYTES || rowStart < freeEndOffset()) {
                return dandb::core::Status::Corruption("Cannot update row: slot "+std::to_string(slotId)+" has corrupt row bounds");
            }

            size_t payloadSize = payload.size();

            if(payloadSize != rowSize) {
                return dandb::core::Status::InvalidArgument("Cannot update row: payload and encoded size differ");
            }

            std::memcpy(page_.data()+rowOffset, payload.data(), payloadSize);

            return dandb::core::Status::Ok();

        }

        uint16_t SlottedPage::slotOffset(dandb::core::SlotId slotId) const {
            return TABLE_PAGE_HEADER_SIZE+slotId*SLOT_ENTRY_SIZE;
        }

        bool SlottedPage::hasSpaceFor(size_t payloadSize) const {

            if(freeStartOffset() > freeEndOffset()) return false;

            size_t availableSpace = freeEndOffset()-freeStartOffset();
            size_t neededSpace = payloadSize+SLOT_ENTRY_SIZE;

            return neededSpace <= availableSpace;

        }

    }
}
