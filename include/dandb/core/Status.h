#pragma once

#include <string>

namespace dandb {
	namespace core {

		enum class StatusCode {
			Ok,
			InvalidArgument,
			NotFound,
			AlreadyExists,
			IOError,
			Corruption,
			Internal
		};

		class Status {

		public:
			static Status Ok() {
				return Status(StatusCode::Ok, "");
			}
			static Status InvalidArgument(const std::string& message) {
				return Status(StatusCode::InvalidArgument, message);
			}
			static Status NotFound(const std::string& message) {
				return Status(StatusCode::NotFound, message);
			}
			static Status AlreadyExists(const std::string& message) {
				return Status(StatusCode::AlreadyExists, message);
			}
			static Status IOError(const std::string& message) {
				return Status(StatusCode::IOError, message);
			}
			static Status Corruption(const std::string& message) {
				return Status(StatusCode::Corruption, message);
			}
			static Status Internal(const std::string& message) {
				return Status(StatusCode::Internal, message);
			}
			bool ok() const {
				return code_ == StatusCode::Ok;
			}
			StatusCode code() const {
				return code_;
			}
			const std::string& message() const {
				return message_;
			}

		private:
			Status(StatusCode code, const std::string& message)
				: code_(code), message_(message) {}

			StatusCode code_;
			std::string message_;
		};

	}
}