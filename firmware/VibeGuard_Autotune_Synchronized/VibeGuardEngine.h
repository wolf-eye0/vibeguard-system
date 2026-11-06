#pragma once

#include <Arduino.h>
#include <Preferences.h>
#include <cmath>
#include <cstring>

// ============================================================================
// VIBEGUARD: 4-STAGE ROTORDYNAMIC DSP & ADAPTIVE CALIBRATION ENGINE
// Based on Deep Research: Substrate-Invariant Condition Metric J(t)
// Homogeneous of Degree Zero with Bounded-Lag Autocorrelation Tracking
// ============================================================================

static constexpr float    VG_FS_HZ        = 800.0f;    // 800 Hz Output Data Rate
static constexpr uint32_t VG_TS_US        = 1250UL;    // 1250 us sample period
static constexpr size_t   VG_WIN_N        = 128;       // 160 ms analysis window
static constexpr float    VG_LSB_TO_G     = 0.00390625f; // +-16g Full Res (1/256 g/LSB)

// Autocorrelation Lag Bounds for 1x Speed Tracking (7.62 Hz to 14.55 Hz @ 800 Hz)
static constexpr size_t VG_LAG_MIN = 55;
static constexpr size_t VG_LAG_MAX = 105;
static constexpr size_t VG_NUM_LAGS = (VG_LAG_MAX - VG_LAG_MIN + 1);

// Calibration Capture Length
static constexpr size_t VG_CAL_SETTLE_WINDOWS  = 5;   // ~0.8 s settle time
static constexpr size_t VG_CAL_CAPTURE_WINDOWS = 50;  // 8.0 s baseline capture

enum VibeStage : uint8_t {
    STAGE_0_NORMAL   = 0, // Bare Arm (0.0 g added mass)
    STAGE_1_WARNING  = 1, // M3x20mm Bolt (+2.5 g)
    STAGE_2_MODERATE = 2, // Bolt + 1 Steel Nut (+3.4 g)
    STAGE_3_CRITICAL = 3  // Bolt + 2 Steel Nuts (+4.3 g)
};

enum CalState : uint8_t {
    CAL_IDLE = 0,
    CAL_SETTLING_S0,
    CAL_CAPTURING_S0,
    CAL_COMPUTING
};

struct BiquadSection {
    float b0, b1, b2;
    float a1, a2;
    float w1, w2;

    inline void reset() { w1 = 0.0f; w2 = 0.0f; }

    inline float step(float x) {
        float y = b0 * x + w1;
        w1 = b1 * x - a1 * y + w2;
        w2 = b2 * x - a2 * y;
        return y;
    }
};

struct CalibrationProfile {
    uint32_t magic;
    float rx0;
    float ry0;
    float rz0;
    float cf0;
    float exy_lf0;
    float f1_0;
    float sigma_j0;
    float t01_enter, t10_exit;
    float t12_enter, t21_exit;
    float t23_enter, t32_exit;
};

struct TelemetryFrame {
    uint32_t windowIdx;
    uint32_t dspExecUs;
    float rx, ry, rz, r3d;
    float peakVector, crestFactor;
    float rLowXY, f1Hz, cSag;
    float rawJ, medJ, smoothedJ;
    bool shockRejected;
    VibeStage committedStage;
};

class VibeGuardEngine {
public:
    VibeGuardEngine() {
        initFilters();
        loadDefaultCalibration();
        resetStateEngine();
    }

    void initFilters() {
        // 4th-Order Butterworth Low-Pass Filter (fc = 25.0 Hz @ fs = 800.0 Hz)
        // Pre-warped cascaded Direct-Form II Transposed SOS sections
        sosX_[0] = { 8.5320090e-3f, 1.7064018e-2f, 8.5320090e-3f, -1.6620102f, 0.6945707f, 0.0f, 0.0f };
        sosX_[1] = { 8.5293568e-3f, 1.7058714e-2f, 8.5293568e-3f, -1.8252980f, 0.8610575f, 0.0f, 0.0f };
        sosY_[0] = sosX_[0];
        sosY_[1] = sosX_[1];
    }

    void loadDefaultCalibration() {
        cal_.magic     = 0x56473236UL; // 'VG26'
        cal_.rx0       = 0.447f;
        cal_.ry0       = 0.509f;
        cal_.rz0       = 0.327f;
        cal_.cf0       = 2.368f;
        cal_.exy_lf0   = 0.0385f;      // (0.196 g)^2 low-pass baseline energy
        cal_.f1_0      = 10.31f;
        cal_.sigma_j0  = 0.055f;
        cal_.t01_enter = 0.80f;
        cal_.t10_exit  = 0.40f;
        cal_.t12_enter = 2.10f;
        cal_.t21_exit  = 1.70f;
        cal_.t23_enter = 6.00f;
        cal_.t32_exit  = 5.00f;
    }

    void resetStateEngine() {
        sosX_[0].reset(); sosX_[1].reset();
        sosY_[0].reset(); sosY_[1].reset();
        f1TrackerHz_   = cal_.f1_0;
        jHat_          = 0.0f;
        medBuf_[0]     = 0.0f;
        medBuf_[1]     = 0.0f;
        medBuf_[2]     = 0.0f;
        medIdx_        = 0;
        spikeCount_    = 0;
        countUp_       = 0;
        countDown_     = 0;
        currentStage_  = STAGE_0_NORMAL;
        windowCounter_ = 0;
        calState_      = CAL_IDLE;
        calCollected_  = 0;
    }

    bool loadFromNVS(Preferences& prefs) {
        if (!prefs.begin("vibeguard", true)) return false;
        size_t len = prefs.getBytesLength("cal_prof");
        if (len != sizeof(CalibrationProfile)) {
            prefs.end();
            return false;
        }
        CalibrationProfile temp;
        prefs.getBytes("cal_prof", &temp, sizeof(CalibrationProfile));
        prefs.end();

        if (temp.magic != 0x56473236UL || temp.rx0 <= 0.01f || temp.ry0 <= 0.01f) {
            return false;
        }
        cal_ = temp;
        // Substrate-Invariant Rotordynamic Threshold Ladder:
        // S0 (<0.80) -> S1 Warning (0.80-2.10) -> S2 Moderate Alarm (2.10-6.00) -> S3 Critical Alarm (>6.00)
        cal_.t01_enter = 0.80f;
        cal_.t10_exit  = 0.40f;
        cal_.t12_enter = 2.10f;
        cal_.t21_exit  = 1.70f;
        cal_.t23_enter = 6.00f;
        cal_.t32_exit  = 5.00f;
        f1TrackerHz_ = cal_.f1_0;
        saveToNVS(prefs);
        return true;
    }

    void saveToNVS(Preferences& prefs) {
        if (!prefs.begin("vibeguard", false)) return;
        cal_.magic = 0x56473236UL;
        prefs.putBytes("cal_prof", &cal_, sizeof(CalibrationProfile));
        prefs.end();
    }

    void startBaselineCalibration() {
        calState_     = CAL_SETTLING_S0;
        calCollected_ = 0;
    }

    CalState getCalibrationState() const { return calState_; }
    const CalibrationProfile& getCalibration() const { return cal_; }
    VibeStage getCommittedStage() const { return currentStage_; }
    float getSmoothedJ() const { return jHat_; }

    TelemetryFrame processWindow(const float* __restrict axRaw,
                                 const float* __restrict ayRaw,
                                 const float* __restrict azRaw,
                                 Preferences& prefs) {
        uint32_t tStartUs = micros();
        TelemetryFrame out = {};
        out.windowIdx = ++windowCounter_;

        // STAGE 1: Exact Window Mean (DC & Gravity Vector) Detrending
        float sumX = 0.0f, sumY = 0.0f, sumZ = 0.0f;
        for (size_t n = 0; n < VG_WIN_N; ++n) {
            sumX += axRaw[n];
            sumY += ayRaw[n];
            sumZ += azRaw[n];
        }
        const float invN = 1.0f / static_cast<float>(VG_WIN_N);
        const float muX = sumX * invN;
        const float muY = sumY * invN;
        const float muZ = sumZ * invN;

        // STAGE 2: Broad-Band RMS, Peak Vector & 4th-Order 25 Hz IIR Filtering
        float sqX = 0.0f, sqY = 0.0f, sqZ = 0.0f;
        float maxVecSq = 0.0f;
        float sqLowXY = 0.0f;

        for (size_t n = 0; n < VG_WIN_N; ++n) {
            const float ax = axRaw[n] - muX;
            const float ay = ayRaw[n] - muY;
            const float az = azRaw[n] - muZ;

            const float x2 = ax * ax;
            const float y2 = ay * ay;
            const float z2 = az * az;
            sqX += x2;
            sqY += y2;
            sqZ += z2;

            const float vecSq = x2 + y2 + z2;
            if (vecSq > maxVecSq) maxVecSq = vecSq;

            // Pass X and Y through cascaded 4th-order Butterworth LP filter
            const float axLP = sosX_[1].step(sosX_[0].step(ax));
            const float ayLP = sosY_[1].step(sosY_[0].step(ay));
            sqLowXY += (axLP * axLP + ayLP * ayLP);
            xyLowBuf_[n] = axLP + ayLP; // Combined planar signal for speed tracking
        }

        out.rx = sqrtf(sqX * invN);
        out.ry = sqrtf(sqY * invN);
        out.rz = sqrtf(sqZ * invN);
        out.r3d = sqrtf((sqX + sqY + sqZ) * invN);
        out.peakVector = sqrtf(maxVecSq);
        out.crestFactor = (out.r3d > 1e-4f) ? (out.peakVector / out.r3d) : 1.0f;

        const float exyLF = sqLowXY * invN;
        out.rLowXY = sqrtf(exyLF);

        // STAGE 3: Sensorless 1x Rotational Frequency via Bounded Autocorrelation
        float r0 = 0.0f;
        for (size_t n = 0; n < VG_WIN_N; ++n) {
            r0 += xyLowBuf_[n] * xyLowBuf_[n];
        }
        r0 *= invN;

        float rLag[VG_NUM_LAGS];
        size_t bestIdx = 0;
        float maxR = -1e9f;

        for (size_t idx = 0; idx < VG_NUM_LAGS; ++idx) {
            const size_t k = VG_LAG_MIN + idx;
            const size_t limit = VG_WIN_N - k;
            float acc = 0.0f;
            for (size_t n = 0; n < limit; ++n) {
                acc += xyLowBuf_[n] * xyLowBuf_[n + k];
            }
            const float val = acc / static_cast<float>(limit);
            rLag[idx] = val;
            if (val > maxR) {
                maxR = val;
                bestIdx = idx;
            }
        }

        const float coherence = (r0 > 1e-6f) ? (maxR / r0) : 0.0f;
        if (coherence > 0.22f && bestIdx > 0 && bestIdx < (VG_NUM_LAGS - 1)) {
            const float rPrev = rLag[bestIdx - 1];
            const float rCurr = rLag[bestIdx];
            const float rNext = rLag[bestIdx + 1];
            const float denom = 2.0f * (rPrev - 2.0f * rCurr + rNext);
            float deltaK = 0.0f;
            if (fabsf(denom) > 1e-7f) {
                deltaK = (rPrev - rNext) / denom;
                if (deltaK < -0.5f) deltaK = -0.5f;
                if (deltaK >  0.5f) deltaK =  0.5f;
            }
            const float exactLag = static_cast<float>(VG_LAG_MIN + bestIdx) + deltaK;
            float fInst = VG_FS_HZ / exactLag;
            if (fInst < 7.5f)  fInst = 7.5f;
            if (fInst > 14.0f) fInst = 14.0f;
            f1TrackerHz_ = 0.80f * f1TrackerHz_ + 0.20f * fInst;
        }

        out.f1Hz = f1TrackerHz_;

        // Speed-Sag Compensation Factor C_sag = (f1_0 / f1(t))^2
        const float freqRatio = cal_.f1_0 / f1TrackerHz_;
        out.cSag = freqRatio * freqRatio;
        if (out.cSag < 0.75f) out.cSag = 0.75f;
        if (out.cSag > 1.45f) out.cSag = 1.45f;

        // STAGE 4: Compute Substrate-Invariant Condition Metric J(t)
        out.rawJ = computeConditionMetric(out.ry, out.r3d, out.cSag);

        // Handle Active Automated Calibration if Triggered
        if (calState_ != CAL_IDLE) {
            handleCalibrationStep(out.rx, out.ry, out.rz, out.r3d, out.crestFactor, exyLF, out.f1Hz, coherence, prefs);
        }

        // STAGE 5: Spike Rejection Filter & 4-Stage Hysteresis State Machine
        const float innov = out.rawJ - jHat_;
        const float rzRatio = (cal_.rz0 > 1e-3f) ? (out.rz / cal_.rz0) : 1.0f;
        const bool isShock = (out.crestFactor > 4.80f) ||
                             (rzRatio > 1.85f) ||
                             ((fabsf(innov) > 3.50f) && (coherence < 0.22f));

        float jGated = out.rawJ;
        if (isShock && spikeCount_ < 2) {
            spikeCount_++;
            jGated = jHat_; // Freeze state innovation during transient <320ms shock
            out.shockRejected = true;
        } else {
            if (!isShock) spikeCount_ = 0;
            out.shockRejected = false;

            // Huber Saturation Limiter (delta_Huber = 2.20)
            static constexpr float HUBER_DELTA = 2.20f;
            if (innov > HUBER_DELTA)       jGated = jHat_ + HUBER_DELTA;
            else if (innov < -HUBER_DELTA) jGated = jHat_ - HUBER_DELTA;
            else                           jGated = out.rawJ;
        }

        // 3-Window Sliding Median Filter
        medBuf_[medIdx_] = jGated;
        medIdx_ = (medIdx_ + 1) % 3;
        out.medJ = medianOf3(medBuf_[0], medBuf_[1], medBuf_[2]);

        // Dual-Rate Asymmetric Exponential Moving Average
        const float alphaEMA = (out.medJ >= jHat_) ? 0.22f : 0.18f;
        jHat_ = jHat_ + alphaEMA * (out.medJ - jHat_);
        out.smoothedJ = jHat_;

        // Commit 4-Stage Hysteresis State Machine
        updateHysteresisState(jHat_);
        out.committedStage = currentStage_;
        out.dspExecUs = micros() - tStartUs;

        return out;
    }

private:
    BiquadSection sosX_[2];
    BiquadSection sosY_[2];
    float xyLowBuf_[VG_WIN_N];
    CalibrationProfile cal_;
    float f1TrackerHz_;
    float jHat_;
    float medBuf_[3];
    uint8_t medIdx_;
    uint8_t spikeCount_;
    uint8_t countUp_;
    uint8_t countDown_;
    VibeStage currentStage_;
    uint32_t windowCounter_;

    // Calibration Capture Buffers
    CalState calState_;
    size_t calCollected_;
    float calRx_[VG_CAL_CAPTURE_WINDOWS];
    float calRy_[VG_CAL_CAPTURE_WINDOWS];
    float calRz_[VG_CAL_CAPTURE_WINDOWS];
    float calR3D_[VG_CAL_CAPTURE_WINDOWS];
    float calCF_[VG_CAL_CAPTURE_WINDOWS];
    float calExyLF_[VG_CAL_CAPTURE_WINDOWS];
    float calF1_[VG_CAL_CAPTURE_WINDOWS];
    float calJ0_[VG_CAL_CAPTURE_WINDOWS];

    inline float computeConditionMetric(float ry, float r3d, float cSag) const {
        const float r3d0 = sqrtf(cal_.rx0 * cal_.rx0 + cal_.ry0 * cal_.ry0 + cal_.rz0 * cal_.rz0);
        if (r3d0 < 1e-3f) return 0.0f;
        const float ratio3D = r3d / r3d0;
        // Rotordynamic kinetic energy growth: E_rel = (R_3D / R_3D,0)^2 - 1
        const float eRel = (ratio3D * ratio3D) - 1.0f;
        const float metric = 5.0f * (eRel > -0.5f ? eRel : -0.5f);
        return cSag * metric;
    }

    static inline float medianOf3(float a, float b, float c) {
        if (a > b) { float t = a; a = b; b = t; }
        if (b > c) { float t = b; b = c; c = t; }
        if (a > b) { float t = a; a = b; b = t; }
        return b;
    }

    static float computeArrayMedian(float* arr, size_t n) {
        // In-place insertion sort for small calibration arrays (N = 50)
        for (size_t i = 1; i < n; ++i) {
            float key = arr[i];
            int j = static_cast<int>(i) - 1;
            while (j >= 0 && arr[j] > key) {
                arr[j + 1] = arr[j];
                j--;
            }
            arr[j + 1] = key;
        }
        if (n % 2 == 0) {
            return 0.5f * (arr[n / 2 - 1] + arr[n / 2]);
        }
        return arr[n / 2];
    }

    static float computeMADSigma(const float* sortedArr, size_t n, float med) {
        float dev[VG_CAL_CAPTURE_WINDOWS];
        for (size_t i = 0; i < n; ++i) {
            dev[i] = fabsf(sortedArr[i] - med);
        }
        return 1.4826f * computeArrayMedian(dev, n);
    }

    void handleCalibrationStep(float rx, float ry, float rz, float r3d, float cf, float exyLF, float f1, float coh, Preferences& prefs) {
        if (calState_ == CAL_SETTLING_S0) {
            calCollected_++;
            if (calCollected_ >= VG_CAL_SETTLE_WINDOWS) {
                calState_ = CAL_CAPTURING_S0;
                calCollected_ = 0;
                Serial.println("[CAL] Settling complete. Capturing 50 baseline windows...");
            }
            return;
        }

        if (calState_ == CAL_CAPTURING_S0) {
            // Reject transient shock table bumps during calibration capture
            if (cf > 5.0f) {
                Serial.printf("[CAL] Window rejected: crest_factor=%.2f > 5.0 (transient bump)\n", cf);
                return;
            }

            calRx_[calCollected_]    = rx;
            calRy_[calCollected_]    = ry;
            calRz_[calCollected_]    = rz;
            calR3D_[calCollected_]   = r3d;
            calCF_[calCollected_]    = cf;
            calExyLF_[calCollected_] = exyLF;
            calF1_[calCollected_]    = f1;
            calCollected_++;

            if ((calCollected_ % 10) == 0 || calCollected_ == VG_CAL_CAPTURE_WINDOWS) {
                Serial.printf("[CAL] Progress: %u/%d windows captured (f1=%.1fHz, cf=%.2f)\n",
                              static_cast<unsigned int>(calCollected_), VG_CAL_CAPTURE_WINDOWS, f1, cf);
            }

            if (calCollected_ >= VG_CAL_CAPTURE_WINDOWS) {
                calState_ = CAL_COMPUTING;
            }
        }

        if (calState_ == CAL_COMPUTING) {
            // Preserve raw copies before in-place median sorting
            float ryCopy[VG_CAL_CAPTURE_WINDOWS], r3dCopy[VG_CAL_CAPTURE_WINDOWS];
            memcpy(ryCopy, calRy_, sizeof(calRy_));
            memcpy(r3dCopy, calR3D_, sizeof(calR3D_));

            cal_.rx0     = computeArrayMedian(calRx_, VG_CAL_CAPTURE_WINDOWS);
            cal_.ry0     = computeArrayMedian(calRy_, VG_CAL_CAPTURE_WINDOWS);
            cal_.rz0     = computeArrayMedian(calRz_, VG_CAL_CAPTURE_WINDOWS);
            cal_.cf0     = computeArrayMedian(calCF_, VG_CAL_CAPTURE_WINDOWS);
            cal_.exy_lf0 = computeArrayMedian(calExyLF_, VG_CAL_CAPTURE_WINDOWS);
            cal_.f1_0    = computeArrayMedian(calF1_, VG_CAL_CAPTURE_WINDOWS);
            if (cal_.exy_lf0 < 1e-4f) cal_.exy_lf0 = 1e-4f;

            // Compute baseline J[m] series and MAD dispersion
            for (size_t m = 0; m < VG_CAL_CAPTURE_WINDOWS; ++m) {
                calJ0_[m] = computeConditionMetric(ryCopy[m], r3dCopy[m], 1.0f);
            }
            const float medJ0 = computeArrayMedian(calJ0_, VG_CAL_CAPTURE_WINDOWS);
            float sigmaJ0 = computeMADSigma(calJ0_, VG_CAL_CAPTURE_WINDOWS, medJ0);
            if (sigmaJ0 < 0.045f) sigmaJ0 = 0.045f;
            cal_.sigma_j0 = sigmaJ0;

            // Substrate-Invariant Rotordynamic Threshold Ladder
            cal_.t01_enter = 0.80f;
            cal_.t10_exit  = 0.40f;
            cal_.t12_enter = 2.10f;
            cal_.t21_exit  = 1.70f;
            cal_.t23_enter = 6.00f;
            cal_.t32_exit  = 5.00f;

            saveToNVS(prefs);
            Serial.printf("[CAL] COMPLETE! Baseline: rx0=%.5f ry0=%.5f rz0=%.5f cf0=%.3f exyLF0=%.5f f1=%.2fHz sigmaJ0=%.4f\n",
                          cal_.rx0, cal_.ry0, cal_.rz0, cal_.cf0, cal_.exy_lf0, cal_.f1_0, cal_.sigma_j0);
            Serial.printf("[CAL] Threshold Ladder: T01=%.3f T12=%.3f T23=%.3f\n",
                          cal_.t01_enter, cal_.t12_enter, cal_.t23_enter);
            resetStateEngine();
            calState_ = CAL_IDLE;
        }
    }

    void updateHysteresisState(float jVal) {
        static constexpr uint8_t PERSIST_ENTER = 5; // 800 ms escalation persistence
        static constexpr uint8_t PERSIST_EXIT  = 5; // 800 ms de-escalation persistence

        VibeStage targetUp   = currentStage_;
        VibeStage targetDown = currentStage_;

        // Escalation Threshold Evaluation
        if (currentStage_ < STAGE_3_CRITICAL && jVal >= cal_.t23_enter) {
            targetUp = STAGE_3_CRITICAL;
        } else if (currentStage_ < STAGE_2_MODERATE && jVal >= cal_.t12_enter) {
            targetUp = STAGE_2_MODERATE;
        } else if (currentStage_ < STAGE_1_WARNING && jVal >= cal_.t01_enter) {
            targetUp = STAGE_1_WARNING;
        }

        // De-Escalation Threshold Evaluation
        if (currentStage_ > STAGE_0_NORMAL && jVal <= cal_.t10_exit) {
            targetDown = STAGE_0_NORMAL;
        } else if (currentStage_ > STAGE_1_WARNING && jVal <= cal_.t21_exit) {
            targetDown = STAGE_1_WARNING;
        } else if (currentStage_ > STAGE_2_MODERATE && jVal <= cal_.t32_exit) {
            targetDown = STAGE_2_MODERATE;
        }

        if (targetUp > currentStage_) {
            countUp_++;
            countDown_ = 0;
            if (countUp_ >= PERSIST_ENTER) {
                currentStage_ = targetUp;
                countUp_ = 0;
            }
        } else if (targetDown < currentStage_) {
            countDown_++;
            countUp_ = 0;
            if (countDown_ >= PERSIST_EXIT) {
                currentStage_ = targetDown;
                countDown_ = 0;
            }
        } else {
            if (countUp_ > 0)   countUp_--;
            if (countDown_ > 0) countDown_--;
        }
    }
};
