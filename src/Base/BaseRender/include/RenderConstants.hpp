#pragma once

namespace RenderConstants {
    // Thread wait settings
    constexpr int RENDER_WAIT_TIMEOUT_MS = 100; // Wait interval for render condition
    constexpr int MAX_RENDER_WAIT_COUNT = 50;   // Max wait iterations (5 seconds)
    constexpr int MAX_SHUTDOWN_WAIT_COUNT = 20; // Max shutdown wait iterations (2 seconds)

    // Timer settings
    constexpr int RESIZE_TIMER_DELAY_MS = 100;  // Delay before processing resize
    constexpr int RESIZE_RETRY_DELAY_MS = 16;   // Retry next frame when resize collides with rendering

    // Logging settings
    constexpr int LOG_THROTTLE_MS = 1000; // Throttle warning messages

    // DB Generation settings (for demo/testing)
    constexpr int DB_GENERATION_CLEAR_THRESHOLD = 20;       // Clear scene after 20 objects
    constexpr int DB_GENERATION_DEFAULT_INTERVAL_MS = 1000; // Default generation interval

    // Random generation ranges (for demo/testing)
    constexpr float RANDOM_POSITION_MIN = -5.0f;
    constexpr float RANDOM_POSITION_MAX = 5.0f;
    constexpr float RANDOM_SIZE_MIN = 0.5f;
    constexpr float RANDOM_SIZE_MAX = 1.5f;
}
