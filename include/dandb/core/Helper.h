#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>

namespace dandb {
    namespace core {
        namespace helper {

            inline void writeUint16(std::span<std::byte> buffer, size_t offset, uint16_t value) {

                for(size_t i = 0; i < 2; i++) {
                    buffer[offset+i] = static_cast<std::byte>((value>>(8*i))&0xFFu);
                }

            }

            inline uint16_t readUint16(std::span<const std::byte> buffer, size_t offset) {

                uint16_t res = 0;
                for(size_t i = 0; i < 2; i++) {
                    res |= std::to_integer<uint16_t>(buffer[offset+i]) << (8*i);
                }

                return res;

            }

            inline void writeUint32(std::span<std::byte> buffer, size_t offset, uint32_t value) {

                for(size_t i = 0; i < 4; i++) {
                    buffer[offset+i] = static_cast<std::byte>((value>>(8*i))&0xFFu);
                }

            }

            inline uint32_t readUint32(std::span<const std::byte> buffer, size_t offset) {

                uint32_t res = 0;
                for(size_t i = 0; i < 4; i++) {
                    res |= std::to_integer<uint32_t>(buffer[offset+i]) << (8*i);
                }

                return res;

            }

            inline void writeUint64(std::span<std::byte> buffer, size_t offset, uint64_t value) {

                for(size_t i = 0; i < 8; i++) {
                    buffer[offset+i] = static_cast<std::byte>((value>>(8*i))&0xFFu);
                }

            }

            inline uint64_t readUint64(std::span<const std::byte> buffer, size_t offset) {

                uint64_t res = 0;
                for(size_t i = 0; i < 8; i++) {
                    res |= std::to_integer<uint64_t>(buffer[offset+i]) << (8*i);
                }

                return res;

            }

            inline void writeDouble(std::span<std::byte> buffer, size_t offset, double value) {

                std::array<std::byte, 8> doubleBytes{};
                std::memcpy(doubleBytes.data(), &value, doubleBytes.size());

                for(size_t i = 0; i < doubleBytes.size(); i++) {
                    buffer[offset+i] = doubleBytes[i];
                }

            }

            inline double readDouble(std::span<const std::byte> buffer, size_t offset) {

                double value;
                std::memcpy(&value, buffer.data()+offset, sizeof(double));

                return value;

            }

        }
    }
}
