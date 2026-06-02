#include <dandb/core/Helper.h>
#include <dandb/record/Codec.h>
#include <dandb/record/Layout.h>

#include <cstring>
#include <utility>

namespace dandb {
    namespace record {

        dandb::core::Result<std::vector<std::byte>> Codec::encode(const Schema& schema, const Row& row) {
            
            if(row.valueCount() != schema.columnCount()) {
                return dandb::core::Status::InvalidArgument("Cannot encode row: row value count and schema column count differ");
            }

            size_t count = row.valueCount();

            for(size_t i = 0; i < count; i++) {

                const Value& val = row.value(i);
                const Column& col = schema.column(i);

                if(val.type() != col.type) {
                    return dandb::core::Status::InvalidArgument("Cannot encode row: value type does not match schema column type at index "+std::to_string(i));
                }

                if(val.isNull() && !col.nullable) {
                    return dandb::core::Status::InvalidArgument("Cannot encode row: NULL is not supported at column index "+std::to_string(i));
                }

                if(val.type() == dandb::record::LogicalType::String && !val.isNull() && val.asString().size() > col.stringCapacity) {
                    return dandb::core::Status::InvalidArgument("Cannot encode row: string value surpasses the schema string capacity at index "+std::to_string(i));
                }

            }

            const size_t encodedSize = dandb::record::layout::encodedSize(schema);
            std::vector<std::byte> encoded(encodedSize);

            size_t offset = 0;

            encoded[offset] = std::byte{static_cast<uint8_t>(count)}; offset++;

            for(size_t i = 0; i < count; i++) {
                if(!row.value(i).isNull()) continue;
                size_t colByte = i/8;
                size_t colBit = i%8;
                encoded[offset+colByte] |= std::byte{static_cast<uint8_t>(1U<<colBit)};
            }

            offset += dandb::record::layout::nullBitmapSize(count);
            offset = dandb::record::layout::alignTo(offset, 8);

            for(size_t i = 0; i < count; i++) {
                
                const Value& val = row.value(i);
                const Column& col = schema.column(i);

                size_t alignment = dandb::record::layout::valueAlignment(col.type);
                offset = dandb::record::layout::alignTo(offset, alignment);

                if(val.isNull()) {
                    offset += dandb::record::layout::valueSize(col);
                    continue;
                }

                switch(val.type()) {
                    case LogicalType::Boolean:
                        encoded[offset] = std::byte{val.asBoolean()};
                        break;
                    case LogicalType::Byte:
                        encoded[offset] = std::byte{static_cast<uint8_t>(val.asByte())};
                        break;
                    case LogicalType::Int32:
                        dandb::core::helper::writeUint32(encoded, offset, val.asInt32());
                        break;
                    case LogicalType::Int64:
                        dandb::core::helper::writeUint64(encoded, offset, val.asInt64());
                        break;
                    case LogicalType::Double:
                        dandb::core::helper::writeDouble(encoded, offset, val.asDouble());
                        break;
                    case LogicalType::String:
                        const std::string& stringValue = val.asString();
                        std::memcpy(encoded.data()+offset, stringValue.data(), stringValue.size());
                        break;
                }

                offset += dandb::record::layout::valueSize(col);
                
            }

            return encoded;

        }

        dandb::core::Result<Row> Codec::decode(const Schema& schema, const std::vector<std::byte>& row) {

            if(dandb::record::layout::encodedSize(schema) != row.size()) {
                return dandb::core::Status::InvalidArgument("Cannot decode row: schema encoding size and row size differ");
            }

            if(std::to_integer<size_t>(row[0]) != schema.columnCount()) {
                return dandb::core::Status::InvalidArgument("Cannot decode row: schema column count and row value count differ");
            }

            size_t count = schema.columnCount();
            size_t nullBitmapSize = dandb::record::layout::nullBitmapSize(count);

            std::vector<bool> nullBitmap(count);

            size_t offset = 1;

            for(size_t i = 0; i < count; i++) {
                size_t colByte = i/8;
                size_t colBit = i%8;
                nullBitmap[i] = static_cast<uint8_t>((row[offset+colByte]>>colBit)&std::byte{1}) != 0;
            }

            uint8_t lastNullBitmapByte = std::to_integer<uint8_t>(row[offset+nullBitmapSize-1]);
            uint8_t unusedBitsMask = (0xFFu<<(count%8));

            if((lastNullBitmapByte&unusedBitsMask) != 0) {
                return dandb::core::Status::InvalidArgument("Cannot decode row: null bitmap has non-zero unused bits");
            }

            offset += nullBitmapSize;
            offset = dandb::record::layout::alignTo(offset, 8);

            std::vector<Value> values;
            values.reserve(count);

            for(size_t i = 0; i < count; i++) {

                const Column& col = schema.column(i);

                size_t alignment = dandb::record::layout::valueAlignment(col.type);
                offset = dandb::record::layout::alignTo(offset, alignment);

                if(nullBitmap[i]) {

                    if(!col.nullable) {
                        return dandb::core::Status::InvalidArgument("Cannot decode row: non-NULL column has NULL value at index "+std::to_string(i));
                    }

                    for(size_t j = 0; j < dandb::record::layout::valueSize(col); j++) {
                        if(row[offset+j] != std::byte{0}) {
                            return dandb::core::Status::InvalidArgument("Cannot decode row: NULL value has non-zero payload bytes at column index "+std::to_string(i));
                        }
                    }

                    offset += dandb::record::layout::valueSize(col);
                    values.push_back(Value::null(col.type));

                    continue;
                }

                switch(col.type) {
                    case LogicalType::Boolean: {
                        values.push_back(Value::boolean(static_cast<uint8_t>(row[offset]) != 0));
                        break;
                    }
                    case LogicalType::Byte: {
                        values.push_back(Value::byte(static_cast<int8_t>(row[offset])));
                        break;
                    }
                    case LogicalType::Int32: {
                        int32_t value = static_cast<int32_t>(dandb::core::helper::readUint32(row, offset));
                        values.push_back(Value::int32(value));
                        break;
                    }
                    case LogicalType::Int64: {
                        int64_t value = static_cast<int64_t>(dandb::core::helper::readUint64(row, offset));
                        values.push_back(Value::int64(value));
                        break;
                    }
                    case LogicalType::Double: {
                        double value = dandb::core::helper::readDouble(row, offset);
                        values.push_back(Value::doubleValue(value));
                        break;
                    }
                    case LogicalType::String: {
                        std::string stringValue;
                        stringValue.reserve(col.stringCapacity);
                        for(size_t j = 0; j < col.stringCapacity; j++) {
                            char ch = static_cast<char>(row[offset+j]);
                            if(ch == '\0') break;
                            stringValue += ch;
                        }
                        values.push_back(Value::string(std::move(stringValue)));
                        break;
                    }
                }

                offset += dandb::record::layout::valueSize(col);

            }

            return Row(std::move(values));

        }

    }
}
