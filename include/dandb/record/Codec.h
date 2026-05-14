#pragma once

#include <dandb/core/Result.h>
#include <dandb/record/Schema.h>
#include <dandb/record/Row.h>

#include <vector>
#include <span>

namespace dandb {
    namespace record {

        class Codec {

            public:
                static dandb::core::Result<std::vector<std::byte>> encode(const Schema& schema, const Row& row);
                static dandb::core::Result<Row> decode(const Schema& schema, const std::vector<std::byte>& row);

            private:
                static void writeUint32(std::span<std::byte> buffer, size_t offset, uint32_t value);
                static void writeUint64(std::span<std::byte> buffer, size_t offset, uint64_t value);
                static void writeDouble(std::span<std::byte> buffer, size_t offset, double value);

        };

    }
}