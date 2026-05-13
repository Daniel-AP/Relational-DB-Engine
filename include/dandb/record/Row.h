#pragma once

#include <dandb/record/Value.h>

#include <cstddef>
#include <utility>
#include <vector>

namespace dandb {
    namespace record {

        class Row {
            public:
                explicit Row(std::vector<Value> values)
                    : values_(std::move(values)) {

                }

                size_t valueCount() const {
                    return values_.size();
                }

                const Value& value(size_t index) const {
                    return values_[index];
                }

                const std::vector<Value>& values() const {
                    return values_;
                }

            private:
                std::vector<Value> values_;
        };

    }
}
