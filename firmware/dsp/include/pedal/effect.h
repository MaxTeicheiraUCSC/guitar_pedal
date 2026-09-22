// pedal/effect.h — effect interface shared by all four effects and the chain.
#pragma once
#include "core.h"

namespace pedal {

enum class EffectId : uint8_t { Tremolo = 0, Delay = 1, Reverb = 2, Fuzz = 3 };

class Effect {
public:
    virtual ~Effect() = default;

    virtual const char*      name() const = 0;
    virtual int              numParams() const = 0;
    virtual const ParamDesc& paramDesc(int i) const = 0;

    // Returns false if allocation failed.
    virtual bool init(const Config& cfg) = 0;
    virtual void reset() = 0;

    // In-place stereo processing. Called every block regardless of enabled
    // state so effects can render trails; effects with no state short-circuit.
    virtual void process(Frame* buf, int n) = 0;

    // Tap-tempo / clock hook (seconds per beat). Effects that don't care ignore it.
    virtual void setTempo(float secondsPerBeat) { (void)secondsPerBeat; }

    // enable/disable with trails. Base class tracks the flag; effects read it.
    virtual void setEnabled(bool e) { enabled_ = e; }
    bool enabled() const { return enabled_; }

    // normalized parameter access (0..1). Real-unit conversion via paramDesc().
    void  setParamNorm(int i, float n) { if (i >= 0 && i < numParams()) { norm_[i] = clampf(n, 0.f, 1.f); onParam(i, paramDesc(i).toReal(norm_[i])); } }
    float getParamNorm(int i) const    { return (i >= 0 && i < numParams()) ? norm_[i] : 0.f; }
    void  setParamReal(int i, float v) { if (i >= 0 && i < numParams()) setParamNorm(i, paramDesc(i).toNorm(v)); }
    float getParamReal(int i) const    { return (i >= 0 && i < numParams()) ? paramDesc(i).toReal(norm_[i]) : 0.f; }

    void loadDefaults() { for (int i = 0; i < numParams(); ++i) setParamReal(i, paramDesc(i).def); }

protected:
    virtual void onParam(int index, float realValue) = 0;
    bool  enabled_ = true;
    float norm_[kMaxParams] = {};
};

} // namespace pedal
