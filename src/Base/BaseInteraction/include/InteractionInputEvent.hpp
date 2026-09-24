#pragma once

#include <cstdint>

class QKeyEvent;
class QMouseEvent;
class QWheelEvent;

struct MouseInputEvent {
    std::uint64_t viewId{0};
    QMouseEvent* event{nullptr};
};

struct WheelInputEvent {
    std::uint64_t viewId{0};
    QWheelEvent* event{nullptr};
};

struct KeyInputEvent {
    std::uint64_t viewId{0};
    QKeyEvent* event{nullptr};
};
