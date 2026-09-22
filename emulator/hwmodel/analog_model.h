// emulator/hwmodel/analog_model.h — the deliberately simple ("ideal") model of the
// pedal's analog I/O stages and codec, derived from the Seed3 datasheet Fig 3.4/3.5
// (instrument-level input/output, OPA1652 on a +9 V single rail, AREF bias = 4.5 V).
//
// It runs at an internal rate of kInternalOversample x fs so the 48-82 kHz RC corners
// and the op-amp/codec clipping are represented without bilinear warping.
// Its parameters (see AnalogParams) are what hardware/spice/validate fits against
// the full ngspice reference; defaults below are the hand-derived nominal values.
#pragma once
#include "pedal/core.h"
#include "pedal/oversampler.h"
#include <cstring>

namespace hw {

struct AnalogParams {
    // supply / bias
    float vBias      = 4.5f;   // AREF_AUDIO_BIAS
    float vRailHi    = 9.0f;
    float opampSat   = 0.06f;  // OPA1652 rail-to-rail output stays this far from each rail (V) — matches the SPICE behavioral model's VSAT
    float opampKnee  = 0.02f;  // softness of the saturation knee (V); fitted to the SPICE reference (report: THD vs level)

    // input stage (Fig 3.4)
    float inCouplingHz    = 0.0159f;  // 10 uF into 1 M
    // 3k3 / (4k7 || 10k): the second stage's 10k input resistor loads the divider.
    // (Hand derivation missed this; caught by hardware/spice/validate — see report.)
    float inAtten         = 3.1973f / (3.3f + 3.1973f);   // = 0.4921
    float inAttenLpHz     = 98000.f;                      // (3k3 || 3.197k) * 1 nF
    float inStage2HpHz    = 1.59f;                        // 10 uF into 10 k
    float inStage2Gain    = -0.68f;                       // 6k8 / 10k inverting: op-amp rail limit x 0.492 x 0.68 = 1.47 V < codec 1.8 V
    float inStage2LpHz    = 49800.f;                      // 6k8 || 470 pF
    float inRcLpHz        = 48229.f;                      // 100 R + 33 nF
    float codecLoad       = 20000.f / (20000.f + 100.f);  // codec 20 k input impedance after 100 R
    float codecHpHz       = 2.0f;                         // codec AC coupling (TAC5242, assumed)
    float codecClipV      = 1.8f;                         // absolute max; clamp diodes conduct here
    float codecFullScaleV = 1.8f;                         // 3.6 Vpp = 0 dBFS
    int   codecBits       = 24;

    // output stage (Fig 3.5)
    float dacFullScaleV   = 1.4142f;   // 0 dBFS @ 1 Vrms
    float outCouplingHz   = 1.061f;    // 10 uF into 15 k
    float outGain         = -33.f / 15.f;
    float outLpHz         = 48229.f;   // 33 k || 100 pF
    float outLoadHz       = 1.59f;     // 10 uF into 10 k (+ amp input)
    float outLoadGain     = 10000.f / 10100.f;
};

// name -> field table so the validation harness can override any parameter by name
struct AnalogParamEntry { const char* name; float AnalogParams::* field; };
inline const AnalogParamEntry* analogParamTable(int& count) {
    static const AnalogParamEntry t[] = {
        { "vBias", &AnalogParams::vBias }, { "vRailHi", &AnalogParams::vRailHi }, { "opampSat", &AnalogParams::opampSat }, { "opampKnee", &AnalogParams::opampKnee },
        { "inCouplingHz", &AnalogParams::inCouplingHz }, { "inAtten", &AnalogParams::inAtten }, { "inAttenLpHz", &AnalogParams::inAttenLpHz },
        { "inStage2HpHz", &AnalogParams::inStage2HpHz }, { "inStage2Gain", &AnalogParams::inStage2Gain }, { "inStage2LpHz", &AnalogParams::inStage2LpHz },
        { "inRcLpHz", &AnalogParams::inRcLpHz }, { "codecLoad", &AnalogParams::codecLoad }, { "codecHpHz", &AnalogParams::codecHpHz },
        { "codecClipV", &AnalogParams::codecClipV }, { "codecFullScaleV", &AnalogParams::codecFullScaleV },
        { "dacFullScaleV", &AnalogParams::dacFullScaleV }, { "outCouplingHz", &AnalogParams::outCouplingHz }, { "outGain", &AnalogParams::outGain },
        { "outLpHz", &AnalogParams::outLpHz }, { "outLoadHz", &AnalogParams::outLoadHz }, { "outLoadGain", &AnalogParams::outLoadGain },
    };
    count = sizeof(t) / sizeof(t[0]); return t;
}
inline bool setAnalogParam(AnalogParams& p, const char* name, float v) {
    int n; const AnalogParamEntry* t = analogParamTable(n);
    for (int i = 0; i < n; ++i) if (!std::strcmp(t[i].name, name)) { p.*(t[i].field) = v; return true; }
    return false;
}

constexpr int kInternalOversample = 8;   // 384 kHz internal: keeps the ~100 kHz RC corners below Nyquist

class OpampStage {
public:
    void set(const AnalogParams& p) { lo_ = p.opampSat; hi_ = p.vRailHi - p.opampSat; knee_ = p.opampKnee; }
    // soft saturation towards the rails: linear inside, tanh knee outside
    inline float process(float v) const {
        if (v > hi_ - knee_) return hi_ - knee_ + knee_ * std::tanh((v - (hi_ - knee_)) / knee_);
        if (v < lo_ + knee_) return lo_ + knee_ - knee_ * std::tanh(((lo_ + knee_) - v) / knee_);
        return v;
    }
private:
    float lo_ = 0.1f, hi_ = 8.9f, knee_ = 0.12f;
};

// Volts at the jack tip -> volts at the codec pin (relative to codec bias), one channel.
class InputStage {
public:
    void init(float fs, const AnalogParams& p) {
        p_ = p; fsi_ = fs * kInternalOversample;
        coupling_.setOnePoleHP(fsi_, p.inCouplingHz);
        attenLp_.setOnePoleLP(fsi_, p.inAttenLpHz);
        s2Hp_.setOnePoleHP(fsi_, p.inStage2HpHz);
        s2Lp_.setOnePoleLP(fsi_, p.inStage2LpHz);
        rcLp_.setOnePoleLP(fsi_, p.inRcLpHz);
        codecHp_.setOnePoleHP(fsi_, p.codecHpHz);
        opamp_.set(p);
        up_.init(kInternalOversample); down_.init(kInternalOversample);
    }
    // one sample at fs in, one sample at fs out (volts)
    inline float process(float vJack) {
        float tmp[kInternalOversample];
        up_.up(vJack, tmp);
        for (int k = 0; k < kInternalOversample; ++k) tmp[k] = step(tmp[k]);
        return down_.down(tmp);
    }
    // internal-rate step (exposed for tests)
    inline float step(float v) {
        v = coupling_.process(v) + p_.vBias;          // biased at AREF
        v = opamp_.process(v);                        // unity buffer
        v = p_.vBias + (v - p_.vBias) * p_.inAtten;   // 3k3/4k7 (referenced to bias: 4k7 goes to GND in the schematic
                                                      //   — see note in hardware/spice/README.md; DC handled by next HP)
        v = attenLp_.process(v);
        float ac = s2Hp_.process(v - p_.vBias);       // 10 uF into 10 k
        ac = s2Lp_.process(ac * p_.inStage2Gain);
        v = opamp_.process(p_.vBias + ac);
        ac = rcLp_.process(v - p_.vBias) * p_.codecLoad;
        ac = codecHp_.process(ac);
        // clamp diodes / codec absolute max
        if (ac >  p_.codecClipV) ac =  p_.codecClipV;
        if (ac < -p_.codecClipV) ac = -p_.codecClipV;
        return ac;
    }
    float toNormalized(float vCodec) const {          // codec pin volts -> [-1,1] float with quantization
        float x = vCodec / p_.codecFullScaleV;
        const float q = static_cast<float>(1u << (p_.codecBits - 1));
        return std::round(x * q) / q;
    }
private:
    AnalogParams p_; float fsi_ = 192000.f;
    pedal::Biquad coupling_, attenLp_, s2Hp_, s2Lp_, rcLp_, codecHp_;
    OpampStage opamp_;
    pedal::Oversampler up_, down_;
};

// DAC float [-1,1] -> volts at the output jack, one channel.
class OutputStage {
public:
    void init(float fs, const AnalogParams& p) {
        p_ = p; fsi_ = fs * kInternalOversample;
        coupling_.setOnePoleHP(fsi_, p.outCouplingHz);
        lp_.setOnePoleLP(fsi_, p.outLpHz);
        load_.setOnePoleHP(fsi_, p.outLoadHz);
        opamp_.set(p);
        up_.init(kInternalOversample); down_.init(kInternalOversample);
    }
    inline float process(float x) {
        float tmp[kInternalOversample];
        up_.up(x, tmp);
        for (int k = 0; k < kInternalOversample; ++k) tmp[k] = step(tmp[k]);
        return down_.down(tmp);
    }
    inline float step(float x) {
        float v = x * p_.dacFullScaleV;
        if (v >  p_.dacFullScaleV) v =  p_.dacFullScaleV;
        if (v < -p_.dacFullScaleV) v = -p_.dacFullScaleV;
        v = coupling_.process(v);
        v = lp_.process(v * p_.outGain);
        v = opamp_.process(p_.vBias + v) - p_.vBias;
        return load_.process(v) * p_.outLoadGain;
    }
private:
    AnalogParams p_; float fsi_ = 192000.f;
    pedal::Biquad coupling_, lp_, load_;
    OpampStage opamp_;
    pedal::Oversampler up_, down_;
};

// Potentiometer -> ADC -> smoothed control value, with soft take-over.
class PotModel {
public:
    void init(float controlRateHz, int bits = 12, float smoothMs = 8.f) {
        bits_ = bits; a_ = std::exp(-1.f / (0.001f * smoothMs * controlRateHz));
    }
    // physical knob position 0..1 -> value seen by firmware (after quantization/smoothing)
    float read(float knob) {
        const float q = static_cast<float>((1 << bits_) - 1);
        const float adc = std::round(pedal::clampf(knob, 0.f, 1.f) * q) / q;
        y_ = adc + a_ * (y_ - adc);
        return y_;
    }
    // soft take-over: returns true when the knob has "caught" the remote value
    bool takeover(float knob, float remote) {
        if (caught_) return true;
        if (!armed_) { armed_ = true; lastKnob_ = knob; return false; }
        const bool crossed = (lastKnob_ - remote) * (knob - remote) <= 0.f || std::fabs(knob - remote) < 0.01f;
        lastKnob_ = knob;
        if (crossed) caught_ = true;
        return caught_;
    }
    void release() { caught_ = false; armed_ = false; }   // call when a remote change arrives
private:
    int bits_ = 12; float a_ = 0.f, y_ = 0.f, lastKnob_ = 0.f; bool caught_ = true, armed_ = false;
};

} // namespace hw
