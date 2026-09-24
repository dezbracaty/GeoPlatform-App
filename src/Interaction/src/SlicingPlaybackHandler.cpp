#include "SlicingPlaybackHandler.hpp"

#include "SlicingPreviewBridge.hpp"
#include "Foundation/Log.h"

#include <algorithm>

SlicingPlaybackHandler::SlicingPlaybackHandler(QObject* parent)
    : QObject(parent)
    , m_playbackTimer(new QTimer(this)) {
    connect(m_playbackTimer, &QTimer::timeout,
            this, &SlicingPlaybackHandler::onPlaybackTick);

    auto* bridge = SlicingPreviewBridge::instance();
    connect(bridge, &SlicingPreviewBridge::playbackPlayRequested,
            this, &SlicingPlaybackHandler::performPlay, Qt::UniqueConnection);
    connect(bridge, &SlicingPreviewBridge::playbackPauseRequested,
            this, &SlicingPlaybackHandler::performPause, Qt::UniqueConnection);
    connect(bridge, &SlicingPreviewBridge::playbackStopRequested,
            this, &SlicingPlaybackHandler::performStop, Qt::UniqueConnection);
    connect(bridge, &SlicingPreviewBridge::playbackSpeedChangeRequested,
            this, &SlicingPlaybackHandler::setPlaybackSpeed, Qt::UniqueConnection);
    connect(bridge, &SlicingPreviewBridge::playbackLoopingChangeRequested,
            this, &SlicingPlaybackHandler::setIsLooping, Qt::UniqueConnection);
    connect(bridge, &SlicingPreviewBridge::currentToolpathPreviewChanged,
            this, &SlicingPlaybackHandler::performPause, Qt::UniqueConnection);

    syncPlaybackStateToBridge();
    LOG_INFO("SlicingPlaybackHandler created");
}

void SlicingPlaybackHandler::performPlay() {
    auto* bridge = SlicingPreviewBridge::instance();
    const int totalSteps = bridge->totalSteps();
    if (totalSteps <= 0) {
        LOG_WARN("Cannot play: selected layer range has no toolpath segments");
        return;
    }

    const int step = bridge->currentStep();
    if (step < 0 || step >= totalSteps - 1) {
        bridge->setCurrentStep(0);
    }

    if (!m_isPlaying) {
        m_isPlaying = true;
        syncPlaybackStateToBridge();
    }
    m_playbackTimer->start(
        std::max(1, static_cast<int>(50 / std::max(0.1f, m_playbackSpeed))));
}

void SlicingPlaybackHandler::performPause() {
    if (m_playbackTimer) {
        m_playbackTimer->stop();
    }
    if (!m_isPlaying) {
        return;
    }
    m_isPlaying = false;
    syncPlaybackStateToBridge();
}

void SlicingPlaybackHandler::performStop() {
    performPause();
    auto* bridge = SlicingPreviewBridge::instance();
    if (bridge->totalSteps() > 0) {
        bridge->setCurrentStep(0);
    }
}

void SlicingPlaybackHandler::setPlaybackSpeed(float speed) {
    m_playbackSpeed = std::max(0.1f, speed);
    syncPlaybackStateToBridge();
    if (m_isPlaying && m_playbackTimer) {
        m_playbackTimer->start(
            std::max(1, static_cast<int>(50 / m_playbackSpeed)));
    }
}

void SlicingPlaybackHandler::setIsLooping(bool looping) {
    m_isLooping = looping;
    syncPlaybackStateToBridge();
}

void SlicingPlaybackHandler::syncPlaybackStateToBridge() const {
    auto* bridge = SlicingPreviewBridge::instance();
    bridge->updatePlaybackPlaying(m_isPlaying);
    bridge->updatePlaybackSpeed(m_playbackSpeed);
    bridge->updatePlaybackLooping(m_isLooping);
}

void SlicingPlaybackHandler::onPlaybackTick() {
    advanceToNextStep();
}

void SlicingPlaybackHandler::advanceToNextStep() {
    auto* bridge = SlicingPreviewBridge::instance();
    const int totalSteps = bridge->totalSteps();
    if (totalSteps <= 0) {
        performPause();
        return;
    }

    const int step = bridge->currentStep();
    if (step < 0) {
        bridge->setCurrentStep(0);
    } else if (step < totalSteps - 1) {
        bridge->setCurrentStep(step + 1);
    } else if (m_isLooping) {
        bridge->setCurrentStep(0);
    } else if (m_isPlaying) {
        // A negative Step removes the shared cap and shows every selected layer fully.
        bridge->setCurrentStep(-1);
        performPause();
    }
}
