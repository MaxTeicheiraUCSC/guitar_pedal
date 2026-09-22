// pedal/core.h — shared primitives for the portable DSP core.
// No heap, no exceptions, no RTTI, no iostream. Everything that needs a large
// buffer asks the Allocator so the Daisy wrapper can hand out SDRAM and the
// host wrapper can hand out heap.
#pragma once
#include <cstddef>
#include <cstdint>
#include <cmath>
#include <initializer_list>

namespace pedal {

struct Frame {
    float l = 0.f;
    float r = 0.f;
};

constexpr int   kMaxBlock   = 64;      // codec block size upper bound
constexpr int   kNumEffects = 4;
constexpr int   kMaxParams  = 8;       // per effect (maps onto 8 MIDI CCs)
constexpr float kPi         = 3.14159265358979323846f;
constexpr float kTwoPi      = 2.f * kPi;

// Arena-style allocator: the core never frees during operation.
class Allocator {
public:
    virtual ~Allocator() = default;
    virtual void* alloc(size_t bytes, size_t align = 16) = 0;

    template <typename T>
    T* allocArray(size_t count) {
        void* p = alloc(sizeof(T) * count, alignof(T) > 16 ? alignof(T) : 16);
        if (!p) return nullptr;
        T* t = static_cast<T*>(p);
        for (size_t i = 0; i < count; ++i) t[i] = T{};
        return t;
    }
};

struct Config {
    float      sampleRate = 48000.f;
    int        blockSize  = 48;
    Allocator* mem        = nullptr;
};

// ---- parameter descriptors ------------------------------------------------
enum class Curve : uint8_t { Linear, Log, Switch, Steps };

struct ParamDesc {
    const char* name;
    float       min;
    float       max;
    float       def;      // in real units
    Curve       curve;
    const char* unit;
    uint8_t     steps;    // for Curve::Steps / Switch

    // normalized (0..1) <-> real units
    float toReal(float n) const {
        if (n < 0.f) n = 0.f; else if (n > 1.f) n = 1.f;
        switch (curve) {
            case Curve::Log:    return min * std::pow(max / min, n);
            case Curve::Switch: return n >= 0.5f ? 1.f : 0.f;
            case Curve::Steps:  return std::floor(n * (steps - 0.001f));
            default:            return min + (max - min) * n;
        }
    }
    float toNorm(float v) const {
        switch (curve) {
            case Curve::Log:    return std::log(v / min) / std::log(max / min);
            case Curve::Switch: return v >= 0.5f ? 1.f : 0.f;
            case Curve::Steps:  return (v + 0.5f) / steps;
            default:            return (v - min) / (max - min);
        }
    }
};

// ---- one-pole smoother for control signals --------------------------------
class Smoother {
public:
    void init(float sampleRate, float timeMs) {
        a_ = std::exp(-1.f / (0.001f * timeMs * sampleRate));
    }
    void set(float target) { target_ = target; }
    void snap(float v)     { target_ = v; y_ = v; }
    float next()           { y_ = target_ + a_ * (y_ - target_); return y_; }
    float value() const    { return y_; }
    float target() const   { return target_; }
private:
    float a_ = 0.f, y_ = 0.f, target_ = 0.f;
};

// ---- RBJ biquad -------------------------------------------------------------
class Biquad {
public:
    enum Type { LowPass, HighPass, LowShelf, HighShelf, Peak, AllPass };

    void reset() { z1_ = z2_ = 0.f; }
    void bypass() { b0_ = 1.f; b1_ = b2_ = a1_ = a2_ = 0.f; }

    void set(Type type, float fs, float fc, float q = 0.7071068f, float gainDb = 0.f) {
        if (fc > fs * 0.49f) fc = fs * 0.49f;
        if (fc < 1.f) fc = 1.f;
        const float w0 = kTwoPi * fc / fs;
        const float cw = std::cos(w0), sw = std::sin(w0);
        const float alpha = sw / (2.f * q);
        const float A = std::pow(10.f, gainDb / 40.f);
        float b0, b1, b2, a0, a1, a2;
        switch (type) {
            case LowPass:
                b0 = (1 - cw) / 2; b1 = 1 - cw; b2 = (1 - cw) / 2;
                a0 = 1 + alpha; a1 = -2 * cw; a2 = 1 - alpha; break;
            case HighPass:
                b0 = (1 + cw) / 2; b1 = -(1 + cw); b2 = (1 + cw) / 2;
                a0 = 1 + alpha; a1 = -2 * cw; a2 = 1 - alpha; break;
            case AllPass:
                b0 = 1 - alpha; b1 = -2 * cw; b2 = 1 + alpha;
                a0 = 1 + alpha; a1 = -2 * cw; a2 = 1 - alpha; break;
            case Peak:
                b0 = 1 + alpha * A; b1 = -2 * cw; b2 = 1 - alpha * A;
                a0 = 1 + alpha / A; a1 = -2 * cw; a2 = 1 - alpha / A; break;
            case LowShelf: {
                const float sa = 2 * std::sqrt(A) * alpha;
                b0 = A * ((A + 1) - (A - 1) * cw + sa);
                b1 = 2 * A * ((A - 1) - (A + 1) * cw);
                b2 = A * ((A + 1) - (A - 1) * cw - sa);
                a0 = (A + 1) + (A - 1) * cw + sa;
                a1 = -2 * ((A - 1) + (A + 1) * cw);
                a2 = (A + 1) + (A - 1) * cw - sa; break; }
            default: { // HighShelf
                const float sa = 2 * std::sqrt(A) * alpha;
                b0 = A * ((A + 1) + (A - 1) * cw + sa);
                b1 = -2 * A * ((A - 1) + (A + 1) * cw);
                b2 = A * ((A + 1) + (A - 1) * cw - sa);
                a0 = (A + 1) - (A - 1) * cw + sa;
                a1 = 2 * ((A - 1) - (A + 1) * cw);
                a2 = (A + 1) - (A - 1) * cw - sa; break; }
        }
        b0_ = b0 / a0; b1_ = b1 / a0; b2_ = b2 / a0; a1_ = a1 / a0; a2_ = a2 / a0;
    }

    // first-order sections expressed as biquads (b2 = a2 = 0)
    void setOnePoleLP(float fs, float fc) {
        if (fc > 0.45f * fs) fc = 0.45f * fs;
        const float k = std::tan(kPi * fc / fs);
        const float n = 1.f / (1.f + k);
        b0_ = k * n; b1_ = k * n; b2_ = 0.f; a1_ = (k - 1.f) * n; a2_ = 0.f;
    }
    void setOnePoleHP(float fs, float fc) {
        if (fc > 0.45f * fs) fc = 0.45f * fs;
        const float k = std::tan(kPi * fc / fs);
        const float n = 1.f / (1.f + k);
        b0_ = n; b1_ = -n; b2_ = 0.f; a1_ = (k - 1.f) * n; a2_ = 0.f;
    }

    inline float process(float x) {   // transposed direct form II
        const float y = b0_ * x + z1_;
        z1_ = b1_ * x - a1_ * y + z2_;
        z2_ = b2_ * x - a2_ * y;
        return y;
    }
private:
    float b0_ = 1.f, b1_ = 0.f, b2_ = 0.f, a1_ = 0.f, a2_ = 0.f;
    float z1_ = 0.f, z2_ = 0.f;
};

// Stereo pair of biquads sharing coefficients
struct StereoBiquad {
    Biquad l, r;
    void set(Biquad::Type t, float fs, float fc, float q = 0.7071068f, float g = 0.f) { l.set(t, fs, fc, q, g); r.set(t, fs, fc, q, g); }
    void bypass() { l.bypass(); r.bypass(); }
    void reset()  { l.reset();  r.reset();  }
    inline Frame process(Frame f) { return { l.process(f.l), r.process(f.r) }; }
};

// ---- LFO --------------------------------------------------------------------
class Lfo {
public:
    enum Shape { Sine, Triangle, Square, Opto, Saw, Count };
    void init(float fs) { fs_ = fs; }
    void setRate(float hz) { inc_ = hz / fs_; }
    void setPhase(float p) { ph_ = p - std::floor(p); }
    void setShape(Shape s) { shape_ = s; }
    float phase() const { return ph_; }
    // returns 0..1 (unipolar)
    inline float next() {
        const float p = ph_;
        ph_ += inc_; if (ph_ >= 1.f) ph_ -= 1.f;
        return shapeAt(p, shape_);
    }
    static float shapeAt(float p, Shape s) {
        switch (s) {
            case Triangle: return p < 0.5f ? 2.f * p : 2.f - 2.f * p;
            case Square:   return p < 0.5f ? 1.f : 0.f;
            case Saw:      return 1.f - p;
            case Opto: {   // lamp/LDR: fast attack, slow release -> rounded, asymmetric
                const float s = 0.5f - 0.5f * std::cos(kTwoPi * p);
                return std::pow(s, 1.8f);
            }
            default:       return 0.5f - 0.5f * std::cos(kTwoPi * p);
        }
    }
private:
    float fs_ = 48000.f, inc_ = 0.f, ph_ = 0.f;
    Shape shape_ = Sine;
};

// ---- Fractional delay line (mono) ------------------------------------------
class DelayLine {
public:
    bool init(Allocator& mem, int maxSamples) {
        // power of two for cheap wrap
        size_ = 1; while (size_ < maxSamples + 4) size_ <<= 1;
        buf_ = mem.allocArray<float>(size_);
        mask_ = size_ - 1; w_ = 0;
        return buf_ != nullptr;
    }
    int capacity() const { return size_ - 4; }
    void clear() { for (int i = 0; i < size_; ++i) buf_[i] = 0.f; }
    inline void write(float x) { buf_[w_] = x; w_ = (w_ + 1) & mask_; }
    inline float readInt(int d) const { return buf_[(w_ - d) & mask_]; }
    // 4-point Hermite interpolation
    inline float read(float d) const {
        if (d < 1.f) d = 1.f;
        const int   di = static_cast<int>(d);
        const float fr = d - di;
        const int   i0 = (w_ - di) & mask_;
        const float xm1 = buf_[(i0 + 1) & mask_];
        const float x0  = buf_[i0];
        const float x1  = buf_[(i0 - 1) & mask_];
        const float x2  = buf_[(i0 - 2) & mask_];
        const float c0 = x0;
        const float c1 = 0.5f * (x1 - xm1);
        const float c2 = xm1 - 2.5f * x0 + 2.f * x1 - 0.5f * x2;
        const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
        return ((c3 * fr + c2) * fr + c1) * fr + c0;
    }
    // allpass-style read/write helpers for reverb
    inline float* raw() { return buf_; }
private:
    float* buf_ = nullptr;
    int size_ = 0, mask_ = 0, w_ = 0;
};

// ---- helpers ----------------------------------------------------------------
inline float dbToLin(float db) { return std::pow(10.f, db / 20.f); }
inline float linToDb(float x)  { return 20.f * std::log10(x > 1e-12f ? x : 1e-12f); }
inline float clampf(float x, float lo, float hi) { return x < lo ? lo : (x > hi ? hi : x); }
inline float softClip(float x) { return std::tanh(x); }
inline float flushDenormal(float x) { return std::fabs(x) < 1e-20f ? 0.f : x; }

} // namespace pedal
