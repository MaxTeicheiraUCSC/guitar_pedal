// pedal/oversampler.h — polyphase FIR up/down-sampler for 1x/2x/4x, mono.
// Kaiser-windowed sinc prototype, computed at init (no tables in flash).
#pragma once
#include "core.h"

namespace pedal {

class Oversampler {
public:
    static constexpr int kMaxFactor = 8;
    static constexpr int kTapsPerPhase = 32;                 // 128 taps at 4x, 256 at 8x (8x is only used by the emulator's analog model)
    static constexpr int kMaxTaps = kTapsPerPhase * kMaxFactor;

    void init(int factor) {
        factor_ = factor < 1 ? 1 : (factor > kMaxFactor ? kMaxFactor : factor);
        taps_ = kTapsPerPhase * factor_;
        design();
        clear();
    }
    int factor() const { return factor_; }
    void clear() { for (float& v : histUp_) v = 0.f; for (float& v : histDown_) v = 0.f; hu_ = hd_ = 0; }

    // Produces factor() samples into out from one input sample.
    inline void up(float x, float* out) {
        if (factor_ == 1) { out[0] = x; return; }
        histUp_[hu_] = x;
        for (int p = 0; p < factor_; ++p) {
            float acc = 0.f;
            int idx = hu_;
            for (int k = p; k < taps_; k += factor_) {
                acc += h_[k] * histUp_[idx];
                idx = (idx - 1) & (kTapsPerPhase - 1);
            }
            out[p] = acc * factor_;
        }
        hu_ = (hu_ + 1) & (kTapsPerPhase - 1);
    }
    // Consumes factor() samples, returns one.
    inline float down(const float* in) {
        if (factor_ == 1) return in[0];
        for (int p = 0; p < factor_; ++p) { histDown_[hd_] = in[p]; hd_ = (hd_ + 1) & (kMaxTaps - 1); }
        float acc = 0.f;
        int idx = (hd_ - 1) & (kMaxTaps - 1);
        for (int k = 0; k < taps_; ++k) { acc += h_[k] * histDown_[idx]; idx = (idx - 1) & (kMaxTaps - 1); }
        return acc;
    }
    // Total latency of a matched up()/down() pair, in input samples. Both FIRs are
    // centred at (taps-1)/2 and down() emits after consuming L samples, which lands
    // the impulse exactly on an integer: (taps - L) / L = kTapsPerPhase - 1.
    int latency() const { return factor_ == 1 ? 0 : kTapsPerPhase - 1; }

private:
    static float besselI0(float x) {
        float sum = 1.f, term = 1.f; const float q = x * x / 4.f;
        for (int k = 1; k < 30; ++k) { term *= q / (k * k); sum += term; if (term < 1e-9f * sum) break; }
        return sum;
    }
    void design() {
        const float beta = 8.f;
        const float fc = 0.5f / factor_;               // normalized to oversampled rate (cycles/sample)
        const float M = static_cast<float>(taps_ - 1);
        float sum = 0.f;
        for (int n = 0; n < taps_; ++n) {
            const float t = n - M / 2.f;
            const float sinc = (std::fabs(t) < 1e-6f) ? 2.f * fc : std::sin(kTwoPi * fc * t) / (kPi * t);
            const float r = 2.f * n / M - 1.f;
            const float w = besselI0(beta * std::sqrt(1.f - r * r)) / besselI0(beta);
            h_[n] = sinc * w; sum += h_[n];
        }
        for (int n = 0; n < taps_; ++n) h_[n] /= sum;   // unity DC gain
    }

    int factor_ = 1, taps_ = kTapsPerPhase;
    float h_[kMaxTaps] = {};
    float histUp_[kTapsPerPhase] = {};   // ring, kTapsPerPhase is a power of two
    float histDown_[kMaxTaps] = {};
    int hu_ = 0, hd_ = 0;
};

} // namespace pedal
