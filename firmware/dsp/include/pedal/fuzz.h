// pedal/fuzz.h — digital fuzz: input low-cut, gain, bias (asymmetry), oversampled
// waveshaper with first-order antiderivative anti-aliasing (ADAA), tilt tone,
// noise gate and volume. Three clipper laws: Soft (tanh), Hard, Asym (Fuzz-Face-like).
#pragma once
#include "effect.h"
#include "oversampler.h"

namespace pedal {

class Fuzz final : public Effect {
public:
    enum P { Gain, Bias, Tone, Volume, Mode, Oversample, Gate, LowCut, NumP };
    enum ModeId { Soft, Hard, Asym, ModeCount };

    const char* name() const override { return "Fuzz"; }
    int numParams() const override { return NumP; }
    const ParamDesc& paramDesc(int i) const override {
        static const ParamDesc d[NumP] = {
            { "Gain",       0.f,   60.f,  30.f,  Curve::Linear, "dB", 0 },
            { "Bias",       -1.f,  1.f,   0.f,   Curve::Linear, "",   0 },
            { "Tone",       -1.f,  1.f,   0.f,   Curve::Linear, "",   0 },
            { "Volume",     -30.f, 6.f,   -12.f, Curve::Linear, "dB", 0 },
            { "Mode",       0.f,   2.f,   0.f,   Curve::Steps,  "",   ModeCount },
            { "Oversample", 0.f,   2.f,   2.f,   Curve::Steps,  "",   3 },   // 0:1x 1:2x 2:4x
            { "Gate",       -90.f, -20.f, -70.f, Curve::Linear, "dB", 0 },
            { "LowCut",     20.f,  500.f, 60.f,  Curve::Log,    "Hz", 0 },
        };
        return d[i];
    }

    bool init(const Config& cfg) override {
        fs_ = cfg.sampleRate;
        gain_.init(fs_, 10.f); bias_.init(fs_, 10.f); vol_.init(fs_, 10.f); gateG_.init(fs_, 5.f);
        env_.init(fs_, 30.f);
        loadDefaults();
        gain_.snap(gain_.target()); bias_.snap(bias_.target()); vol_.snap(vol_.target()); gateG_.snap(1.f);
        reset();
        return true;
    }
    void reset() override {
        for (int c = 0; c < 2; ++c) { hpIn_[c].reset(); dc_[c].reset(); tiltLo_[c].reset(); tiltHi_[c].reset(); osUp_[c].clear(); osDown_[c].clear(); x1_[c] = 0.f; }
        env_.snap(0.f);
    }

    void process(Frame* buf, int n) override {
        if (!enabled_) return;
        float tmp[Oversampler::kMaxFactor];   // fuzz uses at most 4x
        for (int i = 0; i < n; ++i) {
            const float g = gain_.next(), b = bias_.next(), v = vol_.next();
            // gate from the clean input envelope
            const float lvl = 0.5f * (std::fabs(buf[i].l) + std::fabs(buf[i].r));
            env_.set(lvl); const float e = env_.next();
            gateG_.set(e > gateThr_ ? 1.f : (e / gateThr_) * (e / gateThr_));
            const float gg = gateG_.next();

            float* ch[2] = { &buf[i].l, &buf[i].r };
            for (int c = 0; c < 2; ++c) {
                float x = hpIn_[c].process(*ch[c]) * g + b;
                osUp_[c].up(x, tmp);
                const int L = osUp_[c].factor();
                for (int k = 0; k < L; ++k) tmp[k] = adaa(c, tmp[k]);
                float y = osDown_[c].down(tmp);
                y = dc_[c].process(y);                        // remove bias DC
                y = tiltHi_[c].process(tiltLo_[c].process(y));
                *ch[c] = y * v * gg;
            }
        }
    }

protected:
    void onParam(int i, float v) override {
        switch (i) {
            case Gain:       gain_.set(dbToLin(v)); break;
            case Bias:       bias_.set(v * 0.8f); break;
            case Tone:       for (int c = 0; c < 2; ++c) { tiltLo_[c].set(Biquad::LowShelf, fs_, 800.f, 0.7f, -6.f * v); tiltHi_[c].set(Biquad::HighShelf, fs_, 800.f, 0.7f, 6.f * v); } break;
            case Volume:     vol_.set(dbToLin(v)); break;
            case Mode:       mode_ = static_cast<int>(v); break;
            case Oversample: { const int f = 1 << static_cast<int>(v); for (int c = 0; c < 2; ++c) { osUp_[c].init(f); osDown_[c].init(f); } break; }
            case Gate:       gateThr_ = dbToLin(v); break;
            case LowCut:     for (int c = 0; c < 2; ++c) hpIn_[c].set(Biquad::HighPass, fs_, v); for (int c = 0; c < 2; ++c) dc_[c].setOnePoleHP(fs_, 10.f); break;
        }
    }
private:
    // --- nonlinearities and their antiderivatives ---
    static inline float logcosh(float x) {
        const float a = std::fabs(x);
        return a > 15.f ? a - 0.6931472f : std::log(std::cosh(a));
    }
    inline float f(float x) const {
        switch (mode_) {
            case Hard: return clampf(x, -1.f, 1.f);
            case Asym: return x >= 0.f ? std::tanh(x) : 0.6f * std::tanh(x / 0.6f);
            default:   return std::tanh(x);
        }
    }
    inline float F(float x) const {
        switch (mode_) {
            case Hard: { const float a = std::fabs(x); return a <= 1.f ? 0.5f * x * x : a - 0.5f; }
            case Asym: return x >= 0.f ? logcosh(x) : 0.36f * logcosh(x / 0.6f);
            default:   return logcosh(x);
        }
    }
    inline float adaa(int c, float x) {
        const float x1 = x1_[c];
        const float d = x - x1;
        float y;
        if (std::fabs(d) < 1e-4f) y = f(0.5f * (x + x1));
        else                      y = (F(x) - F(x1)) / d;
        x1_[c] = x;
        return y;
    }

    float fs_ = 48000.f, gateThr_ = 0.0003f;
    int mode_ = Soft;
    Smoother gain_, bias_, vol_, gateG_, env_;
    Biquad hpIn_[2], dc_[2], tiltLo_[2], tiltHi_[2];
    Oversampler osUp_[2], osDown_[2];
    float x1_[2] = {};
};

} // namespace pedal
