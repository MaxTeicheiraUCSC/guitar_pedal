// pedal/tremolo.h — amplitude tremolo with LFO shapes, stereo phase offset
// and a harmonic mode (crossover + anti-phase LFOs, the Fender "harmonic vibrato").
#pragma once
#include "effect.h"

namespace pedal {

class Tremolo final : public Effect {
public:
    enum P { Rate, Depth, Shape, Harmonic, StereoPhase, Crossover, Volume, NumP };

    const char* name() const override { return "Tremolo"; }
    int numParams() const override { return NumP; }
    const ParamDesc& paramDesc(int i) const override {
        static const ParamDesc d[NumP] = {
            { "Rate",        0.1f,  20.f,  4.5f, Curve::Log,    "Hz",  0 },
            { "Depth",       0.f,   1.f,   0.6f, Curve::Linear, "",    0 },
            { "Shape",       0.f,   4.f,   0.f,  Curve::Steps,  "",    static_cast<uint8_t>(Lfo::Count) },
            { "Harmonic",    0.f,   1.f,   0.f,  Curve::Switch, "",    2 },
            { "StereoPhase", 0.f,   180.f, 0.f,  Curve::Linear, "deg", 0 },
            { "Crossover",   200.f, 2000.f,800.f,Curve::Log,    "Hz",  0 },
            { "Volume",      -12.f, 12.f,  0.f,  Curve::Linear, "dB",  0 },
        };
        return d[i];
    }

    bool init(const Config& cfg) override {
        fs_ = cfg.sampleRate;
        lfoL_.init(fs_); lfoR_.init(fs_);
        depth_.init(fs_, 20.f); vol_.init(fs_, 20.f);
        loadDefaults();
        depth_.snap(depth_.target()); vol_.snap(vol_.target());
        reset();
        return true;
    }
    void reset() override { for (auto* b : { &lpL_, &lpR_, &hpL_, &hpR_ }) b->reset(); }

    void setTempo(float spb) override { if (syncToTempo_) setRate(1.f / spb); }

    void process(Frame* buf, int n) override {
        if (!enabled_) return;
        for (int i = 0; i < n; ++i) {
            const float d = depth_.next();
            const float g = vol_.next();
            const float ml = lfoL_.next();
            const float mr = lfoR_.next();
            Frame f = buf[i];
            if (harmonic_) {
                // Linkwitz-Riley 2nd-order split (two cascaded Butterworth-ish 1st order pairs is
                // cheaper; here: LP + (x - LP) keeps it perfectly complementary)
                const float loL = lpL_.process(f.l), hiL = f.l - loL;
                const float loR = lpR_.process(f.r), hiR = f.r - loR;
                const float aL = 1.f - d * ml, bL = 1.f - d * (1.f - ml);
                const float aR = 1.f - d * mr, bR = 1.f - d * (1.f - mr);
                f.l = (loL * aL + hiL * bL) * g;
                f.r = (loR * aR + hiR * bR) * g;
            } else {
                f.l *= (1.f - d * ml) * g;
                f.r *= (1.f - d * mr) * g;
            }
            buf[i] = f;
        }
    }

    void setSyncToTempo(bool s) { syncToTempo_ = s; }

protected:
    void onParam(int i, float v) override {
        switch (i) {
            case Rate:        setRate(v); break;
            case Depth:       depth_.set(v); break;
            case Shape:       lfoL_.setShape(static_cast<Lfo::Shape>(static_cast<int>(v)));
                              lfoR_.setShape(static_cast<Lfo::Shape>(static_cast<int>(v))); break;
            case Harmonic:    harmonic_ = v >= 0.5f; break;
            case StereoPhase: phaseOffset_ = v / 360.f; lfoR_.setPhase(lfoL_.phase() + phaseOffset_); break;
            case Crossover:   lpL_.set(Biquad::LowPass, fs_, v, 0.5f); lpR_.set(Biquad::LowPass, fs_, v, 0.5f); break;
            case Volume:      vol_.set(dbToLin(v)); break;
        }
    }
private:
    void setRate(float hz) { lfoL_.setRate(hz); lfoR_.setRate(hz); }
    float fs_ = 48000.f;
    Lfo lfoL_, lfoR_;
    Smoother depth_, vol_;
    Biquad lpL_, lpR_, hpL_, hpR_;
    bool harmonic_ = false, syncToTempo_ = false;
    float phaseOffset_ = 0.f;
};

} // namespace pedal
