#include "PickService.hpp"

#include "Foundation/Log.h"

#include <DocumentManager.hpp>
#include <ModelInstanceDB.hpp>
#include <ModelPartDB.hpp>
#include <ViewportCoordinateSystem.hpp>
#include <WindowDB.hpp>

#include <QMetaObject>
#include <QMutexLocker>
#include <QWaitCondition>

#include <algorithm>
#include <cmath>
#include <vector>
#include <limits>

using namespace GPlatform::Rendering;

PickService::PickService(QObject* parent)
    : QObject(parent) {
    qRegisterMetaType<PickChannel>("PickChannel");
    qRegisterMetaType<PickSnapshot>("PickSnapshot");
    qRegisterMetaType<RenderPickRequest>(
        "GPlatform::Rendering::RenderPickRequest");
    qRegisterMetaType<RenderPickResult>(
        "GPlatform::Rendering::RenderPickResult");
}

bool PickService::supports(PickFeatures features,
                           PickDetail detail) noexcept {
    switch (detail) {
    case PickDetail::Object:
        return (features & pickFeature(PickFeature::Object)) != 0;
    case PickDetail::Surface:
        return (features & pickFeature(PickFeature::Surface)) != 0;
    case PickDetail::Primitive:
        return (features & pickFeature(PickFeature::Primitive)) != 0;
    }
    return false;
}

void PickService::attachRenderView(RenderViewAttachment attachment) {
    if (attachment.viewId == INVALID_DB_ID || attachment.generation == 0) {
        return;
    }

    std::shared_ptr<IRenderPickCapability> oldCapability;
    std::vector<std::uint64_t> oldRequests;
    {
        QMutexLocker locker(&m_mutex);
        const auto existing = m_views.find(attachment.viewId);
        if (existing != m_views.end()) {
            disconnect(existing->second.completionConnection);
            oldCapability = existing->second.capability;
            for (auto it = m_pending.begin(); it != m_pending.end();) {
                if (it->second.viewId == attachment.viewId) {
                    oldRequests.push_back(it->first);
                    it = m_pending.erase(it);
                } else {
                    ++it;
                }
            }
            for (auto it = m_cache.begin(); it != m_cache.end();) {
                if (it->first.first == attachment.viewId) {
                    it = m_cache.erase(it);
                } else {
                    ++it;
                }
            }
        }

        ViewRegistration registration;
        registration.role = attachment.role;
        registration.generation = attachment.generation;
        registration.logicalSize = attachment.logicalSize;
        registration.capability = std::move(attachment.pickCapability);
        if (registration.capability) {
            registration.completionConnection = connect(
                registration.capability.get(),
                &IRenderPickCapability::pickCompleted,
                this,
                [this](const RenderPickResult& result) {
                    onRenderPickCompleted(result);
                },
                Qt::QueuedConnection);
        }
        m_views[attachment.viewId] = std::move(registration);
    }

    if (oldCapability) {
        for (const auto requestId : oldRequests) {
            oldCapability->cancelPick(requestId);
        }
    }
    LOG_INFO("PickService attached View {} generation={}",
             attachment.viewId.getValue(), attachment.generation);
}

void PickService::updateRenderViewSize(DBInstanceID viewId,
                                       std::uint64_t generation,
                                       const QSizeF& logicalSize) {
    std::shared_ptr<IRenderPickCapability> capability;
    std::vector<std::uint64_t> requestIds;
    {
        QMutexLocker locker(&m_mutex);
        const auto view = m_views.find(viewId);
        if (view == m_views.end() || view->second.generation != generation ||
            view->second.logicalSize == logicalSize) {
            return;
        }
        view->second.logicalSize = logicalSize;
        ++view->second.viewportRevision;
        capability = view->second.capability;
        view->second.latestOutstanding.clear();
        for (auto it = m_pending.begin(); it != m_pending.end();) {
            if (it->second.viewId == viewId &&
                it->second.viewGeneration == generation) {
                requestIds.push_back(it->first);
                it = m_pending.erase(it);
            } else {
                ++it;
            }
        }
        for (auto it = m_cache.begin(); it != m_cache.end();) {
            if (it->first.first == viewId) {
                it = m_cache.erase(it);
            } else {
                ++it;
            }
        }
    }
    if (capability) {
        for (const auto requestId : requestIds) {
            capability->cancelPick(requestId);
        }
    }
}

void PickService::detachRenderView(DBInstanceID viewId,
                                   std::uint64_t generation) {
    std::shared_ptr<IRenderPickCapability> capability;
    std::vector<std::uint64_t> requestIds;
    {
        QMutexLocker locker(&m_mutex);
        const auto view = m_views.find(viewId);
        if (view == m_views.end() || view->second.generation != generation) {
            return;
        }
        capability = view->second.capability;
        disconnect(view->second.completionConnection);
        m_views.erase(view);
        for (auto it = m_pending.begin(); it != m_pending.end();) {
            if (it->second.viewId == viewId &&
                it->second.viewGeneration == generation) {
                requestIds.push_back(it->first);
                it = m_pending.erase(it);
            } else {
                ++it;
            }
        }
        for (auto it = m_cache.begin(); it != m_cache.end();) {
            if (it->first.first == viewId) {
                it = m_cache.erase(it);
            } else {
                ++it;
            }
        }
    }
    if (capability) {
        for (const auto requestId : requestIds) {
            capability->cancelPick(requestId);
        }
    }
    LOG_INFO("PickService detached View {} generation={}",
             viewId.getValue(), generation);
}

std::uint64_t PickService::requestForView(
    DBInstanceID viewId,
    const QPoint& screenPosition,
    PickChannel channel,
    PickDetail detail,
    PickDelivery delivery) {
    if (viewId == INVALID_DB_ID) {
        return 0;
    }

    RenderPickRequest request;
    PendingRequest pending;
    std::shared_ptr<IRenderPickCapability> capability;
    std::uint64_t supersededId = 0;
    bool supported = false;
    {
        QMutexLocker locker(&m_mutex);
        auto view = m_views.find(viewId);
        if (view == m_views.end() ||
            view->second.logicalSize.width() <= 0.0 ||
            view->second.logicalSize.height() <= 0.0) {
            return 0;
        }
        if (screenPosition.x() < 0 || screenPosition.y() < 0 ||
            screenPosition.x() >= view->second.logicalSize.width() ||
            screenPosition.y() >= view->second.logicalSize.height()) {
            return 0;
        }

        capability = view->second.capability;
        request.requestId = m_nextRequestId++;
        request.detail = detail;
        request.normalizedPosition = {
            screenPosition.x() / view->second.logicalSize.width(),
            screenPosition.y() / view->second.logicalSize.height()};

        pending = {viewId, view->second.generation,
                   view->second.viewportRevision, channel, detail,
                   screenPosition, capability};
        if (delivery == PickDelivery::LatestOnly) {
            const auto outstanding =
                view->second.latestOutstanding.find(channel);
            if (outstanding != view->second.latestOutstanding.end()) {
                const auto old = m_pending.find(outstanding->second);
                if (old != m_pending.end()) {
                    supersededId = old->first;
                    m_pending.erase(old);
                }
            }
            view->second.latestOutstanding[channel] = request.requestId;
        }
        m_pending.emplace(request.requestId, pending);
        supported = capability && supports(capability->capabilities(), detail);
    }

    if (supersededId != 0 && capability) {
        capability->cancelPick(supersededId);
    }
    if (!supported) {
        completeUnsupported(pending, request.requestId);
    } else {
        capability->requestPick(request);
    }
    return request.requestId;
}

RenderPickResult PickService::pickCpuForView(
    DBInstanceID viewId,
    const QPoint& screenPosition,
    PickDetail detail) const {
    RenderPickResult result;
    result.windowId = viewId;
    result.status = PickStatus::Miss;
    if (viewId == INVALID_DB_ID) {
        return result;
    }

    {
        QMutexLocker locker(&m_mutex);
        const auto registered = m_views.find(viewId);
        if (registered == m_views.end() ||
            registered->second.logicalSize.width() <= 0.0 ||
            registered->second.logicalSize.height() <= 0.0 ||
            screenPosition.x() < 0 || screenPosition.y() < 0 ||
            screenPosition.x() >= registered->second.logicalSize.width() ||
            screenPosition.y() >= registered->second.logicalSize.height()) {
            return result;
        }
    }

    auto* document = DocumentManager::instance();
    const auto window = document
        ? std::dynamic_pointer_cast<WindowDB>(document->getDBInstance(viewId))
        : nullptr;
    const auto coordinates = window ? window->getCoordinateSystem() : nullptr;
    if (!coordinates) {
        result.status = PickStatus::Unsupported;
        return result;
    }

    const auto projection = coordinates->projectionSnapshot();
    if (!projection.isValid()) {
        result.status = PickStatus::Unsupported;
        return result;
    }
    const WorldRay ray = projection.rayFromScreen(QPointF(screenPosition));

    float nearestDistance = std::numeric_limits<float>::infinity();
    ModelInstanceDB::RayHit nearestInstanceHit;
    std::shared_ptr<ModelInstanceDB> nearestInstance;
    for (const auto& object :
         document->getDBInstancesByType(TypeID::MODEL_INSTANCE_DB)) {
        auto instance = std::dynamic_pointer_cast<ModelInstanceDB>(object);
        if (!instance || !instance->getVisible() || !instance->isPickable() ||
            !instance->isValid()) {
            continue;
        }
        const auto hit = instance->intersectWorldRay(
            ray.origin, ray.direction, nearestDistance);
        if (!hit || hit->worldDistance >= nearestDistance) continue;
        nearestDistance = hit->worldDistance;
        nearestInstanceHit = *hit;
        nearestInstance = std::move(instance);
    }

    if (nearestInstance) {
        result.status = PickStatus::Hit;
        result.payload.objectId = nearestInstance->getDBInstanceID();
        if (detail != PickDetail::Object &&
            nearestInstanceHit.partId.isValid()) {
            result.payload.partId = nearestInstanceHit.partId;
        }
        if (detail == PickDetail::Surface || detail == PickDetail::Primitive) {
            result.payload.localPosition =
                nearestInstanceHit.objectLocalPosition;
            result.payload.worldPosition = nearestInstanceHit.worldPosition;
        }
        if (detail == PickDetail::Primitive) {
            result.payload.primitiveIndex =
                nearestInstanceHit.primitiveIndex;
        }
        return result;
    }

    return result;
}

RenderPickResult PickService::pickRendererForViewBlocking(
    DBInstanceID viewId,
    const QPoint& screenPosition,
    PickDetail detail,
    int timeoutMilliseconds) {
    RenderPickResult fallback;
    fallback.windowId = viewId;
    fallback.status = PickStatus::Unsupported;

    RenderPickRequest request;
    std::shared_ptr<IRenderPickCapability> capability;
    {
        QMutexLocker locker(&m_mutex);
        const auto view = m_views.find(viewId);
        if (view == m_views.end() ||
            view->second.logicalSize.width() <= 0.0 ||
            view->second.logicalSize.height() <= 0.0 ||
            screenPosition.x() < 0 || screenPosition.y() < 0 ||
            screenPosition.x() >= view->second.logicalSize.width() ||
            screenPosition.y() >= view->second.logicalSize.height()) {
            return fallback;
        }
        capability = view->second.capability;
        if (!capability || !supports(capability->capabilities(), detail)) {
            return fallback;
        }
        request.requestId = m_nextRequestId++;
        request.detail = detail;
        request.normalizedPosition = {
            screenPosition.x() / view->second.logicalSize.width(),
            screenPosition.y() / view->second.logicalSize.height()};
    }

    struct WaitState {
        QMutex mutex;
        QWaitCondition condition;
        std::optional<RenderPickResult> result;
    };
    const auto state = std::make_shared<WaitState>();
    const auto connection = connect(
        capability.get(), &IRenderPickCapability::pickCompleted,
        this,
        [state, requestId = request.requestId, viewId](
            const RenderPickResult& completed) {
            if (completed.requestId != requestId ||
                completed.windowId != viewId) {
                return;
            }
            QMutexLocker locker(&state->mutex);
            state->result = completed;
            state->condition.wakeAll();
        },
        Qt::DirectConnection);

    capability->requestPick(request);
    {
        QMutexLocker locker(&state->mutex);
        if (!state->result) {
            state->condition.wait(&state->mutex,
                                  std::max(1, timeoutMilliseconds));
        }
    }
    disconnect(connection);

    QMutexLocker locker(&state->mutex);
    if (state->result) {
        return *state->result;
    }
    capability->cancelPick(request.requestId);
    fallback.requestId = request.requestId;
    fallback.status = PickStatus::Stale;
    return fallback;
}

void PickService::completeUnsupported(const PendingRequest& pending,
                                      std::uint64_t requestId) {
    QMetaObject::invokeMethod(
        this,
        [this, pending, requestId] {
            RenderPickResult result;
            result.requestId = requestId;
            result.windowId = pending.viewId;
            result.status = PickStatus::Unsupported;
            onRenderPickCompleted(result);
        },
        Qt::QueuedConnection);
}

void PickService::onRenderPickCompleted(const RenderPickResult& result) {
    PickSnapshot snapshot;
    {
        QMutexLocker locker(&m_mutex);
        const auto pending = m_pending.find(result.requestId);
        if (pending == m_pending.end()) {
            return;
        }
        const PendingRequest request = pending->second;
        m_pending.erase(pending);

        const auto view = m_views.find(request.viewId);
        if (view == m_views.end() ||
            view->second.generation != request.viewGeneration ||
            view->second.viewportRevision != request.viewportRevision ||
            result.windowId != request.viewId) {
            return;
        }
        const auto outstanding =
            view->second.latestOutstanding.find(request.channel);
        if (outstanding != view->second.latestOutstanding.end() &&
            outstanding->second == result.requestId) {
            view->second.latestOutstanding.erase(outstanding);
        }

        snapshot.requestId = result.requestId;
        snapshot.viewId = request.viewId;
        snapshot.viewGeneration = request.viewGeneration;
        snapshot.viewportRevision = request.viewportRevision;
        snapshot.channel = request.channel;
        snapshot.detail = request.detail;
        snapshot.status = result.status;
        snapshot.screenPosition = request.screenPosition;
        snapshot.result = result.payload;
        if (snapshot.isReusable()) {
            const CacheKey key{snapshot.viewId, snapshot.channel};
            const auto cached = m_cache.find(key);
            if (cached == m_cache.end() ||
                cached->second.requestId < snapshot.requestId) {
                m_cache[key] = snapshot;
            }
        }
    }
    emit pickCompleted(snapshot);
}

std::optional<PickSnapshot> PickService::cachedForView(
    DBInstanceID viewId,
    PickChannel channel,
    PickDetail detail,
    const QPoint& screenPosition,
    int toleranceLogicalPixels) const {
    QMutexLocker locker(&m_mutex);
    const auto cache = m_cache.find({viewId, channel});
    if (cache == m_cache.end() || cache->second.detail != detail ||
        !cache->second.isReusable()) {
        return std::nullopt;
    }
    const auto view = m_views.find(viewId);
    if (view == m_views.end() ||
        view->second.generation != cache->second.viewGeneration ||
        view->second.viewportRevision != cache->second.viewportRevision) {
        return std::nullopt;
    }
    if ((cache->second.screenPosition - screenPosition).manhattanLength() >
        std::max(0, toleranceLogicalPixels)) {
        return std::nullopt;
    }
    return cache->second;
}

std::optional<PickSnapshot> PickService::latestForView(
    DBInstanceID viewId,
    PickChannel channel,
    PickDetail detail) const {
    QMutexLocker locker(&m_mutex);
    const auto cache = m_cache.find({viewId, channel});
    if (cache == m_cache.end() || cache->second.detail != detail ||
        !cache->second.isReusable()) {
        return std::nullopt;
    }
    const auto view = m_views.find(viewId);
    if (view == m_views.end() ||
        view->second.generation != cache->second.viewGeneration ||
        view->second.viewportRevision != cache->second.viewportRevision) {
        return std::nullopt;
    }
    return cache->second;
}

PickFeatures PickService::capabilitiesForView(DBInstanceID viewId) const {
    QMutexLocker locker(&m_mutex);
    const auto view = m_views.find(viewId);
    return view != m_views.end() && view->second.capability
        ? view->second.capability->capabilities()
        : PickFeatures{0};
}

bool PickService::supportsFeature(
    DBInstanceID viewId,
    PickFeature feature) const {
    return (capabilitiesForView(viewId) & pickFeature(feature)) != 0;
}

void PickService::cancel(std::uint64_t requestId) {
    std::shared_ptr<IRenderPickCapability> capability;
    {
        QMutexLocker locker(&m_mutex);
        const auto pending = m_pending.find(requestId);
        if (pending == m_pending.end()) {
            return;
        }
        capability = pending->second.capability;
        const auto view = m_views.find(pending->second.viewId);
        if (view != m_views.end()) {
            auto& latest = view->second.latestOutstanding;
            const auto channel = latest.find(pending->second.channel);
            if (channel != latest.end() && channel->second == requestId) {
                latest.erase(channel);
            }
        }
        m_pending.erase(pending);
    }
    if (capability) {
        capability->cancelPick(requestId);
    }
}

GPlatform::Rendering::RendererRole PickService::roleForView(DBInstanceID viewId) const {
    QMutexLocker locker(&m_mutex);
    const auto it = m_views.find(viewId);
    return it == m_views.end() ? GPlatform::Rendering::RendererRole::None : it->second.role;
}
