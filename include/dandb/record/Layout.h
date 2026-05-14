#pragma once

#include <dandb/record/Schema.h>
#include <dandb/record/Value.h>

#include <cstddef>

namespace dandb {
    namespace record {
        namespace layout {

            size_t alignTo(size_t offset, size_t alignment);
            size_t nullBitmapSize(size_t columnCount);
            size_t valueSize(const Column& column);
            size_t valueAlignment(LogicalType type);
            size_t encodedSize(const Schema& schema);

        }
    }
}
