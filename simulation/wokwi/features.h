#ifndef FEATURES_H
#define FEATURES_H

#include <vector>
#include <cstddef>

// ==========================================
// Static Buffer Functions (Embedded Real-Time)
// ==========================================

// Remove DC bias / 1g gravity component in-place
void remove_dc(float* data, size_t n);

// Compute 1D Root Mean Square
float compute_rms(const float* data, size_t n);

// Compute 3-Axis Euclidean Vector RMS: RMS(sqrt(x^2 + y^2 + z^2))
float compute_vector_rms(const float* x, const float* y, const float* z, size_t n);

// Apply Hann Window to minimize spectral leakage before FFT
void apply_hann_window(float* data, size_t n);

// Lightweight In-Place Radix-2 Real FFT
// Computes dominant peak frequency (Hz) and peak magnitude (g)
void compute_fft_peak(const float* time_data, size_t n, float sampling_rate,
                      float& out_peak_freq, float& out_peak_mag);

// ==========================================
// std::vector Wrappers (Host / Unit-Test API)
// ==========================================
void remove_dc(std::vector<float>& data);
float compute_rms(const std::vector<float>& data);
float compute_vector_rms(const std::vector<float>& x,
                         const std::vector<float>& y,
                         const std::vector<float>& z);

#endif // FEATURES_H