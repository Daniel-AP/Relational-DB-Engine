#include <dandb/core/Helper.h>
#include <dandb/storage/DiskManager.h>

#include <fstream>
#include <stdexcept>

namespace {

    constexpr size_t MAGIC_SIZE = 4;
    constexpr dandb::core::PageId DISK_HEADER_PAGE_ID = 0;
    constexpr dandb::core::PageId INITIAL_PAGE_COUNT = 1;
    constexpr uint16_t FREE_PAGE_KIND = 0xFFFF;

    constexpr size_t PAGE_ID_OFFSET = 0;
    constexpr size_t PAGE_SIZE_OFFSET = 4;
    constexpr size_t PAGE_KIND_OFFSET = 4;
    constexpr size_t PAGE_COUNT_OFFSET = 8;
    constexpr size_t NEXT_FREE_PAGE_ID_OFFSET = 8;
    constexpr size_t FIRST_FREE_PAGE_ID_OFFSET = 12;
    constexpr size_t FREE_PAGE_COUNT_OFFSET = 16;

    std::streamoff pageFileOffset(dandb::core::PageId pageId) {

        return static_cast<std::streamoff>(pageId)*static_cast<std::streamoff>(dandb::core::PAGE_SIZE_BYTES);

    }

}

namespace dandb {
    namespace storage {

        DiskManager::DiskManager(std::filesystem::path filePath, std::array<char, 4> magic)
            : filePath_(std::move(filePath)), magic_(magic) {

            if(!std::filesystem::exists(filePath_)) {

                if(!filePath_.parent_path().empty()) {
                    try {
                        std::filesystem::create_directories(filePath_.parent_path());
                    } catch(const std::filesystem::filesystem_error& error) {
                        throw std::runtime_error("Cannot create disk file '" + filePath_.string() + "': " + error.what());
                    }
                }

                pageCount_ = INITIAL_PAGE_COUNT;
                firstFreePageId_ = dandb::core::INVALID_PAGE_ID;
                freePageCount_ = 0;

                std::fstream file(filePath_, std::ios::binary | std::ios::in | std::ios::out | std::ios::trunc);
                if(!file.is_open()) {
                    throw std::runtime_error("Cannot create disk file '" + filePath_.string() + "': unable to open file");
                }

                const auto status = writeHeader(file);
                file.close();

                if(!status.ok()) {
                    throw std::runtime_error(status.message());
                }

                return;
            }

            std::fstream file(filePath_, std::ios::binary | std::ios::in | std::ios::out);
            if(!file.is_open()) {
                throw std::runtime_error("Cannot open disk file '" + filePath_.string() + "': unable to open file");
            }

            const auto status = readHeader(file);
            file.close();

            if(!status.ok()) {
                throw std::runtime_error(status.message());
            }

        }

        dandb::core::Result<dandb::core::PageId> DiskManager::allocatePage() {

            std::fstream file(filePath_, std::ios::binary | std::ios::in | std::ios::out);
            if(!file.is_open()) {
                return dandb::core::Status::IOError("Cannot allocate page in disk file '" + filePath_.string() + "': unable to open file");
            }

            dandb::core::PageId allocatedPageId;

            if(freePageCount_ > 0) {

                allocatedPageId = firstFreePageId_;
                freePageCount_--;

                std::array<std::byte, dandb::core::PAGE_SIZE_BYTES> currentPage{};
                const auto readStatus = readPage(file, allocatedPageId, currentPage);

                if(!readStatus.ok()) {
                    freePageCount_++;
                    return readStatus;
                }

                firstFreePageId_ = dandb::core::helper::readUint32(currentPage, NEXT_FREE_PAGE_ID_OFFSET);

                std::array<std::byte, dandb::core::PAGE_SIZE_BYTES> newPage{};

                file.clear();
                file.seekp(pageFileOffset(allocatedPageId), std::ios::beg);
                file.write(reinterpret_cast<char*>(newPage.data()), dandb::core::PAGE_SIZE_BYTES);
                file.flush();

                if(!file) {
                    freePageCount_++;
                    firstFreePageId_ = allocatedPageId;
                    file.close();

                    return dandb::core::Status::IOError("Cannot allocate page in disk file '" + filePath_.string() + "': unable to write page bytes");
                }

                const auto headerStatus = writeHeader(file);
                if(!headerStatus.ok()) {
                    freePageCount_++;
                    firstFreePageId_ = allocatedPageId;

                    const auto pageRollbackStatus = writePage(file, allocatedPageId, currentPage);
                    if(!pageRollbackStatus.ok()) {
                        return dandb::core::Status::IOError("Cannot rollback allocated free page " + std::to_string(allocatedPageId) + " in disk file '" + filePath_.string() + "': " + pageRollbackStatus.message());
                    }

                    file.close();

                    return headerStatus;
                }

            } else if(pageCount_ == dandb::core::INVALID_PAGE_ID) {
                return dandb::core::Status::Internal("Cannot allocate page: the maximum number of pages has been reached");
            } else {

                allocatedPageId = pageCount_;
                pageCount_++;

                std::array<std::byte, dandb::core::PAGE_SIZE_BYTES> newPage{};

                file.clear();
                file.seekp(pageFileOffset(allocatedPageId), std::ios::beg);
                file.write(reinterpret_cast<char*>(newPage.data()), dandb::core::PAGE_SIZE_BYTES);
                file.flush();

                if(!file) {
                    pageCount_--;
                    file.close();

                    return dandb::core::Status::IOError("Cannot allocate page in disk file '" + filePath_.string() + "': unable to write page bytes");
                }

                const auto status = writeHeader(file);
                if(!status.ok()) {
                    pageCount_--;
                    file.close();

                    return status;
                }

            }

            file.close();

            return allocatedPageId;

        }

        dandb::core::Status DiskManager::freePage(dandb::core::PageId pageId) {

            if(pageId == DISK_HEADER_PAGE_ID) {
                return dandb::core::Status::InvalidArgument("Cannot free page 0 from disk file '" + filePath_.string() + "': page 0 is reserved for the disk header");
            }

            if(pageId >= pageCount_) {
                return dandb::core::Status::InvalidArgument("Unable to free page " + std::to_string(pageId) + ": page does not exist");
            }

            std::fstream file(filePath_, std::ios::binary | std::ios::in | std::ios::out);
            if(!file.is_open()) {
                return dandb::core::Status::IOError("Cannot free page in disk file '" + filePath_.string() + "': unable to open file");
            }

            std::array<std::byte, dandb::core::PAGE_SIZE_BYTES> currentPage{};
            const auto readStatus = readPage(file, pageId, currentPage);

            if(!readStatus.ok()) {
                return readStatus;
            }

            if(dandb::core::helper::readUint16(currentPage, PAGE_KIND_OFFSET) == FREE_PAGE_KIND) {
                return dandb::core::Status::InvalidArgument("Unable to free page " + std::to_string(pageId) + ": page is already free");
            }

            std::array<std::byte, dandb::core::PAGE_SIZE_BYTES> freedPage{};

            dandb::core::helper::writeUint32(freedPage, PAGE_ID_OFFSET, pageId);
            dandb::core::helper::writeUint16(freedPage, PAGE_KIND_OFFSET, FREE_PAGE_KIND);
            dandb::core::helper::writeUint32(freedPage, NEXT_FREE_PAGE_ID_OFFSET, firstFreePageId_);

            const auto previousFirstFreePageId = firstFreePageId_;
            const auto previousFreePageCount = freePageCount_;

            const auto writeStatus = writePage(file, pageId, freedPage);

            if(!writeStatus.ok()) {
                return writeStatus;
            }

            firstFreePageId_ = pageId;
            freePageCount_++;

            const auto headerStatus = writeHeader(file);

            if(!headerStatus.ok()) {
                firstFreePageId_ = previousFirstFreePageId;
                freePageCount_ = previousFreePageCount;

                const auto pageRollbackStatus = writePage(file, pageId, currentPage);
                if(!pageRollbackStatus.ok()) {
                    return dandb::core::Status::IOError("Cannot rollback free page " + std::to_string(pageId) + " in disk file '" + filePath_.string() + "': " + pageRollbackStatus.message());
                }

                return headerStatus;
            }

            return dandb::core::Status::Ok();

        }

        dandb::core::Status DiskManager::readPage(dandb::core::PageId pageId, std::array<std::byte, dandb::core::PAGE_SIZE_BYTES>& out) {

            if(pageId == DISK_HEADER_PAGE_ID) {
                return dandb::core::Status::InvalidArgument("Cannot read page 0 from disk file '" + filePath_.string() + "': page 0 is reserved for the disk header");
            }

            if(pageId >= pageCount_) {
                return dandb::core::Status::InvalidArgument("Unable to read page " + std::to_string(pageId) + ": page does not exist");
            }

            std::fstream file(filePath_, std::ios::binary | std::ios::in);
            if(!file.is_open()) {
                return dandb::core::Status::IOError("Cannot read page in disk file '" + filePath_.string() + "': unable to open file");
            }

            return readPage(file, pageId, out);

        }

        dandb::core::Status DiskManager::readPage(std::fstream& file, dandb::core::PageId pageId, std::array<std::byte, dandb::core::PAGE_SIZE_BYTES>& out) {

            if(!file.is_open()) {
                return dandb::core::Status::IOError("Cannot read page in disk file '" + filePath_.string() + "': file is not open");
            }

            file.clear();
            file.seekg(pageFileOffset(pageId), std::ios::beg);

            std::array<std::byte, dandb::core::PAGE_SIZE_BYTES> page{};
            file.read(reinterpret_cast<char*>(page.data()), dandb::core::PAGE_SIZE_BYTES);

            if(!file) {
                return dandb::core::Status::IOError("Cannot read page in disk file '" + filePath_.string() + "': stream read failed");
            }

            out = page;

            return dandb::core::Status::Ok();

        }

        dandb::core::Status DiskManager::writePage(dandb::core::PageId pageId, const std::array<std::byte, dandb::core::PAGE_SIZE_BYTES>& data) {

            if(pageId == DISK_HEADER_PAGE_ID) {
                return dandb::core::Status::InvalidArgument("Cannot write page 0 from disk file '" + filePath_.string() + "': page 0 is reserved for the disk header");
            }

            if(pageId >= pageCount_) {
                return dandb::core::Status::InvalidArgument("Unable to write page " + std::to_string(pageId) + ": page does not exist");
            }

            std::fstream file(filePath_, std::ios::binary | std::ios::in | std::ios::out);
            if(!file.is_open()) {
                return dandb::core::Status::IOError("Cannot write page in disk file '" + filePath_.string() + "': unable to open file");
            }

            std::array<std::byte, dandb::core::PAGE_SIZE_BYTES> currentPage{};

            const auto readStatus = readPage(file, pageId, currentPage);
            if(!readStatus.ok()) {
                return dandb::core::Status::IOError("Cannot write page in disk file '" + filePath_.string() + "': unable to read file");
            }

            if(dandb::core::helper::readUint16(currentPage, PAGE_KIND_OFFSET) == FREE_PAGE_KIND) {
                return dandb::core::Status::InvalidArgument("Unable to write page " + std::to_string(pageId) + ": page is free");
            }

            return writePage(file, pageId, data);

        }

        dandb::core::Status DiskManager::writePage(std::fstream& file, dandb::core::PageId pageId, const std::array<std::byte, dandb::core::PAGE_SIZE_BYTES>& data) {

            if(!file.is_open()) {
                return dandb::core::Status::IOError("Cannot write page in disk file '" + filePath_.string() + "': file is not open");
            }

            file.clear();
            file.seekp(pageFileOffset(pageId), std::ios::beg);
            file.write(reinterpret_cast<const char*>(data.data()), dandb::core::PAGE_SIZE_BYTES);
            file.flush();

            if(!file) {
                return dandb::core::Status::IOError("Cannot write page in disk file '" + filePath_.string() + "': stream write failed");
            }

            return dandb::core::Status::Ok();

        }

        dandb::core::PageId DiskManager::pageCount() const {

            return pageCount_;

        }

        dandb::core::Status DiskManager::readHeader(std::fstream& file) {

            if(!file.is_open()) {
                return dandb::core::Status::IOError("Cannot read disk header from '" + filePath_.string() + "': file is not open");
            }

            std::array<std::byte, dandb::core::PAGE_SIZE_BYTES> header{};

            file.clear();
            file.seekg(pageFileOffset(DISK_HEADER_PAGE_ID), std::ios::beg);
            file.read(reinterpret_cast<char*>(header.data()), dandb::core::PAGE_SIZE_BYTES);

            if(!file) {
                return dandb::core::Status::IOError("Cannot read disk header from '" + filePath_.string() + "': stream read failed");
            }

            for(size_t i = 0; i < MAGIC_SIZE; i++) {
                if(header[i] != static_cast<std::byte>(magic_[i])) {
                    return dandb::core::Status::Corruption("Cannot read disk header from '" + filePath_.string() + "': invalid magic bytes");
                }
            }

            pageCount_ = dandb::core::helper::readUint32(header, PAGE_COUNT_OFFSET);
            firstFreePageId_ = dandb::core::helper::readUint32(header, FIRST_FREE_PAGE_ID_OFFSET);
            freePageCount_ = dandb::core::helper::readUint32(header, FREE_PAGE_COUNT_OFFSET);

            return dandb::core::Status::Ok();

        }

        dandb::core::Status DiskManager::writeHeader(std::fstream& file) {

            if(!file.is_open()) {
                return dandb::core::Status::IOError("Cannot write disk header to '" + filePath_.string() + "': file is not open");
            }

            std::array<std::byte, dandb::core::PAGE_SIZE_BYTES> header{};

            for(size_t i = 0; i < MAGIC_SIZE; i++) {
                header[i] = static_cast<std::byte>(magic_[i]);
            }

            dandb::core::helper::writeUint32(header, PAGE_SIZE_OFFSET, dandb::core::PAGE_SIZE_BYTES);
            dandb::core::helper::writeUint32(header, PAGE_COUNT_OFFSET, pageCount_);
            dandb::core::helper::writeUint32(header, FIRST_FREE_PAGE_ID_OFFSET, firstFreePageId_);
            dandb::core::helper::writeUint32(header, FREE_PAGE_COUNT_OFFSET, freePageCount_);

            file.clear();
            file.seekp(pageFileOffset(DISK_HEADER_PAGE_ID), std::ios::beg);
            file.write(reinterpret_cast<char*>(header.data()), dandb::core::PAGE_SIZE_BYTES);
            file.flush();

            if(!file) {
                return dandb::core::Status::IOError("Cannot write disk header to '" + filePath_.string() + "': stream write failed");
            }

            return dandb::core::Status::Ok();

        }
    }
}
