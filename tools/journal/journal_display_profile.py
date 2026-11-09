"""The shared GPlatform recording/playback display contract."""

import math
import time


WINDOW_PROFILE = {
    "window": {"objectName": "gplatform.main"},
    "logicalSize": [1382, 954],
}


def validate_preconditions(preconditions):
    if not isinstance(preconditions, dict):
        raise ValueError("metadata.preconditions must be an object")
    logical = preconditions.get("mainWindowLogicalSize", WINDOW_PROFILE["logicalSize"])
    if logical != WINDOW_PROFILE["logicalSize"]:
        raise ValueError(f"Scenario logical window {logical} conflicts with {WINDOW_PROFILE['logicalSize']}")
    # Legacy DPR/imageSize describe the baseline, not the current display.
    dpr = preconditions.get("devicePixelRatio")
    if dpr is not None:
        device_pixel_ratio({"devicePixelRatio": dpr})
    size = preconditions.get("imageSize")
    if size is not None and (not isinstance(size, list) or len(size) != 2 or
            any(type(value) is not int or value <= 0 for value in size)):
        raise ValueError(f"Invalid baseline imageSize: {size}")


def process_environment(inherited, scenario, *, environment_overrides=None):
    environment = dict(inherited)
    environment.update(scenario)
    # Preserve native scaling by default. A caller may explicitly configure
    # its test child after ambient/scenario scaling overrides are cleared.
    for key in ("QT_DEVICE_PIXEL_RATIO", "QT_AUTO_SCREEN_SCALE_FACTOR", "QT_FONT_DPI",
                "QT_SCALE_FACTOR", "QT_SCREEN_SCALE_FACTORS", "QT_ENABLE_HIGHDPI_SCALING",
                "QT_SCALE_FACTOR_ROUNDING_POLICY"):
        environment.pop(key, None)
    if environment_overrides is not None:
        if not isinstance(environment_overrides, dict):
            raise ValueError("environment_overrides must be an object")
        for key, value in environment_overrides.items():
            if (not isinstance(key, str) or not key or "=" in key or "\0" in key or
                    not isinstance(value, str) or "\0" in value):
                raise ValueError("environment_overrides requires valid string keys and values")
        environment.update(environment_overrides)
    return environment


def device_pixel_ratio(info):
    value = info.get("devicePixelRatio")
    if (isinstance(value, bool) or not isinstance(value, (int, float)) or
            not math.isfinite(value) or value <= 0):
        raise RuntimeError(f"Invalid application window DPR: {value}")
    return value


def validate_window_capacity(info):
    available = info.get("availableLogicalSize")
    if (not isinstance(available, list) or len(available) != 2 or
            any(isinstance(value, bool) or not isinstance(value, (int, float)) or
                not math.isfinite(value) or value <= 0 for value in available)):
        raise RuntimeError(f"Invalid application screen area: {available}")
    if any(actual < required for actual, required in zip(available, WINDOW_PROFILE["logicalSize"])):
        raise RuntimeError(f"Journal requires 1382x954 logical pixels; "
                           f"available screen area is {available[0]}x{available[1]}. "
                           "Use a larger display or a dedicated virtual display.")


def wait_for_window(client, timeout=3):
    deadline = None
    while True:
        info = client.window_info(WINDOW_PROFILE["window"])
        device_pixel_ratio(info)
        validate_window_capacity(info)
        if info.get("logicalSize") == WINDOW_PROFILE["logicalSize"]:
            return info
        if deadline is None:
            deadline = time.monotonic() + timeout
        if time.monotonic() >= deadline:
            raise RuntimeError(f"Journal window did not reach 1382x954: {info}")
        time.sleep(0.02)
