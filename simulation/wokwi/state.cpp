#include "state.h"

bool should_enter_abnormal(const WindowHistory& h, const PersistenceParams& p) {
    const auto& v = h.is_above_threshold;
    if (v.empty()) return false;
    if (p.k_required <= 0) return false;

    const int n = static_cast<int>(v.size());
    const int M = (p.m_window > 0 && p.m_window < n) ? p.m_window : n;

    if (p.require_consecutive) {
        int best_run = 0;
        int current_run = 0;
        for (int i = n - M; i < n; ++i) {
            if (v[i]) {
                current_run++;
                if (current_run > best_run) best_run = current_run;
            } else {
                current_run = 0;
            }
        }
        return best_run >= p.k_required;
    } else {
        int count = 0;
        for (int i = n - M; i < n; ++i) {
            if (v[i]) count++;
        }
        return count >= p.k_required;
    }
}

bool should_clear_abnormal(const WindowHistory& h, const PersistenceParams& p) {
    const auto& v = h.is_above_threshold;
    if (v.empty()) return true;

    int clear_req = (p.k_clear_required > 0) ? p.k_clear_required : p.k_required;
    const int n = static_cast<int>(v.size());
    if (n < clear_req) return false;

    // Check if the last `clear_req` windows were strictly normal (false)
    for (int i = n - clear_req; i < n; ++i) {
        if (v[i]) return false; // Found an abnormal window, do NOT clear
    }
    return true; // All recent windows were clean
}

SystemState update_state(SystemState current,
                         bool config_ok,
                         bool calibration_done,
                         bool persistence_abnormal,
                         bool clear_abnormal,
                         bool fault_condition) {
    if (fault_condition) {
        // Sticky hardware fault: SPI disconnection or sensor self-test failure
        return SystemState::FaultInvalid;
    }

    switch (current) {
    case SystemState::StartSelfCheck:
        if (config_ok) {
            return SystemState::Calibrating;
        }
        return current;

    case SystemState::Calibrating:
        if (calibration_done) {
            return SystemState::Normal;
        }
        return current;

    case SystemState::Normal:
        if (persistence_abnormal) {
            return SystemState::Abnormal;
        }
        return current;

    case SystemState::Abnormal:
        if (clear_abnormal) {
            return SystemState::Normal;
        }
        return current;

    case SystemState::FaultInvalid:
        return SystemState::FaultInvalid;
    }

    return current;
}

std::string to_string(SystemState s) {
    switch (s) {
    case SystemState::StartSelfCheck: return "StartSelfCheck";
    case SystemState::Calibrating:    return "Calibrating";
    case SystemState::Normal:         return "Normal";
    case SystemState::Abnormal:       return "Abnormal";
    case SystemState::FaultInvalid:   return "FaultInvalid";
    }
    return "Unknown";
}