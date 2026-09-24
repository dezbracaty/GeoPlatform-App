#pragma once

#include <QObject>
#include <QTimer>

class SlicingPlaybackHandler : public QObject {
    Q_OBJECT

public:
    explicit SlicingPlaybackHandler(QObject* parent = nullptr);
    ~SlicingPlaybackHandler() override = default;

private:
    void performPlay();
    void performPause();
    void performStop();
    void setPlaybackSpeed(float speed);
    void setIsLooping(bool looping);
    void syncPlaybackStateToBridge() const;
    void advanceToNextStep();

private slots:
    void onPlaybackTick();

private:
    QTimer* m_playbackTimer = nullptr;
    bool m_isPlaying = false;
    float m_playbackSpeed = 1.0f;
    bool m_isLooping = false;
};
