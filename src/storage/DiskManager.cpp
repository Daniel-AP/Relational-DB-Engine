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

            dandb::core::PageId allocatedPageId = pageCount_;
            pageCount_++;

            file.seekp(allocatedPageId*dandb::core::PAGE_SIZE_BYTES, std::ios::beg);

            std::array<std::byte, dandb::core::PAGE_SIZE_BYTES> newPage{};
            file.write(reinterpret_cast<char*>(newPage.data()), dandb::core::PAGE_SIZE_BYTES);

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

            file.close();

            return allocatedPageId;

        }

        dandb::core::PageId DiskManager::pageCount() const {

            return pageCount_;

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

            pageCount_ = readUint32(header, 8);
            firstFreePageId_ = readUint32(header, 12);
            freePageCount_ = readUint32(header, 16);

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
