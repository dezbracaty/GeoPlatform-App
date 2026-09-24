#pragma once

#include <QString>
#include <QVariantMap>
#include <memory>
#include "ActionError.hpp"

// Action执行上下文
class ActionContext {
public:
    using SPtr = std::shared_ptr<ActionContext>;

    ActionContext() = default;
    ~ActionContext() = default;

    // ActionCode访问
    void setActionCode(const QString& code) {
        m_actionCode = code;
    }
    QString getActionCode() const {
        return m_actionCode;
    }

    // 参数访问
    void setParams(const QVariantMap& params) {
        m_params = params;
    }
    QVariantMap getParams() const {
        return m_params;
    }

    // 单个参数访问
    void setParam(const QString& key, const QVariant& value) {
        m_params[key] = value;
    }

    QVariant getParam(const QString& key, const QVariant& defaultValue = QVariant()) const {
        return m_params.value(key, defaultValue);
    }

    // 调用跟踪信息与业务参数分离，供异步 Handler 返回真实完成结果。
    void setInvocationId(const QString& invocationId) {
        m_invocationId = invocationId;
    }
    QString getInvocationId() const {
        return m_invocationId;
    }

    void setInvocationMetadata(const QVariantMap& metadata) {
        m_invocationMetadata = metadata;
    }
    QVariantMap getInvocationMetadata() const {
        return m_invocationMetadata;
    }

    // 结果存储（Handler执行结果）
    void setResult(const QVariant& result) {
        m_result = result;
    }
    QVariant getResult() const {
        return m_result;
    }

    // 错误信息
    void setError(ActionErrorCode code, const QString& message = QString()) {
        m_errorCode = code;
        m_error = actionErrorMessage(code, message);
        m_hasError = true;
    }

    QString getError() const {
        return m_error;
    }
    ActionErrorCode getErrorCode() const {
        return m_errorCode;
    }
    bool hasError() const {
        return m_hasError;
    }

private:
    QString m_actionCode;
    QVariantMap m_params;
    QString m_invocationId;
    QVariantMap m_invocationMetadata;
    QVariant m_result;
    QString m_error;
    ActionErrorCode m_errorCode = ActionErrorCode::None;
    bool m_hasError = false;
};
