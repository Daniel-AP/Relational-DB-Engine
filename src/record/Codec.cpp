#include <dandb/record/Codec.h>
#include <dandb/record/Layout.h>

#include <cstring>

namespace dandb {
    namespace record {

        dandb::core::Result<std::vector<std::byte>> Codec::encode(const Schema& schema, const Row& row) {
            
            if(row.valueCount() != schema.columnCount()) {
                return dandb::core::Status::InvalidArgument("Cannot encode the row: row value count and schema column count differ");
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
                        writeUint32(encoded, offset, val.asInt32());
                        break;
                    case LogicalType::Int64:
                        writeUint64(encoded, offset, val.asInt64());
                        break;
                    case LogicalType::Double:
                        writeDouble(encoded, offset, val.asDouble());
                        break;
                    case LogicalType::String:
                        std::string stringValue = val.asString();
                        std::memcpy(encoded.data()+offset, stringValue.data(), stringValue.size());
                        break;
                }

                offset += dandb::record::layout::valueSize(col);
                
            }

            return encoded;

        }

        dandb::core::Result<Row> Codec::decode(const Schema& schema, const std::vector<std::byte>& row) {

        }

        void Codec::writeUint32(std::span<std::byte> buffer, size_t offset, uint32_t value) {

            for(size_t i = 0; i < 4; i++) {
                buffer[offset+i] = static_cast<std::byte>((value>>(8*i))&0xFFu);
            }

        }

        void Codec::writeUint64(std::span<std::byte> buffer, size_t offset, uint64_t value) {

            for(size_t i = 0; i < 8; i++) {
                buffer[offset+i] = static_cast<std::byte>((value>>(8*i))&0xFFu);
            }

        }

        void Codec::writeDouble(std::span<std::byte> buffer, size_t offset, double value) {

            std::array<std::byte, 8> doubleBytes{};
            std::memcpy(doubleBytes.data(), &value, doubleBytes.size());

            for(size_t i = 0; i < doubleBytes.size(); i++) {
                buffer[offset+i] = doubleBytes[i];
            }

        }

    }
}