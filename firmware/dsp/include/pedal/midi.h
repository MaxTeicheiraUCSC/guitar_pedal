// pedal/midi.h — byte-stream MIDI parser and the pedal's control map.
// The same map is used by the ESP32 bridge (BLE-MIDI / web UI -> UART) and by
// the emulator, so one document (docs/midi-map.md) describes all remote control.
//
//   CC  0..31   effect (cc/8) param (cc%8) MSB   (14-bit with LSB on CC 32..63)
//   CC 64..67   effect enable (>=64 on)
//   CC 70       global high-cut mode 0/1/2      CC 71  global low-cut mode 0/1/2
//   CC 72       tap tempo (any value)
//   CC 74       input trim (-12..+12 dB)        CC 75  output trim
//   PC n        load preset n
//   SysEx F0 7D <cmd> ... F7   (7D = non-commercial manufacturer id)
//        01 o0 o1 o2 o3            set chain order
//        02 n / 03 n               save / load preset n
//        04 s hi lo                HP slot s freq (14-bit Hz)
//        05 s hi lo                LP slot s freq
//        06 spbHi spbLo            tempo in ms per beat (14-bit)
//        07                        request full state (device answers with a dump)
#pragma once
#include "pedal.h"

namespace pedal {

class MidiParser {
public:
    explicit MidiParser(Pedal& p) : pedal_(p) {}

    void feed(uint8_t b) {
        if (b == 0xF0) { inSysex_ = true; sxLen_ = 0; return; }
        if (b == 0xF7) { if (inSysex_) handleSysex(); inSysex_ = false; return; }
        if (inSysex_) { if (sxLen_ < kSysexMax) sx_[sxLen_++] = b; return; }
        if (b >= 0xF8) return;                       // realtime: ignore (clock could drive tempo later)
        if (b & 0x80) { status_ = b; d1_ = -1; return; }
        if (status_ == 0) return;
        const uint8_t type = status_ & 0xF0;
        if (type == 0xC0) { programChange(b); return; }
        if (type == 0xD0) return;
        if (d1_ < 0) { d1_ = b; return; }
        const uint8_t d1 = static_cast<uint8_t>(d1_); d1_ = -1;
        if (type == 0xB0) controlChange(d1, b);
    }
    void feed(const uint8_t* data, int n) { for (int i = 0; i < n; ++i) feed(data[i]); }

    // ---- encoding helpers (used by the device to echo state) ----
    static int encodeParamCC(int effect, int param, float norm, uint8_t* out) {
        const int v = static_cast<int>(norm * 16383.f + 0.5f);
        const uint8_t cc = static_cast<uint8_t>(effect * 8 + param);
        out[0] = 0xB0; out[1] = cc;      out[2] = static_cast<uint8_t>((v >> 7) & 0x7F);
        out[3] = 0xB0; out[4] = cc + 32; out[5] = static_cast<uint8_t>(v & 0x7F);
        return 6;
    }
    static int encodeEnableCC(int effect, bool on, uint8_t* out) { out[0] = 0xB0; out[1] = static_cast<uint8_t>(64 + effect); out[2] = on ? 127 : 0; return 3; }
    static int encodeOrder(const uint8_t* order, uint8_t* out) { out[0] = 0xF0; out[1] = 0x7D; out[2] = 0x01; for (int i = 0; i < 4; ++i) out[3 + i] = order[i]; out[7] = 0xF7; return 8; }
    static int encodeTempo(float spb, uint8_t* out) { const int ms = static_cast<int>(spb * 1000.f + 0.5f) & 0x3FFF; out[0] = 0xF0; out[1] = 0x7D; out[2] = 0x06; out[3] = ms >> 7; out[4] = ms & 0x7F; out[5] = 0xF7; return 6; }
    static int encodeCut(bool lp, int slot, float hz, uint8_t* out) { const int v = static_cast<int>(hz + 0.5f) & 0x3FFF; out[0] = 0xF0; out[1] = 0x7D; out[2] = lp ? 0x05 : 0x04; out[3] = static_cast<uint8_t>(slot & 1); out[4] = v >> 7; out[5] = v & 0x7F; out[6] = 0xF7; return 7; }

    // dumps the entire state as a MIDI byte stream (max ~ 4*8*6 + 4*3 + 8 + 6 + 4*7 + 6 = 246 bytes)
    static constexpr int kDumpMax = 320;
    static int encodeStateDump(const Pedal& p, uint8_t* out) {
        int n = 0;
        for (int e = 0; e < kNumEffects; ++e) {
            for (int i = 0; i < kMaxParams; ++i) n += encodeParamCC(e, i, p.getParam(e, i), out + n);
            n += encodeEnableCC(e, p.isEnabled(e), out + n);
        }
        n += encodeOrder(p.order(), out + n);
        n += encodeTempo(p.tempo(), out + n);
        for (int s = 0; s < 2; ++s) { n += encodeCut(false, s, p.hpFreq(s), out + n); n += encodeCut(true, s, p.lpFreq(s), out + n); }
        out[n++] = 0xB0; out[n++] = 70; out[n++] = static_cast<uint8_t>(p.globalHp());
        out[n++] = 0xB0; out[n++] = 71; out[n++] = static_cast<uint8_t>(p.globalLp());
        return n;
    }

    bool stateRequested() { const bool r = stateReq_; stateReq_ = false; return r; }

private:
    void controlChange(uint8_t cc, uint8_t val) {
        if (cc < 32) {                       // MSB: apply immediately at 7-bit resolution, remember for LSB
            msb_[cc] = val;
            pedal_.setParam(cc / 8, cc % 8, val / 127.f);
        } else if (cc < 64) {                // LSB: refine to 14 bits
            const uint8_t m = msb_[cc - 32];
            pedal_.setParam((cc - 32) / 8, (cc - 32) % 8, ((m << 7) | val) / 16383.f);
        } else if (cc < 68) {
            pedal_.setEnabled(cc - 64, val >= 64);
        } else switch (cc) {
            case 70: pedal_.setGlobalHp(val); break;
            case 71: pedal_.setGlobalLp(val); break;
            case 72: pedal_.tap(pedal_.samplePosition()); break;
            case 74: pedal_.setInputTrimDb(-12.f + 24.f * val / 127.f); break;
            case 75: pedal_.setOutputTrimDb(-12.f + 24.f * val / 127.f); break;
            default: break;
        }
    }
    void programChange(uint8_t n) { pedal_.requestPreset(false, n); }
    void handleSysex() {
        if (sxLen_ < 2 || sx_[0] != 0x7D) return;
        const uint8_t cmd = sx_[1];
        switch (cmd) {
            case 0x01: if (sxLen_ >= 6) pedal_.setOrder(&sx_[2]); break;
            case 0x02: if (sxLen_ >= 3) pedal_.requestPreset(true, sx_[2]); break;
            case 0x03: if (sxLen_ >= 3) pedal_.requestPreset(false, sx_[2]); break;
            case 0x04: if (sxLen_ >= 5) pedal_.setHpFreq(sx_[2], static_cast<float>((sx_[3] << 7) | sx_[4])); break;
            case 0x05: if (sxLen_ >= 5) pedal_.setLpFreq(sx_[2], static_cast<float>((sx_[3] << 7) | sx_[4])); break;
            case 0x06: if (sxLen_ >= 4) pedal_.setTempo(static_cast<float>((sx_[2] << 7) | sx_[3]) * 0.001f); break;
            case 0x07: stateReq_ = true; break;
            default: break;
        }
    }

    static constexpr int kSysexMax = 64;
    Pedal& pedal_;
    uint8_t status_ = 0; int d1_ = -1;
    bool inSysex_ = false; uint8_t sx_[kSysexMax] = {}; int sxLen_ = 0;
    uint8_t msb_[32] = {};
    bool stateReq_ = false;
};

} // namespace pedal
