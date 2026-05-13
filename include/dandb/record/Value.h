#pragma once

#include <cstdint>
#include <string>
#include <variant>

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

        class Value {
            public:
                static Value boolean(bool value) {
                    return Value(LogicalType::Boolean, false, value);
                }

                static Value byte(int8_t value) {
                    return Value(LogicalType::Byte, false, value);
                }

                static Value int32(int32_t value) {
                    return Value(LogicalType::Int32, false, value);
                }

                static Value int64(int64_t value) {
                    return Value(LogicalType::Int64, false, value);
                }

                static Value doubleValue(double value) {
                    return Value(LogicalType::Double, false, value);
                }

                static Value string(std::string value) {
                    return Value(LogicalType::String, false, value);
                }

                static Value null(LogicalType type) {
                    return Value(type, true, std::monostate{});
                }

                LogicalType type() const {
                    return type_;
                }

                bool isNull() const {
                    return isNull_;
                }

                bool asBoolean() const {
                    return std::get<bool>(payload_);
                }

                int8_t asByte() const {
                    return std::get<int8_t>(payload_);
                }

                int32_t asInt32() const {
                    return std::get<int32_t>(payload_);
                }

                int64_t asInt64() const {
                    return std::get<int64_t>(payload_);
                }

                double asDouble() const {
                    return std::get<double>(payload_);
                }

                const std::string& asString() const {
                    return std::get<std::string>(payload_);
                }

            private:
                using Payload = std::variant<
                    std::monostate,
                    bool,
                    int8_t,
                    int32_t,
                    int64_t,
                    double,
                    std::string
                >;

                Value(LogicalType type, bool isNull, Payload payload)
                    : type_(type),
                      isNull_(isNull),
                      payload_(payload) {

                }

                LogicalType type_;
                bool isNull_;
                Payload payload_;
        };

    }
}
