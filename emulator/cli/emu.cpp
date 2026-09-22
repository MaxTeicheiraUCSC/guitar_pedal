// emu — offline pedal emulator.
//
//   emu --in in.wav --out out.wav [options]
//     --preset file.preset       load a text preset (see preset_file.h)
//     --save-preset file         write the effective settings after --set/--enable/--order
//     --set fxN.Param=value      set a parameter in real units (e.g. --set fx1.Time=500)
//     --enable N[,N...]          enable effects (0 trem, 1 delay, 2 reverb, 3 fuzz); others off
//     --order a,b,c,d            chain order
//     --bpm N                    tempo for subdivisions
//     --hp 0|1|2  --lp 0|1|2     global cut toggles
//     --mono                     treat input as a TS plug (dual-mono)
//     --block N                  block size (default 48)
//     --hw                       route through the hardware model (input stage -> codec -> DSP -> DAC -> output stage)
//     --stage in|out             analog-only: in = volts at jack -> volts at codec pin; out = DAC float -> volts at jack
//     --in-gain-v X              scale input samples to volts (default 1.0, i.e. the WAV already holds volts)
//     --hwparam name=value       override an AnalogParams field (see hwmodel/analog_model.h), repeatable
//     --cpu                      print per-effect host cost as a fraction of real time
//     --list                     print parameter tables and exit
#include "pedal/pedal.h"
#include "pedal/midi.h"
#include "hwmodel/analog_model.h"
#include "wav.h"
#include "preset_file.h"
#include "host_alloc.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <chrono>
#include <algorithm>

static void listParams(pedal::Pedal& p) {
    for (int e = 0; e < pedal::kNumEffects; ++e) {
        std::printf("fx%d %s\n", e, p.effect(e)->name());
        for (int i = 0; i < p.effect(e)->numParams(); ++i) {
            const auto& d = p.effect(e)->paramDesc(i);
            std::printf("   %-12s %8g .. %-8g default %-8g %s  (CC %d)\n", d.name, d.min, d.max, d.def, d.unit, e * 8 + i);
        }
    }
}

int main(int argc, char** argv) {
    std::string inPath, outPath, presetPath, savePath, stage;
    std::vector<std::string> sets, hwparams; std::string enable, order;
    float bpm = 0.f; int hp = -1, lp = -1, block = 48; bool mono = false, hwOn = false, cpu = false, list = false;
    float inGainV = 1.f;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() -> std::string { return (i + 1 < argc) ? argv[++i] : ""; };
        if (a == "--in") inPath = next(); else if (a == "--out") outPath = next();
        else if (a == "--preset") presetPath = next(); else if (a == "--save-preset") savePath = next();
        else if (a == "--set") sets.push_back(next()); else if (a == "--enable") enable = next();
        else if (a == "--order") order = next(); else if (a == "--bpm") bpm = std::atof(next().c_str());
        else if (a == "--hp") hp = std::atoi(next().c_str()); else if (a == "--lp") lp = std::atoi(next().c_str());
        else if (a == "--block") block = std::atoi(next().c_str()); else if (a == "--mono") mono = true;
        else if (a == "--hw") hwOn = true; else if (a == "--stage") stage = next();
        else if (a == "--in-gain-v") inGainV = std::atof(next().c_str());
        else if (a == "--hwparam") hwparams.push_back(next());
        else if (a == "--cpu") cpu = true; else if (a == "--list") list = true;
        else { std::fprintf(stderr, "unknown option %s\n", a.c_str()); return 2; }
    }
    if (block < 1 || block > pedal::kMaxBlock) { std::fprintf(stderr, "block must be 1..%d\n", pedal::kMaxBlock); return 2; }

    wav::Audio in;
    if (!list && !wav::read(inPath, in)) { std::fprintf(stderr, "cannot read %s\n", inPath.c_str()); return 1; }

    HostAllocator mem;
    pedal::Config cfg; cfg.sampleRate = list ? 48000.f : static_cast<float>(in.sampleRate); cfg.blockSize = block; cfg.mem = &mem;
    pedal::Pedal p;
    if (!p.init(cfg)) { std::fprintf(stderr, "init failed\n"); return 1; }
    if (list) { listParams(p); std::printf("arena: %zu bytes\n", mem.totalBytes()); return 0; }

    if (!presetPath.empty() && !presetfile::load(presetPath, p)) { std::fprintf(stderr, "cannot load preset %s\n", presetPath.c_str()); return 1; }
    if (!enable.empty()) {
        for (int e = 0; e < pedal::kNumEffects; ++e) p.setEnabled(e, false);
        for (size_t pos = 0; pos < enable.size();) { p.setEnabled(std::atoi(enable.c_str() + pos), true); pos = enable.find(',', pos); if (pos == std::string::npos) break; ++pos; }
    }
    if (!order.empty()) { int o[4]; if (std::sscanf(order.c_str(), "%d,%d,%d,%d", &o[0], &o[1], &o[2], &o[3]) != 4) return 2; uint8_t u[4] = { (uint8_t)o[0], (uint8_t)o[1], (uint8_t)o[2], (uint8_t)o[3] }; if (!p.setOrder(u)) { std::fprintf(stderr, "bad order\n"); return 2; } }
    if (bpm > 0.f) p.setTempo(60.f / bpm);
    if (hp >= 0) p.setGlobalHp(hp); if (lp >= 0) p.setGlobalLp(lp);
    for (const auto& s : sets) {
        const size_t eq = s.find('='); if (eq == std::string::npos || s.size() < 5 || s[0] != 'f' || s[3] != '.') { std::fprintf(stderr, "bad --set %s\n", s.c_str()); return 2; }
        const int e = s[2] - '0'; const std::string pn = s.substr(4, eq - 4); const float v = std::atof(s.c_str() + eq + 1);
        bool found = false;
        for (int i = 0; i < p.effect(e)->numParams(); ++i) if (pn == p.effect(e)->paramDesc(i).name) { p.effect(e)->setParamReal(i, v); found = true; }
        if (!found) { std::fprintf(stderr, "unknown param %s\n", s.c_str()); return 2; }
    }
    if (!savePath.empty()) presetfile::save(savePath, p);
    p.setStereoInput(!mono && in.channels >= 2);

    hw::AnalogParams ap;
    for (const auto& s : hwparams) {
        const size_t eq = s.find('='); if (eq == std::string::npos || !hw::setAnalogParam(ap, s.substr(0, eq).c_str(), std::atof(s.c_str() + eq + 1))) { std::fprintf(stderr, "bad --hwparam %s\n", s.c_str()); return 2; }
    }
    // ---- analog-only stages (for the SPICE comparison harness) ----
    if (!stage.empty()) { wav::Audio out; out.sampleRate = in.sampleRate; out.channels = in.channels; out.data.resize(in.data.size());
        std::vector<hw::InputStage> ins(in.channels); std::vector<hw::OutputStage> outs(in.channels);
        for (int c = 0; c < in.channels; ++c) { ins[c].init(cfg.sampleRate, ap); outs[c].init(cfg.sampleRate, ap); }
        for (size_t n = 0; n < in.frames(); ++n) for (int c = 0; c < in.channels; ++c) {
            const float x = in.data[n * in.channels + c] * inGainV;
            out.data[n * in.channels + c] = (stage == "in") ? ins[c].process(x) : outs[c].process(x);
        }
        return wav::write(outPath, out) ? 0 : 1;
    }

    // ---- full pedal ----
    hw::InputStage inL, inR; hw::OutputStage outL, outR;
    if (hwOn) { inL.init(cfg.sampleRate, ap); inR.init(cfg.sampleRate, ap); outL.init(cfg.sampleRate, ap); outR.init(cfg.sampleRate, ap); }

    wav::Audio out; out.sampleRate = in.sampleRate; out.channels = 2; out.data.resize(in.frames() * 2);
    std::vector<pedal::Frame> buf(block);
    double dspSeconds = 0.0;
    for (size_t pos = 0; pos < in.frames(); pos += block) {
        const int n = static_cast<int>(std::min<size_t>(block, in.frames() - pos));
        for (int i = 0; i < n; ++i) {
            float l = in.data[(pos + i) * in.channels], r = in.channels > 1 ? in.data[(pos + i) * in.channels + 1] : l;
            if (hwOn) { l = inL.toNormalized(inL.process(l * inGainV)); r = inR.toNormalized(inR.process(r * inGainV)); }
            buf[i] = { l, r };
        }
        const auto t0 = std::chrono::steady_clock::now();
        p.process(buf.data(), n);
        dspSeconds += std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        for (int i = 0; i < n; ++i) {
            float l = buf[i].l, r = buf[i].r;
            if (hwOn) { l = outL.process(l); r = outR.process(r); }
            out.data[(pos + i) * 2] = l; out.data[(pos + i) * 2 + 1] = r;
        }
    }
    if (cpu) std::printf("host DSP load: %.2f%% of real time (%zu frames @ %d Hz)\n", 100.0 * dspSeconds / (in.frames() / cfg.sampleRate), in.frames(), in.sampleRate);
    return wav::write(outPath, out) ? 0 : 1;
}
