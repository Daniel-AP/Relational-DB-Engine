#pragma once

#include <dandb/core/Result.h>
#include <dandb/core/Status.h>

#include <cstddef>
#include <cstdint>
#include <string>

namespace dandb {
    namespace record {

        enum class LogicalType {
            Boolean,
            Byte,
            Int32,
            Int64,
            Double,
            String
        };

        inline uint8_t logicalTypeCode(LogicalType type) {

            switch(type) {
                case LogicalType::Boolean:
                    return 0x01;
                case LogicalType::Byte:
                    return 0x02;
                case LogicalType::Int32:
                    return 0x03;
                case LogicalType::Int64:
                    return 0x04;
                case LogicalType::Double:
                    return 0x05;
                case LogicalType::String:
                    return 0x06;
            }

            return 0;

        }

        inline dandb::core::Result<LogicalType> logicalTypeFromCode(uint8_t code) {

            switch(code) {
                case 0x01:
                    return LogicalType::Boolean;
                case 0x02:
                    return LogicalType::Byte;
                case 0x03:
                    return LogicalType::Int32;
                case 0x04:
                    return LogicalType::Int64;
                case 0x05:
                    return LogicalType::Double;
                case 0x06:
                    return LogicalType::String;
            }

            return dandb::core::Status::Corruption(
                "Cannot decode logical type code: invalid code "+std::to_string(static_cast<uint32_t>(code))
            );

        }

        inline dandb::core::Result<LogicalType> logicalTypeFromCode(std::byte code) {

            return logicalTypeFromCode(std::to_integer<uint8_t>(code));

        }

    }
}
