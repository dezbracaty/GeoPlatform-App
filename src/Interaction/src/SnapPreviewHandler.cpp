#include "SnapPreviewHandler.hpp"
#include <QWindow>
#include <QPixmap>
#include <SnapPreviewDB.hpp>
#include <TransactionManager.hpp>
#include <ImmediateNotifyGuard.hpp>
#include <cmath>

namespace GPlatform::Interaction {
    namespace {
        // Only one preview may replace the native pointer at a time.
        QPointer<SnapPreviewHandler> cursorOwner;
    }
    SnapPreviewHandler::~SnapPreviewHandler() { clear(); }
    void SnapPreviewHandler::setShowCursor(bool show) {
        if (m_showCursor == show) return;
        clear();
        m_showCursor = show;
    }
    void SnapPreviewHandler::clear() {
        updateCursor(nullptr);
        if (m_display) {
            TransientUpdateGuard guard;
            ImmediateNotifyGuard immediate;
            m_display->setVisible(false);
        }
    }
    void SnapPreviewHandler::updateCursor(QWindow* window) {
        if (!window) {
            if (m_cursorWindow && m_cursorToken && m_savedCursor &&
                m_cursorWindow->cursor().shape() == Qt::BitmapCursor &&
                m_cursorWindow->cursor().pixmap().cacheKey() == m_cursorToken->pixmap().cacheKey())
                m_cursorWindow->setCursor(*m_savedCursor);
            m_cursorWindow.clear();
            m_savedCursor.reset();
            m_cursorToken.reset();
            if (cursorOwner == this) cursorOwner.clear();
            return;
        }
        if (cursorOwner && cursorOwner != this) cursorOwner->clear();
        if (m_cursorWindow == window && m_cursorToken && window->cursor().shape() == Qt::BitmapCursor &&
            window->cursor().pixmap().cacheKey() == m_cursorToken->pixmap().cacheKey()) return;
        updateCursor(nullptr);
        m_cursorWindow = window;
        m_savedCursor = window->cursor();
        QPixmap blank(1, 1);
        blank.fill(Qt::transparent);
        m_cursorToken = QCursor(blank, 0, 0);
        window->setCursor(*m_cursorToken);
        cursorOwner = this;
    }
    void SnapPreviewHandler::show(DBInstanceID viewId, const SnapPointerQuery& query, const std::vector<SnapTarget>& targets,
                                  const std::optional<SnapResult>& result, QWindow* cursorWindow) {
        if (!viewId.isValid() || !query.projection.isValid() || (!result && !m_showCursor)) {
            clear();
            return;
        }
        TransientUpdateGuard guard;
        ImmediateNotifyGuard immediate;
        if (!m_display) m_display = m_scope.create<SnapPreviewDB>();
        std::vector<SnapPreviewDB::Line> lines;
        const auto add = [&](Vector3 a, Vector3 b) { lines.push_back({a,b}); };
        Vector3 center{float(query.position.x()), float(query.position.y()), .5f};
        if (result) {
            const auto& hit = *result;
            const auto& feature = hit.geometry->features[hit.featureIndex];
            if (feature.groupCount) {
                for (const auto& target : targets) {
                    if (!target.db) continue;
                    const auto instance = target.instanceId.isValid() ? target.instanceId : target.db->getDBInstanceID();
                    if (target.db->getDBInstanceID() != hit.sourceId || instance != hit.instanceId)
                        continue;
                    const auto world = [&](Vector3 p) {
                        const Eigen::Vector4f v = target.localToWorld * Eigen::Vector4f(p.x, p.y, p.z, 1);
                        return Vector3{v.x(), v.y(), v.z()};
                    };
                    for (std::size_t i = feature.groupBegin; i < std::size_t(feature.groupBegin) + feature.groupCount; ++i) {
                        const auto& member = hit.geometry->features[i];
                        if (member.kind == SnapFeatureKind::Segment && (!target.eligible || target.eligible(i, member)))
                            add(world(member.first), world(member.second));
                    }
                    break;
                }
            } else if (hit.kind != SnapFeatureKind::Point) {
                add(hit.displayStart, hit.displayEnd);
            }
            center = query.projection.screenFromWorld(hit.worldPosition);
        }
        if (m_showCursor) {
            // Display the operation cursor at the snapped position; raw input stays
            // unchanged so moving away can always release the snap.
            const auto stroke = [&](double ax, double ay, double bx, double by) {
                add(query.projection.worldFromScreen({center.x + ax, center.y + ay}, center.z),
                    query.projection.worldFromScreen({center.x + bx, center.y + by}, center.z));
            };
            stroke(-9, 0, -3, 0); stroke(3, 0, 9, 0);
            stroke(0, -9, 0, -3); stroke(0, 3, 0, 9);
        }
        // Fixed logical-pixel reticle, projected by the same view as the query.
        for (int i = 0; result && i < 24; ++i) {
            const double a = i * 6.28318530718 / 24, b = (i + 1) * 6.28318530718 / 24;
            add(query.projection.worldFromScreen({center.x + 5 * std::cos(a), center.y + 5 * std::sin(a)}, center.z),
                query.projection.worldFromScreen({center.x + 5 * std::cos(b), center.y + 5 * std::sin(b)}, center.z));
        }
        m_display->setPreview(viewId, std::move(lines));
        m_display->setVisible(true);
        updateCursor(m_showCursor ? cursorWindow : nullptr);
    }
}
