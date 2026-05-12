#include <dandb/storage/DiskManager.h>

#include <fstream>
#include <stdexcept>

namespace dandb {
    namespace storage {

        DiskManager::DiskManager(std::filesystem::path filePath, std::array<char, 4> magic)
            : filePath_(filePath), magic_(magic) {

            if(!std::filesystem::exists(filePath_)) {

                if(!filePath_.parent_path().empty()) {
                    try {
                        std::filesystem::create_directories(filePath_.parent_path());
                    } catch(const std::filesystem::filesystem_error& error) {
                        throw std::runtime_error("Cannot create disk file '" + filePath_.string() + "': " + error.what());
                    }
                }

                pageCount_ = 1;
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

                firstFreePageId_ = readUint32(currentPage, 8);

                std::array<std::byte, dandb::core::PAGE_SIZE_BYTES> newPage{};

                file.clear();
                file.seekp(allocatedPageId*dandb::core::PAGE_SIZE_BYTES, std::ios::beg);
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

            } else {

                allocatedPageId = pageCount_;
                pageCount_++;

                std::array<std::byte, dandb::core::PAGE_SIZE_BYTES> newPage{};

                file.clear();
                file.seekp(allocatedPageId*dandb::core::PAGE_SIZE_BYTES, std::ios::beg);
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

            if(pageId == 0) {
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

            if(currentPage[4] == static_cast<std::byte>(0xFFu) && currentPage[5] == static_cast<std::byte>(0xFFu)) {
                return dandb::core::Status::InvalidArgument("Unable to free page " + std::to_string(pageId) + ": page is already free");
            }

            std::array<std::byte, dandb::core::PAGE_SIZE_BYTES> freedPage{};

            writeUint32(freedPage, 0, pageId);
            freedPage[4] = freedPage[5] = static_cast<std::byte>(0xFFu);
            writeUint32(freedPage, 8, firstFreePageId_);

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

            if(pageId == 0) {
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
            file.seekg(pageId*dandb::core::PAGE_SIZE_BYTES, std::ios::beg);

            std::array<std::byte, dandb::core::PAGE_SIZE_BYTES> page{};
            file.read(reinterpret_cast<char*>(page.data()), dandb::core::PAGE_SIZE_BYTES);

            if(!file) {
                return dandb::core::Status::IOError("Cannot read page in disk file '" + filePath_.string() + "': stream read failed");
            }

            out = page;

            return dandb::core::Status::Ok();

        }

        dandb::core::Status DiskManager::writePage(dandb::core::PageId pageId, const std::array<std::byte, dandb::core::PAGE_SIZE_BYTES>& data) {

            if(pageId == 0) {
                return dandb::core::Status::InvalidArgument("Cannot write page 0 from disk file '" + filePath_.string() + "': page 0 is reserved for the disk header");
            }

            if(pageId >= pageCount_) {
                return dandb::core::Status::InvalidArgument("Unable to write page " + std::to_string(pageId) + ": page does not exist");
            }

            std::fstream file(filePath_, std::ios::binary | std::ios::in | std::ios::out);
            if(!file.is_open()) {
                return dandb::core::Status::IOError("Cannot write page in disk file '" + filePath_.string() + "': unable to open file");
            }

            return writePage(file, pageId, data);

        }

        dandb::core::Status DiskManager::writePage(std::fstream& file, dandb::core::PageId pageId, const std::array<std::byte, dandb::core::PAGE_SIZE_BYTES>& data) {

            if(!file.is_open()) {
                return dandb::core::Status::IOError("Cannot write page in disk file '" + filePath_.string() + "': file is not open");
            }

            file.clear();
            file.seekp(pageId*dandb::core::PAGE_SIZE_BYTES, std::ios::beg);
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
            file.seekg(0, std::ios::beg);
            file.read(reinterpret_cast<char*>(header.data()), dandb::core::PAGE_SIZE_BYTES);

            if(!file) {
                return dandb::core::Status::IOError("Cannot read disk header from '" + filePath_.string() + "': stream read failed");
            }

            for(size_t i = 0; i < 4; i++) {
                if(header[i] != static_cast<std::byte>(magic_[i])) {
                    return dandb::core::Status::Corruption("Cannot read disk header from '" + filePath_.string() + "': invalid magic bytes");
                }
            }

            pageCount_ = readUint32(header, 8);
            firstFreePageId_ = readUint32(header, 12);
            freePageCount_ = readUint32(header, 16);

            return dandb::core::Status::Ok();

        }

        dandb::core::Status DiskManager::writeHeader(std::fstream& file) {

            if(!file.is_open()) {
                return dandb::core::Status::IOError("Cannot write disk header to '" + filePath_.string() + "': file is not open");
            }

            std::array<std::byte, dandb::core::PAGE_SIZE_BYTES> header{};

            for(size_t i = 0; i < 4; i++) {
                header[i] = static_cast<std::byte>(magic_[i]);
            }

            writeUint32(header, 4, dandb::core::PAGE_SIZE_BYTES);
            writeUint32(header, 8, pageCount_);
            writeUint32(header, 12, firstFreePageId_);
            writeUint32(header, 16, freePageCount_);

            file.clear();
            file.seekp(0, std::ios::beg);
            file.write(reinterpret_cast<char*>(header.data()), dandb::core::PAGE_SIZE_BYTES);
            file.flush();

            if(!file) {
                return dandb::core::Status::IOError("Cannot write disk header to '" + filePath_.string() + "': stream write failed");
            }

            return dandb::core::Status::Ok();

        }

        void DiskManager::writeUint32(std::span<std::byte> buffer, size_t offset, uint32_t value) {

            for(size_t i = 0; i < 4; i++) {
                buffer[offset+i] = static_cast<std::byte>((value>>(8*i))&0xFFu);
            }

        }

        uint32_t DiskManager::readUint32(std::span<const std::byte> buffer, size_t offset) {

            uint32_t res = 0;
            for(size_t i = 0; i < 4; i++) {
                res |= std::to_integer<uint32_t>(buffer[offset+i]) << (8*i);
            }

            return res;

        }

    }
}
