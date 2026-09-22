// Text preset format (key=value), shared by the CLI and the JUCE app.
//   name=Lead
//   order=3,0,1,2
//   fx0.enabled=1
//   fx0.Rate=4.5            (real units, by parameter name)
//   hp=1  lp=0  hpA=80 hpB=160 lpA=8000 lpB=4000  bpm=120  in_db=0 out_db=0
#pragma once
#include "pedal/pedal.h"
#include <cstdio>
#include <string>
#include <map>
#include <cstring>

namespace presetfile {

inline bool load(const std::string& path, pedal::Pedal& p) {
    FILE* f = std::fopen(path.c_str(), "r");
    if (!f) return false;
    char line[256];
    pedal::Preset pr; p.toPreset(pr);
    while (std::fgets(line, sizeof line, f)) {
        char* eq = std::strchr(line, '='); if (!eq || line[0] == '#') continue;
        *eq = 0; std::string k = line, v = eq + 1;
        while (!v.empty() && (v.back() == '\n' || v.back() == '\r' || v.back() == ' ')) v.pop_back();
        if (k == "name") { std::strncpy(pr.name, v.c_str(), 15); pr.name[15] = 0; }
        else if (k == "order") { int o[4]; if (std::sscanf(v.c_str(), "%d,%d,%d,%d", &o[0], &o[1], &o[2], &o[3]) == 4) for (int i = 0; i < 4; ++i) pr.order[i] = static_cast<uint8_t>(o[i]); }
        else if (k == "hp") pr.globalHp = static_cast<uint8_t>(std::atoi(v.c_str()));
        else if (k == "lp") pr.globalLp = static_cast<uint8_t>(std::atoi(v.c_str()));
        else if (k == "hpA") pr.hpFreq[0] = std::atof(v.c_str()); else if (k == "hpB") pr.hpFreq[1] = std::atof(v.c_str());
        else if (k == "lpA") pr.lpFreq[0] = std::atof(v.c_str()); else if (k == "lpB") pr.lpFreq[1] = std::atof(v.c_str());
        else if (k == "bpm") pr.tempoSpb = 60.f / std::atof(v.c_str());
        else if (k == "in_db") pr.inputTrimDb = std::atof(v.c_str()); else if (k == "out_db") pr.outputTrimDb = std::atof(v.c_str());
        else if (k.rfind("fx", 0) == 0 && k.size() > 4 && k[3] == '.') {
            const int e = k[2] - '0'; if (e < 0 || e >= pedal::kNumEffects) continue;
            const std::string pn = k.substr(4);
            if (pn == "enabled") pr.enabled[e] = std::atoi(v.c_str()) ? 1 : 0;
            else for (int i = 0; i < p.effect(e)->numParams(); ++i)
                if (pn == p.effect(e)->paramDesc(i).name) pr.params[e][i] = p.effect(e)->paramDesc(i).toNorm(std::atof(v.c_str()));
        }
    }
    std::fclose(f);
    return p.applyPreset(pr);
}

inline bool save(const std::string& path, pedal::Pedal& p) {
    FILE* f = std::fopen(path.c_str(), "w");
    if (!f) return false;
    pedal::Preset pr; p.toPreset(pr);
    std::fprintf(f, "name=%s\norder=%d,%d,%d,%d\n", pr.name, pr.order[0], pr.order[1], pr.order[2], pr.order[3]);
    for (int e = 0; e < pedal::kNumEffects; ++e) {
        std::fprintf(f, "fx%d.enabled=%d\n", e, pr.enabled[e]);
        for (int i = 0; i < p.effect(e)->numParams(); ++i) std::fprintf(f, "fx%d.%s=%g\n", e, p.effect(e)->paramDesc(i).name, p.effect(e)->getParamReal(i));
    }
    std::fprintf(f, "hp=%d\nlp=%d\nhpA=%g\nhpB=%g\nlpA=%g\nlpB=%g\nbpm=%g\nin_db=%g\nout_db=%g\n",
                 pr.globalHp, pr.globalLp, pr.hpFreq[0], pr.hpFreq[1], pr.lpFreq[0], pr.lpFreq[1], 60.f / pr.tempoSpb, pr.inputTrimDb, pr.outputTrimDb);
    std::fclose(f); return true;
}

} // namespace presetfile
