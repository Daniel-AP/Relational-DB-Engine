#pragma once

#include <dandb/core/Result.h>
#include <dandb/record/Schema.h>
#include <dandb/record/Row.h>

#include <vector>

namespace dandb {
    namespace record {

        class Codec {

            public:
                static dandb::core::Result<std::vector<std::byte>> encode(const Schema& schema, const Row& row);
                static dandb::core::Result<Row> decode(const Schema& schema, const std::vector<std::byte>& row);

        };

    }
}
