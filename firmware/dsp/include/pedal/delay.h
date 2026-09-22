// pedal/delay.h — stereo fractional delay with filtered/soft-clipped feedback,
// wow/flutter modulation, ping-pong, tap-tempo subdivisions and trails on bypass.
#pragma once
#include "effect.h"

namespace pedal {

class Delay final : public Effect {
public:
    enum P { Time, Feedback, Mix, LowCut, HighCut, Mod, PingPong, Subdiv, NumP };
    enum Sub { SubOff, SubQuarter, SubEighth, SubDottedEighth, SubTripletEighth, SubSixteenth, SubCount };

    static constexpr float kMaxSeconds = 2.0f;

    const char* name() const override { return "Delay"; }
    int numParams() const override { return NumP; }
    const ParamDesc& paramDesc(int i) const override {
        static const ParamDesc d[NumP] = {
            { "Time",     10.f,  2000.f, 380.f, Curve::Log,    "ms", 0 },
            { "Feedback", 0.f,   1.1f,   0.4f,  Curve::Linear, "",   0 },
            { "Mix",      0.f,   1.f,    0.35f, Curve::Linear, "",   0 },
            { "LowCut",   20.f,  2000.f, 120.f, Curve::Log,    "Hz", 0 },
            { "HighCut",  500.f, 20000.f,5000.f,Curve::Log,    "Hz", 0 },
            { "Mod",      0.f,   1.f,    0.15f, Curve::Linear, "",   0 },
            { "PingPong", 0.f,   1.f,    0.f,   Curve::Switch, "",   2 },
            { "Subdiv",   0.f,   5.f,    0.f,   Curve::Steps,  "",   SubCount },
        };
        return d[i];
    }

    bool init(const Config& cfg) override {
        fs_ = cfg.sampleRate;
        if (!cfg.mem) return false;
        const int maxS = static_cast<int>(kMaxSeconds * fs_) + 64;
        if (!dl_.init(*cfg.mem, maxS) || !dr_.init(*cfg.mem, maxS)) return false;
        timeS_.init(fs_, 60.f);           // tape-style slide when time changes
        fb_.init(fs_, 10.f); mix_.init(fs_, 10.f); mod_.init(fs_, 30.f);
        wow_.init(fs_); wow_.setRate(0.55f); wow_.setShape(Lfo::Sine);
        flut_.init(fs_); flut_.setRate(6.3f); flut_.setShape(Lfo::Triangle);
        env_.init(fs_, 200.f);
        loadDefaults();
        timeS_.snap(timeS_.target()); fb_.snap(fb_.target()); mix_.snap(mix_.target()); mod_.snap(mod_.target());
        reset();
        return true;
    }
    void reset() override {
        dl_.clear(); dr_.clear();
        hpL_.reset(); hpR_.reset(); lpL_.reset(); lpR_.reset();
        env_.snap(0.f);
        timeS_.snap(timeS_.target()); fb_.snap(fb_.target()); mix_.snap(mix_.target()); mod_.snap(mod_.target());
    }

    void setTempo(float spb) override {
        tempoSpb_ = spb;
        applySubdiv();
    }

    void process(Frame* buf, int n) override {
        const bool active = enabled_ || env_.value() > 1e-5f;
        if (!active) return;
        for (int i = 0; i < n; ++i) {
            const float t   = timeS_.next();
            const float fb  = enabled_ ? fb_.next() : std::fmin(fb_.next(), 0.9f);   // bypassed: trails must always decay
            const float mix = mix_.next();
            const float md  = mod_.next();
            // wow (slow, deep) + flutter (fast, shallow), in samples
            const float modS = md * (0.0025f * fs_ * (wow_.next() - 0.5f) + 0.0003f * fs_ * (flut_.next() - 0.5f));
            const float dT = clampf(t + modS, 1.f, static_cast<float>(dl_.capacity()));
            const float yl = dl_.read(dT);
            const float yr = dr_.read(dT);

            Frame in = enabled_ ? buf[i] : Frame{};
            float fbl, fbr;
            if (pingpong_) {
                const float mono = 0.5f * (in.l + in.r);
                fbl = mono + fb * yr;
                fbr = fb * yl;
            } else {
                fbl = in.l + fb * yl;
                fbr = in.r + fb * yr;
            }
            // feedback conditioning: low cut, high cut, gentle saturation (keeps >1 feedback musical)
            fbl = lpL_.process(hpL_.process(fbl));
            fbr = lpR_.process(hpR_.process(fbr));
            fbl = softClip(fbl); fbr = softClip(fbr);
            dl_.write(flushDenormal(fbl));
            dr_.write(flushDenormal(fbr));

            env_.set(std::fabs(fbl) + std::fabs(fbr) + std::fabs(yl) + std::fabs(yr)); env_.next();

            // bypassed with trails: dry passes at unity, only the decaying wet is added
            const float dryG = enabled_ ? 1.f - mix : 1.f;
            buf[i].l = buf[i].l * dryG + yl * mix;
            buf[i].r = buf[i].r * dryG + yr * mix;
        }
    }

    float currentTimeMs() const { return 1000.f * timeS_.target() / fs_; }

protected:
    void onParam(int i, float v) override {
        switch (i) {
            case Time:     timeMs_ = v; if (subdiv_ == SubOff) setTimeMs(v); break;
            case Feedback: fb_.set(v); break;
            case Mix:      mix_.set(v); break;
            case LowCut:   hpL_.set(Biquad::HighPass, fs_, v); hpR_.set(Biquad::HighPass, fs_, v); break;
            case HighCut:  lpL_.set(Biquad::LowPass,  fs_, v); lpR_.set(Biquad::LowPass,  fs_, v); break;
            case Mod:      mod_.set(v); break;
            case PingPong: pingpong_ = v >= 0.5f; break;
            case Subdiv:   subdiv_ = static_cast<int>(v); if (subdiv_ == SubOff) setTimeMs(timeMs_); else applySubdiv(); break;
        }
    }
private:
    void setTimeMs(float ms) { timeS_.set(clampf(0.001f * ms * fs_, 1.f, static_cast<float>(dl_.capacity()))); }
    void applySubdiv() {
        if (subdiv_ == SubOff) return;
        static const float mult[SubCount] = { 1.f, 1.f, 0.5f, 0.75f, 1.f / 3.f, 0.25f };
        setTimeMs(1000.f * tempoSpb_ * mult[subdiv_]);
    }
    float fs_ = 48000.f, timeMs_ = 380.f, tempoSpb_ = 0.5f;
    DelayLine dl_, dr_;
    Smoother timeS_, fb_, mix_, mod_, env_;
    Lfo wow_, flut_;
    Biquad hpL_, hpR_, lpL_, lpR_;
    bool pingpong_ = false;
    int subdiv_ = SubOff;
};

} // namespace pedal
