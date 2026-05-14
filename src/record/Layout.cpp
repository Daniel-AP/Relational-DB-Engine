#include <dandb/record/Layout.h>

namespace dandb {
    namespace record {
        namespace layout {

            size_t alignTo(size_t offset, size_t alignment) {

                const size_t remainder = offset%alignment;
                if(remainder == 0) {
                    return offset;
                }

                return offset+(alignment-remainder);

            }

            size_t nullBitmapSize(size_t columnCount) {

                return (columnCount+7)/8;

            }

            size_t valueSize(const Column& column) {

                switch(column.type) {
                    case LogicalType::Boolean:
                        return 1;
                    case LogicalType::Byte:
                        return 1;
                    case LogicalType::Int32:
                        return 4;
                    case LogicalType::Int64:
                        return 8;
                    case LogicalType::Double:
                        return 8;
                    case LogicalType::String:
                        return column.stringCapacity;
                }

                return 0;

            }

            size_t valueAlignment(LogicalType type) {

                switch(type) {
                    case LogicalType::Boolean:
                        return 1;
                    case LogicalType::Byte:
                        return 1;
                    case LogicalType::Int32:
                        return 4;
                    case LogicalType::Int64:
                        return 8;
                    case LogicalType::Double:
                        return 8;
                    case LogicalType::String:
                        return 1;
                }

                return 1;

            }

            size_t encodedSize(const Schema& schema) {

                size_t offset = 0;

                offset += 1;
                offset += nullBitmapSize(schema.columnCount());
                offset = alignTo(offset, 8);

                for(const auto& column: schema.columns()) {
                    offset = alignTo(offset, valueAlignment(column.type));
                    offset += valueSize(column);
                }

                return alignTo(offset, 8);

            }

        }
    }
}
