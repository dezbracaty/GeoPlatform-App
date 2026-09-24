#pragma once
#include "SnapTypes.hpp"
#include <TempDBScope.hpp>
#include <QObject>
#include <QPointer>
#include <QCursor>
#include <memory>

class SnapPreviewDB;
class QWindow;

namespace GPlatform::Interaction {

// Presents a caller-supplied result. Input policy, scheduling and queries belong
// to the operation handler; this class never calls SnapService.
class SnapPreviewHandler final : public QObject {
    Q_OBJECT
public:
    explicit SnapPreviewHandler(QObject* parent = nullptr) : QObject(parent) {}
    ~SnapPreviewHandler() override;
    void setShowCursor(bool show);
    // A miss can still display the free operation cursor. The caller supplies
    // the native window whose cursor may be replaced while this preview is shown.
    void show(DBInstanceID viewId, const SnapPointerQuery& query,
              const std::vector<SnapTarget>& targets, const std::optional<SnapResult>& result,
              QWindow* cursorWindow = nullptr);
    void clear();

private:
    void updateCursor(QWindow* window);
    bool m_showCursor{false};
    QPointer<QWindow> m_cursorWindow;
    std::optional<QCursor> m_savedCursor, m_cursorToken;
    TempDBScope m_scope;
    std::shared_ptr<SnapPreviewDB> m_display;
};

} // namespace GPlatform::Interaction
