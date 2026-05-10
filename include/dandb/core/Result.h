#pragma once

#include <dandb/core/Status.h>
#include <optional>
#include <stdexcept>

namespace dandb {
	namespace core {

		template<typename T>
		class Result {

		public:
			Result(T value) : status_(Status::Ok()), value_(value) {}
			Result(Status status) : status_(status), value_(std::nullopt) {
				if(status.ok()) {
					throw std::invalid_argument("Status must not be Ok");
				}
			}

			bool ok() const {
				if(value_.has_value()) {
					return true;
				}
				return false;
			}

			const Status& status() const {
				return status_;
			}

			const T& value() const {
				return *value_;
			}

			T& value() {
				return *value_;
			}

		private:
			Status status_;
			std::optional<T> value_;
		};

	}
}