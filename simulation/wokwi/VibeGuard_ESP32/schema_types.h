#pragma once
#include <Arduino.h>
#include <vector>
#include <cstdint>

enum class SystemState : uint8_t {
    StartSelfCheck = 0,
    Calibrating    = 1,
    Normal         = 2,
    Abnormal       = 3,
    FaultInvalid   = 4
};

struct PersistenceParams {
    int k_required;
    int m_window;
    int k_clear_required;
    bool require_consecutive;
};

struct WindowHistory {
    std::vector<bool> is_above_threshold;
};

struct AccelGData {
    float x;
    float y;
    float z;
};
