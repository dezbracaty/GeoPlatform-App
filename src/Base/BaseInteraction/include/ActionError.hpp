#pragma once

#include <QString>

enum class ActionErrorCode {
    None = 0,
    TargetRequired,
    TargetNotFound,
    InvalidParams,
    ContractValidation,
    SystemUnavailable,
    Internal
};

struct ActionErrorMeta {
    const char* code = "";
    const char* category = "";
    const char* defaultMessage = "";
};

inline ActionErrorMeta actionErrorMeta(ActionErrorCode code) {
    switch (code) {
        case ActionErrorCode::TargetRequired:
            return {"ERR_TARGET_REQUIRED", "precondition", "Target is required"};
        case ActionErrorCode::TargetNotFound:
            return {"ERR_TARGET_NOT_FOUND", "precondition", "Target not found"};
        case ActionErrorCode::InvalidParams:
            return {"ERR_INVALID_PARAMS", "validation", "Invalid parameters"};
        case ActionErrorCode::ContractValidation:
            return {"ERR_CONTRACT_VALIDATION", "contract", "Contract validation failed"};
        case ActionErrorCode::SystemUnavailable:
            return {"ERR_SYSTEM_UNAVAILABLE", "system", "System unavailable"};
        case ActionErrorCode::Internal:
            return {"ERR_INTERNAL", "system", "Internal error"};
        case ActionErrorCode::None:
        default:
            return {"", "", ""};
    }
}

inline QString actionErrorCodeToString(ActionErrorCode code) {
    return QString::fromLatin1(actionErrorMeta(code).code);
}

inline QString actionErrorMessage(ActionErrorCode code, const QString& customMessage) {
    if (!customMessage.trimmed().isEmpty()) {
        return customMessage;
    }
    return QString::fromLatin1(actionErrorMeta(code).defaultMessage);
}
