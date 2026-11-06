#ifndef STATE_H
#define STATE_H

#include <vector>
#include <string>
#include <cstdint>

enum class SystemState : uint8_t {
    StartSelfCheck = 0,
    Calibrating    = 1,
    Normal         = 2,
    Abnormal       = 3,
    FaultInvalid   = 4
};

struct PersistenceParams {
    int k_required;          // K: number of abnormal windows required to trigger alarm
    int m_window;            // M: window size to look back (e.g. 5)
    int k_clear_required;    // Number of consecutive clean windows required to clear alarm (hysteresis)
    bool require_consecutive;
};

struct WindowHistory {
    std::vector<bool> is_above_threshold;
};

bool should_enter_abnormal(const WindowHistory& h, const PersistenceParams& p);
bool should_clear_abnormal(const WindowHistory& h, const PersistenceParams& p);

// State machine transition function
SystemState update_state(SystemState current,
                         bool config_ok,
                         bool calibration_done,
                         bool persistence_abnormal,
                         bool clear_abnormal,
                         bool fault_condition);

std::string to_string(SystemState s);

#endif // STATE_H