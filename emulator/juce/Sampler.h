#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include <cmath>

// Plucked-guitar sampler for the on-screen keyboard: nearest recorded note, pitch-shifted by
// resampling, natural decay, 150 ms release on note-off. 8 voices. Audio-thread only.
class Sampler {
public:
    struct Sample { int midi; juce::AudioBuffer<float> data; double rate; };
    void addSample(int midi, std::unique_ptr<juce::AudioFormatReader> r) {
        if (!r) return;
        Sample s; s.midi = midi; s.rate = r->sampleRate; s.data.setSize(1, static_cast<int>(r->lengthInSamples));
        r->read(&s.data, 0, s.data.getNumSamples(), 0, true, false); samples_.push_back(std::move(s));
    }
    void prepare(double hostRate) { hostRate_ = hostRate; for (auto& v : voices_) v.active = false; }
    void noteOn(int midi, float vel) {
        if (samples_.empty()) return;
        const Sample* best = &samples_[0];
        for (auto& s : samples_) if (std::abs(s.midi - midi) < std::abs(best->midi - midi)) best = &s;
        Voice* v = nullptr; for (auto& c : voices_) if (!c.active) { v = &c; break; }
        if (!v) { v = &voices_[0]; for (auto& c : voices_) if (c.pos > v->pos) v = &c; }   // steal the oldest
        v->s = best; v->pos = 0.0; v->ratio = std::pow(2.0, (midi - best->midi) / 12.0) * best->rate / hostRate_;
        v->gain = 0.2f + 0.8f * vel; v->rel = 1.0f; v->releasing = false; v->midi = midi; v->active = true;
    }
    void noteOff(int midi) { for (auto& v : voices_) if (v.active && v.midi == midi) v.releasing = true; }
    void allOff() { for (auto& v : voices_) v.releasing = true; }
    // adds into l/r
    void render(float* l, float* r, int n) {
        const float relStep = static_cast<float>(1.0 / (0.15 * hostRate_));
        for (auto& v : voices_) {
            if (!v.active) continue;
            const float* d = v.s->data.getReadPointer(0); const int len = v.s->data.getNumSamples();
            for (int i = 0; i < n; ++i) {
                const int i0 = static_cast<int>(v.pos); if (i0 + 1 >= len) { v.active = false; break; }
                const float fr = static_cast<float>(v.pos - i0); const float x = (d[i0] + (d[i0 + 1] - d[i0]) * fr) * v.gain * v.rel;
                l[i] += x; r[i] += x; v.pos += v.ratio;
                if (v.releasing) { v.rel -= relStep; if (v.rel <= 0.f) { v.active = false; break; } }
            }
        }
    }
private:
    struct Voice { const Sample* s = nullptr; double pos = 0, ratio = 1; float gain = 1, rel = 1; bool releasing = false, active = false; int midi = 0; };
    std::vector<Sample> samples_; Voice voices_[8]; double hostRate_ = 48000.0;
};
