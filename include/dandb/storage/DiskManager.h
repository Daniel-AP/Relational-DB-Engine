#pragma once

#include <dandb/core/Constants.h>
#include <dandb/core/Result.h>

#include <array>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <span>

namespace dandb {
    namespace storage {

        class DiskManager {
            public:
                DiskManager(std::filesystem::path filePath, std::array<char, 4> magic);
                DiskManager(const DiskManager&) = delete;
                DiskManager& operator=(const DiskManager&) = delete;
                DiskManager(DiskManager&&) = delete;
                DiskManager& operator=(DiskManager&&) = delete;

                [[nodiscard]] dandb::core::Result<dandb::core::PageId> allocatePage();
                [[nodiscard]] dandb::core::Status freePage(dandb::core::PageId pageId);
                [[nodiscard]] dandb::core::Status readPage(dandb::core::PageId pageId, std::array<std::byte, dandb::core::PAGE_SIZE_BYTES>& out);
                [[nodiscard]] dandb::core::Status writePage(dandb::core::PageId pageId, const std::array<std::byte, dandb::core::PAGE_SIZE_BYTES>& data);
                dandb::core::PageId pageCount() const;

            private:
                std::filesystem::path filePath_;
                std::array<char, 4> magic_;
                dandb::core::PageId pageCount_;
                dandb::core::PageId firstFreePageId_;
                dandb::core::PageId freePageCount_;

                [[nodiscard]] dandb::core::Status readHeader(std::fstream& file);
                [[nodiscard]] dandb::core::Status writeHeader(std::fstream& file);
                [[nodiscard]] dandb::core::Status readPage(std::fstream& file, dandb::core::PageId pageId, std::array<std::byte, dandb::core::PAGE_SIZE_BYTES>& out);
                [[nodiscard]] dandb::core::Status writePage(std::fstream& file, dandb::core::PageId pageId, const std::array<std::byte, dandb::core::PAGE_SIZE_BYTES>& data);

                void writeUint32(std::span<std::byte> buffer, size_t offset, uint32_t value);
                uint32_t readUint32(std::span<const std::byte> buffer, size_t offset);
        };

    }
}
