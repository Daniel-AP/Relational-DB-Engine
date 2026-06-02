#pragma once

#include <string>
#include <utility>

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

                static Status InvalidArgument(std::string message) {

                    return Status(StatusCode::InvalidArgument, std::move(message));

                }

                static Status NotFound(std::string message) {

                    return Status(StatusCode::NotFound, std::move(message));

                }

                static Status AlreadyExists(std::string message) {

                    return Status(StatusCode::AlreadyExists, std::move(message));

                }

                static Status IOError(std::string message) {

                    return Status(StatusCode::IOError, std::move(message));

                }

                static Status Corruption(std::string message) {

                    return Status(StatusCode::Corruption, std::move(message));

                }

                static Status Internal(std::string message) {

                    return Status(StatusCode::Internal, std::move(message));

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
                Status(StatusCode code, std::string message)
                    : code_(code), message_(std::move(message)) {

                }

                StatusCode code_;
                std::string message_;
            };

    }
}
