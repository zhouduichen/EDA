#pragma once

#include <string>
#include <utility>

namespace cse {

enum class ErrorCode {
    kOk = 0,
    kInvalidArgument,
    kNotFound,
    kAlreadyExists,
    kFileOpenFailed,
    kFileReadFailed,
    kFileWriteFailed,
    kInvalidFileFormat,
    kUnsupportedFileVersion,
    kInvalidComponent,
    kInvalidConnection,
    kSimulationFailed,
    kInternalError
};

template <typename T>
struct Result {
    bool ok{false};
    ErrorCode code{ErrorCode::kInternalError};
    std::string message;
    T value{};

    static Result Success(T result_value) {
        Result result;
        result.ok = true;
        result.code = ErrorCode::kOk;
        result.value = std::move(result_value);
        return result;
    }

    static Result Failure(ErrorCode result_code, std::string result_message) {
        Result result;
        result.code = result_code;
        result.message = std::move(result_message);
        return result;
    }
};

template <>
struct Result<void> {
    bool ok{false};
    ErrorCode code{ErrorCode::kInternalError};
    std::string message;

    static Result Success() {
        Result result;
        result.ok = true;
        result.code = ErrorCode::kOk;
        return result;
    }

    static Result Failure(ErrorCode result_code, std::string result_message) {
        Result result;
        result.code = result_code;
        result.message = std::move(result_message);
        return result;
    }
};

}  // namespace cse
