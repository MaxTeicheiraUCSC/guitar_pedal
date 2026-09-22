// pedal/pedal.h — the whole pedal: four effects, reorderable chain, global
// high/low cut toggles, tap tempo, mono/stereo handling, presets.
#pragma once
#include "core.h"
#include "effect.h"
#include "tremolo.h"
#include "delay.h"
#include "reverb.h"
#include "fuzz.h"

namespace pedal {

// POD preset: safe to memcpy into QSPI flash on the Daisy or to a file on the host.
struct Preset {
    static constexpr uint32_t kMagic = 0x31504450u; // "PDP1"
    uint32_t magic = kMagic;
    char     name[16] = "Init";
    uint8_t  order[kNumEffects] = { 3, 0, 1, 2 };   // fuzz -> tremolo -> delay -> reverb
    uint8_t  enabled[kNumEffects] = { 0, 0, 0, 0 };
    float    params[kNumEffects][kMaxParams] = {};  // normalized
    uint8_t  globalHp = 0;                          // 0 off, 1 slot A, 2 slot B
    uint8_t  globalLp = 0;
    float    hpFreq[2] = { 80.f, 160.f };
    float    lpFreq[2] = { 8000.f, 4000.f };
    float    tempoSpb  = 0.5f;                      // 120 bpm
    float    inputTrimDb  = 0.f;
    float    outputTrimDb = 0.f;
};

// Notifications out of the core (UART echo on Daisy, UI on host). No std::function.
class PedalListener {
public:
    virtual ~PedalListener() = default;
    virtual void onParamChanged(int effect, int param, float norm) { (void)effect; (void)param; (void)norm; }
    virtual void onEnabledChanged(int effect, bool on) { (void)effect; (void)on; }
    virtual void onOrderChanged(const uint8_t* order) { (void)order; }
    virtual void onGlobalCutChanged() {}
    virtual void onTempoChanged(float spb) { (void)spb; }
    virtual void onPresetRequest(bool save, int slot) { (void)save; (void)slot; }
};

class Pedal {
public:
    bool init(const Config& cfg) {
        fs_ = cfg.sampleRate;
        effects_[0] = &tremolo_; effects_[1] = &delay_; effects_[2] = &reverb_; effects_[3] = &fuzz_;
        for (auto* e : effects_) if (!e->init(cfg)) return false;
        inTrim_.init(fs_, 10.f); outTrim_.init(fs_, 10.f);
        Preset p; defaultPreset(p); applyPreset(p);
        inTrim_.snap(inTrim_.target()); outTrim_.snap(outTrim_.target());
        return true;
    }
    void setListener(PedalListener* l) { listener_ = l; }

    // ---- audio ----
    void setStereoInput(bool s) { stereoIn_ = s; }
    bool stereoInput() const { return stereoIn_; }

    void process(Frame* buf, int n) {
        for (int i = 0; i < n; ++i) {
            const float g = inTrim_.next();
            buf[i].l *= g;
            buf[i].r = stereoIn_ ? buf[i].r * g : buf[i].l;
        }
        for (int k = 0; k < kNumEffects; ++k) effects_[order_[k]]->process(buf, n);
        for (int i = 0; i < n; ++i) {
            Frame f = buf[i];
            if (hpMode_) f = hp_.process(f);
            if (lpMode_) f = lp_.process(f);
            const float g = outTrim_.next();
            buf[i] = { f.l * g, f.r * g };
        }
        samplePos_ += static_cast<uint64_t>(n);
    }
    uint64_t samplePosition() const { return samplePos_; }

    // ---- effects ----
    Effect* effect(int i) { return effects_[i]; }
    Tremolo& tremolo() { return tremolo_; }
    Delay&   delay()   { return delay_; }
    Reverb&  reverb()  { return reverb_; }
    Fuzz&    fuzz()    { return fuzz_; }

    void setParam(int e, int p, float norm, bool notify = true) {
        if (e < 0 || e >= kNumEffects) return;
        effects_[e]->setParamNorm(p, norm);
        if (notify && listener_) listener_->onParamChanged(e, p, effects_[e]->getParamNorm(p));
    }
    float getParam(int e, int p) const { return effects_[e]->getParamNorm(p); }

    void setEnabled(int e, bool on, bool notify = true) {
        if (e < 0 || e >= kNumEffects) return;
        effects_[e]->setEnabled(on);
        if (notify && listener_) listener_->onEnabledChanged(e, on);
    }
    void toggle(int e) { setEnabled(e, !effects_[e]->enabled()); }
    bool isEnabled(int e) const { return effects_[e]->enabled(); }

    // ---- chain order ----
    bool setOrder(const uint8_t* order, bool notify = true) {
        uint8_t seen = 0;
        for (int i = 0; i < kNumEffects; ++i) { if (order[i] >= kNumEffects) return false; seen |= 1u << order[i]; }
        if (seen != 0x0F) return false;
        for (int i = 0; i < kNumEffects; ++i) order_[i] = order[i];
        if (notify && listener_) listener_->onOrderChanged(order_);
        return true;
    }
    const uint8_t* order() const { return order_; }

    // ---- global cuts (physical toggles: 0 off / 1 slot A / 2 slot B) ----
    void setGlobalHp(int mode) { hpMode_ = clampi(mode, 0, 2); if (hpMode_) hp_.set(Biquad::HighPass, fs_, hpFreq_[hpMode_ - 1]); hp_.reset(); if (listener_) listener_->onGlobalCutChanged(); }
    void setGlobalLp(int mode) { lpMode_ = clampi(mode, 0, 2); if (lpMode_) lp_.set(Biquad::LowPass,  fs_, lpFreq_[lpMode_ - 1]); lp_.reset(); if (listener_) listener_->onGlobalCutChanged(); }
    void setHpFreq(int slot, float hz) { hpFreq_[slot & 1] = clampf(hz, 20.f, 2000.f); if (hpMode_ == (slot & 1) + 1) hp_.set(Biquad::HighPass, fs_, hpFreq_[slot & 1]); }
    void setLpFreq(int slot, float hz) { lpFreq_[slot & 1] = clampf(hz, 1000.f, 20000.f); if (lpMode_ == (slot & 1) + 1) lp_.set(Biquad::LowPass, fs_, lpFreq_[slot & 1]); }
    int   globalHp() const { return hpMode_; }
    int   globalLp() const { return lpMode_; }
    float hpFreq(int slot) const { return hpFreq_[slot & 1]; }
    float lpFreq(int slot) const { return lpFreq_[slot & 1]; }

    // ---- tempo ----
    void setTempo(float spb, bool notify = true) {
        tempoSpb_ = clampf(spb, 0.05f, 4.f);
        for (auto* e : effects_) e->setTempo(tempoSpb_);
        if (notify && listener_) listener_->onTempoChanged(tempoSpb_);
    }
    float tempo() const { return tempoSpb_; }
    // Tap tempo: call with the current sample position at each tap.
    void tap(uint64_t atSample) {
        if (lastTap_ != 0 && atSample > lastTap_) {
            const float spb = static_cast<float>(atSample - lastTap_) / fs_;
            if (spb < 3.f) {
                tapAvg_ = tapCount_ == 0 ? spb : 0.6f * tapAvg_ + 0.4f * spb;
                ++tapCount_;
                setTempo(tapAvg_);
            } else tapCount_ = 0;
        }
        lastTap_ = atSample;
    }

    void setInputTrimDb(float db)  { inTrimDb_ = db;  inTrim_.set(dbToLin(db)); }
    void setOutputTrimDb(float db) { outTrimDb_ = db; outTrim_.set(dbToLin(db)); }

    // ---- presets ----
    void toPreset(Preset& p) const {
        p.magic = Preset::kMagic;
        for (int e = 0; e < kNumEffects; ++e) {
            p.order[e] = order_[e]; p.enabled[e] = effects_[e]->enabled() ? 1 : 0;
            for (int i = 0; i < kMaxParams; ++i) p.params[e][i] = effects_[e]->getParamNorm(i);
        }
        p.globalHp = static_cast<uint8_t>(hpMode_); p.globalLp = static_cast<uint8_t>(lpMode_);
        p.hpFreq[0] = hpFreq_[0]; p.hpFreq[1] = hpFreq_[1]; p.lpFreq[0] = lpFreq_[0]; p.lpFreq[1] = lpFreq_[1];
        p.tempoSpb = tempoSpb_; p.inputTrimDb = inTrimDb_; p.outputTrimDb = outTrimDb_;
    }
    bool applyPreset(const Preset& p) {
        if (p.magic != Preset::kMagic) return false;
        if (!setOrder(p.order, false)) return false;
        for (int e = 0; e < kNumEffects; ++e) {
            effects_[e]->setEnabled(p.enabled[e] != 0);
            for (int i = 0; i < effects_[e]->numParams(); ++i) effects_[e]->setParamNorm(i, p.params[e][i]);
        }
        hpFreq_[0] = p.hpFreq[0]; hpFreq_[1] = p.hpFreq[1]; lpFreq_[0] = p.lpFreq[0]; lpFreq_[1] = p.lpFreq[1];
        setGlobalHp(p.globalHp); setGlobalLp(p.globalLp);
        setTempo(p.tempoSpb, false);
        setInputTrimDb(p.inputTrimDb); setOutputTrimDb(p.outputTrimDb);
        return true;
    }
    // Default parameter values into a preset (for "Init")
    static void defaultPreset(Preset& p) {
        Tremolo t; Delay d; Reverb r; Fuzz f;
        Effect* fx[kNumEffects] = { &t, &d, &r, &f };
        p = Preset{};
        for (int e = 0; e < kNumEffects; ++e)
            for (int i = 0; i < fx[e]->numParams(); ++i) p.params[e][i] = fx[e]->paramDesc(i).toNorm(fx[e]->paramDesc(i).def);
    }

    void requestPreset(bool save, int slot) { if (listener_) listener_->onPresetRequest(save, slot); }

private:
    static int clampi(int x, int lo, int hi) { return x < lo ? lo : (x > hi ? hi : x); }
    float fs_ = 48000.f;
    Tremolo tremolo_; Delay delay_; Reverb reverb_; Fuzz fuzz_;
    Effect* effects_[kNumEffects] = {};
    uint8_t order_[kNumEffects] = { 3, 0, 1, 2 };
    StereoBiquad hp_, lp_;
    int hpMode_ = 0, lpMode_ = 0;
    float hpFreq_[2] = { 80.f, 160.f }, lpFreq_[2] = { 8000.f, 4000.f };
    float tempoSpb_ = 0.5f, tapAvg_ = 0.5f, inTrimDb_ = 0.f, outTrimDb_ = 0.f;
    uint64_t lastTap_ = 0, samplePos_ = 0; int tapCount_ = 0;
    Smoother inTrim_, outTrim_;
    bool stereoIn_ = true;
    PedalListener* listener_ = nullptr;
};

} // namespace pedal
