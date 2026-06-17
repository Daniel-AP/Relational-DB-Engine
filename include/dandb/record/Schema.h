#pragma once

#include <dandb/core/Result.h>
#include <dandb/record/LogicalType.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace dandb {
    namespace record {

        struct Column {
            std::string name;
            LogicalType type;
            bool nullable;
            bool primaryKey;
            uint16_t stringCapacity;
        };

        class Schema {
            public:
                static dandb::core::Result<Schema> create(std::vector<Column> columns);

                size_t columnCount() const;
                const Column& column(size_t index) const;
                const std::vector<Column>& columns() const;

            private:
                explicit Schema(std::vector<Column> columns);

                std::vector<Column> columns_;
        };

    }
}
