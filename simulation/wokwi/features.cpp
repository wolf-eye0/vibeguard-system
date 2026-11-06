#include "features.h"
#include <cmath>
#include <stdexcept>
#include <algorithm>

// Fast in-place DC removal for static buffers
void remove_dc(float* data, size_t n) {
    if (!data || n == 0) return;
    double sum = 0.0;
    for (size_t i = 0; i < n; ++i) {
        sum += static_cast<double>(data[i]);
    }
    float mean = static_cast<float>(sum / static_cast<double>(n));
    for (size_t i = 0; i < n; ++i) {
        data[i] -= mean;
    }
}

// 1D RMS calculation
float compute_rms(const float* data, size_t n) {
    if (!data || n == 0) return 0.0f;
    double sum_sq = 0.0;
    for (size_t i = 0; i < n; ++i) {
        double val = static_cast<double>(data[i]);
        sum_sq += val * val;
    }
    return static_cast<float>(std::sqrt(sum_sq / static_cast<double>(n)));
}

// 3-Axis Euclidean vector RMS
float compute_vector_rms(const float* x, const float* y, const float* z, size_t n) {
    if (!x || !y || !z || n == 0) return 0.0f;
    double sum_sq = 0.0;
    for (size_t i = 0; i < n; ++i) {
        double dx = static_cast<double>(x[i]);
        double dy = static_cast<double>(y[i]);
        double dz = static_cast<double>(z[i]);
        sum_sq += (dx * dx + dy * dy + dz * dz);
    }
    return static_cast<float>(std::sqrt(sum_sq / static_cast<double>(n)));
}

// Hann Window: w(i) = 0.5 * (1 - cos(2*pi*i / (N-1)))
void apply_hann_window(float* data, size_t n) {
    if (!data || n <= 1) return;
    const float two_pi = 6.283185307179586f;
    const float denom = static_cast<float>(n - 1);
    for (size_t i = 0; i < n; ++i) {
        float factor = 0.5f * (1.0f - std::cos(two_pi * static_cast<float>(i) / denom));
        data[i] *= factor;
    }
}

// Simple in-place Cooley-Tukey Radix-2 FFT
static void fft_radix2(float* real, float* imag, size_t n) {
    // Bit-reversal permutation
    size_t j = 0;
    for (size_t i = 0; i < n - 1; ++i) {
        if (i < j) {
            std::swap(real[i], real[j]);
            std::swap(imag[i], imag[j]);
        }
        size_t k = n >> 1;
        while (k <= j) {
            j -= k;
            k >>= 1;
        }
        j += k;
    }

    // Cooley-Tukey computation
    const float pi = 3.141592653589793f;
    for (size_t len = 2; len <= n; len <<= 1) {
        float angle = -2.0f * pi / static_cast<float>(len);
        float wlen_r = std::cos(angle);
        float wlen_i = std::sin(angle);
        for (size_t i = 0; i < n; i += len) {
            float w_r = 1.0f;
            float w_i = 0.0f;
            for (size_t k = 0; k < len / 2; ++k) {
                float u_r = real[i + k];
                float u_i = imag[i + k];
                float v_r = real[i + k + len / 2] * w_r - imag[i + k + len / 2] * w_i;
                float v_i = real[i + k + len / 2] * w_i + imag[i + k + len / 2] * w_r;
                real[i + k] = u_r + v_r;
                imag[i + k] = u_i + v_i;
                real[i + k + len / 2] = u_r - v_r;
                imag[i + k + len / 2] = u_i - v_i;
                float next_w_r = w_r * wlen_r - w_i * wlen_i;
                float next_w_i = w_r * wlen_i + w_i * wlen_r;
                w_r = next_w_r;
                w_i = next_w_i;
            }
        }
    }
}

void compute_fft_peak(const float* time_data, size_t n, float sampling_rate,
                      float& out_peak_freq, float& out_peak_mag) {
    out_peak_freq = 0.0f;
    out_peak_mag = 0.0f;
    if (!time_data || n < 4 || (n & (n - 1)) != 0) return; // Must be power of 2

    // Allocate working buffers (using local dynamic or stack array)
    std::vector<float> r(time_data, time_data + n);
    std::vector<float> im(n, 0.0f);

    apply_hann_window(r.data(), n);
    fft_radix2(r.data(), im.data(), n);

    // Find dominant frequency bin (skip DC bin 0)
    size_t half = n / 2;
    float max_mag = 0.0f;
    size_t max_bin = 1;
    for (size_t i = 1; i < half; ++i) {
        float mag = std::sqrt(r[i] * r[i] + im[i] * im[i]) * (2.0f / static_cast<float>(n));
        if (mag > max_mag) {
            max_mag = mag;
            max_bin = i;
        }
    }

    out_peak_freq = static_cast<float>(max_bin) * (sampling_rate / static_cast<float>(n));
    out_peak_mag = max_mag;
}

// ==========================================
// std::vector Wrappers for Host Compatibility
// ==========================================
void remove_dc(std::vector<float>& data) {
    remove_dc(data.data(), data.size());
}

float compute_rms(const std::vector<float>& data) {
    if (data.empty()) throw std::invalid_argument("compute_rms: data is empty");
    return compute_rms(data.data(), data.size());
}

float compute_vector_rms(const std::vector<float>& x,
                         const std::vector<float>& y,
                         const std::vector<float>& z) {
    if (x.size() != y.size() || x.size() != z.size()) {
        throw std::invalid_argument("compute_vector_rms: axis sizes differ");
    }
    if (x.empty()) throw std::invalid_argument("compute_vector_rms: data is empty");
    return compute_vector_rms(x.data(), y.data(), z.data(), x.size());
}