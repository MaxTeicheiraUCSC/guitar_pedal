// pedal/reverb.h — Dattorro plate reverb (J. Dattorro, "Effect Design Part 1:
// Reverberator and Other Filters", JAES 1997), stereo in via input sum,
// delays scaled from the paper's 29761 Hz to the running sample rate.
#pragma once
#include "effect.h"

namespace pedal {

class Reverb final : public Effect {
public:
    enum P { Mix, Decay, PreDelay, HighCut, LowCut, Mod, Diffusion, Width, NumP };

    const char* name() const override { return "Reverb"; }
    int numParams() const override { return NumP; }
    const ParamDesc& paramDesc(int i) const override {
        static const ParamDesc d[NumP] = {
            { "Mix",       0.f,   1.f,    0.3f,  Curve::Linear, "",   0 },
            { "Decay",     0.f,   0.98f,  0.6f,  Curve::Linear, "",   0 },
            { "PreDelay",  0.f,   250.f,  10.f,  Curve::Linear, "ms", 0 },
            { "HighCut",   500.f, 20000.f,6000.f,Curve::Log,    "Hz", 0 },
            { "LowCut",    20.f,  1000.f, 80.f,  Curve::Log,    "Hz", 0 },
            { "Mod",       0.f,   1.f,    0.3f,  Curve::Linear, "",   0 },
            { "Diffusion", 0.f,   1.f,    0.7f,  Curve::Linear, "",   0 },
            { "Width",     0.f,   1.f,    1.f,   Curve::Linear, "",   0 },
        };
        return d[i];
    }

    bool init(const Config& cfg) override {
        fs_ = cfg.sampleRate;
        if (!cfg.mem) return false;
        k_ = fs_ / 29761.f;
        auto L = [&](float n) { return static_cast<int>(n * k_ + 0.5f); };
        bool ok = true;
        ok &= pre_.init(*cfg.mem, static_cast<int>(0.25f * fs_) + 8);
        ok &= in1_.init(*cfg.mem, L(142)); ok &= in2_.init(*cfg.mem, L(107));
        ok &= in3_.init(*cfg.mem, L(379)); ok &= in4_.init(*cfg.mem, L(277));
        ok &= apL1_.init(*cfg.mem, L(672) + 64); ok &= dL1_.init(*cfg.mem, L(4453));
        ok &= apL2_.init(*cfg.mem, L(1800));     ok &= dL2_.init(*cfg.mem, L(3720));
        ok &= apR1_.init(*cfg.mem, L(908) + 64); ok &= dR1_.init(*cfg.mem, L(4217));
        ok &= apR2_.init(*cfg.mem, L(2656));     ok &= dR2_.init(*cfg.mem, L(3163));
        if (!ok) return false;
        lenIn1_ = L(142); lenIn2_ = L(107); lenIn3_ = L(379); lenIn4_ = L(277);
        lenApL1_ = L(672); lenDL1_ = L(4453); lenApL2_ = L(1800); lenDL2_ = L(3720);
        lenApR1_ = L(908); lenDR1_ = L(4217); lenApR2_ = L(2656); lenDR2_ = L(3163);
        // output tap positions
        tL_[0] = L(266);  tL_[1] = L(2974); tL_[2] = L(1913); tL_[3] = L(1996); tL_[4] = L(1990); tL_[5] = L(187);  tL_[6] = L(1066);
        tR_[0] = L(353);  tR_[1] = L(3627); tR_[2] = L(1228); tR_[3] = L(2673); tR_[4] = L(2111); tR_[5] = L(335);  tR_[6] = L(121);
        lfo1_.init(fs_); lfo1_.setRate(0.9f); lfo1_.setShape(Lfo::Sine);
        lfo2_.init(fs_); lfo2_.setRate(1.3f); lfo2_.setShape(Lfo::Sine); lfo2_.setPhase(0.37f);
        mix_.init(fs_, 10.f); decay_.init(fs_, 30.f); pre_ms_.init(fs_, 80.f); mod_.init(fs_, 30.f); width_.init(fs_, 10.f);
        env_.init(fs_, 300.f);
        loadDefaults();
        mix_.snap(mix_.target()); decay_.snap(decay_.target()); pre_ms_.snap(pre_ms_.target()); mod_.snap(mod_.target()); width_.snap(width_.target());
        reset();
        return true;
    }

    void reset() override {
        for (DelayLine* d : { &pre_, &in1_, &in2_, &in3_, &in4_, &apL1_, &dL1_, &apL2_, &dL2_, &apR1_, &dR1_, &apR2_, &dR2_ }) d->clear();
        bw_.reset(); dampL_.reset(); dampR_.reset(); hpIn_.reset();
        env_.snap(0.f);
        mix_.snap(mix_.target()); decay_.snap(decay_.target()); pre_ms_.snap(pre_ms_.target()); mod_.snap(mod_.target()); width_.snap(width_.target());
    }

    void process(Frame* buf, int n) override {
        const bool active = enabled_ || env_.value() > 1e-5f;
        if (!active) return;
        for (int i = 0; i < n; ++i) {
            const float mix = mix_.next();
            const float dec = enabled_ ? decay_.next() : std::fmin(decay_.next(), 0.9f);   // bypassed: tail must decay
            const float md  = mod_.next();
            const float wd  = width_.next();
            const float preS = pre_ms_.next() * 0.001f * fs_;

            float x = enabled_ ? 0.5f * (buf[i].l + buf[i].r) : 0.f;
            pre_.write(x);
            x = pre_.read(preS + 1.f);
            x = hpIn_.process(x);
            x = bw_.process(x);
            // input diffusion
            x = allpass(in1_, lenIn1_, x, diff1_);
            x = allpass(in2_, lenIn2_, x, diff1_);
            x = allpass(in3_, lenIn3_, x, diff2_);
            x = allpass(in4_, lenIn4_, x, diff2_);

            // tank
            const float exc = 16.f * k_ * md;
            float l = x + dec * dR2_.readInt(lenDR2_);
            l = allpassMod(apL1_, lenApL1_, l, -0.7f, exc * (lfo1_.next() - 0.5f) * 2.f);
            dL1_.write(l);
            l = dampL_.process(dL1_.readInt(lenDL1_)) * dec;
            l = allpass(apL2_, lenApL2_, l, 0.5f);
            dL2_.write(l);

            float r = x + dec * dL2_.readInt(lenDL2_);
            r = allpassMod(apR1_, lenApR1_, r, -0.7f, exc * (lfo2_.next() - 0.5f) * 2.f);
            dR1_.write(r);
            r = dampR_.process(dR1_.readInt(lenDR1_)) * dec;
            r = allpass(apR2_, lenApR2_, r, 0.5f);
            dR2_.write(r);

            // output taps (Dattorro Table 1)
            float yl = 0.6f * (dR1_.readInt(tL_[0]) + dR1_.readInt(tL_[1]) - apR2_.readInt(tL_[2]) + dR2_.readInt(tL_[3])
                             - dL1_.readInt(tL_[4]) - apL2_.readInt(tL_[5]) - dL2_.readInt(tL_[6]));
            float yr = 0.6f * (dL1_.readInt(tR_[0]) + dL1_.readInt(tR_[1]) - apL2_.readInt(tR_[2]) + dL2_.readInt(tR_[3])
                             - dR1_.readInt(tR_[4]) - apR2_.readInt(tR_[5]) - dR2_.readInt(tR_[6]));
            // width: 0 = mono, 1 = full
            const float m = 0.5f * (yl + yr), s = 0.5f * (yl - yr) * wd;
            yl = m + s; yr = m - s;

            env_.set(std::fabs(x) + std::fabs(yl) + std::fabs(yr)); env_.next();

            const float dryG = enabled_ ? 1.f - mix : 1.f;   // bypassed with trails: dry at unity
            buf[i].l = buf[i].l * dryG + yl * mix;
            buf[i].r = buf[i].r * dryG + yr * mix;
        }
    }

protected:
    void onParam(int i, float v) override {
        switch (i) {
            case Mix:       mix_.set(v); break;
            case Decay:     decay_.set(v); break;
            case PreDelay:  pre_ms_.set(v); break;
            case HighCut:   dampL_.setOnePoleLP(fs_, v); dampR_.setOnePoleLP(fs_, v); bw_.setOnePoleLP(fs_, clampf(v * 1.5f, 500.f, 20000.f)); break;
            case LowCut:    hpIn_.set(Biquad::HighPass, fs_, v); break;
            case Mod:       mod_.set(v); break;
            case Diffusion: diff1_ = 0.75f * v; diff2_ = 0.625f * v; break;
            case Width:     width_.set(v); break;
        }
    }
private:
    // lattice allpass: w = x + g*d ; y = d - g*w
    static inline float allpass(DelayLine& dl, int len, float x, float g) {
        const float d = dl.readInt(len);
        const float w = x + g * d;
        dl.write(flushDenormal(w));
        return d - g * w;
    }
    static inline float allpassMod(DelayLine& dl, int len, float x, float g, float excursion) {
        const float d = dl.read(static_cast<float>(len) + excursion);
        const float w = x + g * d;
        dl.write(flushDenormal(w));
        return d - g * w;
    }

    float fs_ = 48000.f, k_ = 1.f;
    DelayLine pre_, in1_, in2_, in3_, in4_, apL1_, dL1_, apL2_, dL2_, apR1_, dR1_, apR2_, dR2_;
    int lenIn1_ = 0, lenIn2_ = 0, lenIn3_ = 0, lenIn4_ = 0, lenApL1_ = 0, lenDL1_ = 0, lenApL2_ = 0, lenDL2_ = 0,
        lenApR1_ = 0, lenDR1_ = 0, lenApR2_ = 0, lenDR2_ = 0;
    int tL_[7] = {}, tR_[7] = {};
    Biquad bw_, dampL_, dampR_, hpIn_;
    Lfo lfo1_, lfo2_;
    Smoother mix_, decay_, pre_ms_, mod_, width_, env_;
    float diff1_ = 0.75f, diff2_ = 0.625f;
};

} // namespace pedal
