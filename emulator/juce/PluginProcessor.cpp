#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "BinaryData.h"

PedalProcessor::PedalProcessor()
    : AudioProcessor(BusesProperties().withInput("Input", juce::AudioChannelSet::stereo(), true).withOutput("Output", juce::AudioChannelSet::stereo(), true)) {
    pedal::Config cfg; cfg.sampleRate = 48000.f; cfg.blockSize = 48; cfg.mem = &mem_;
    pedal_.init(cfg);
    static const char* fxIds[] = { "trem", "delay", "reverb", "fuzz" };
    for (int e = 0; e < pedal::kNumEffects; ++e) {
        auto* fx = pedal_.effect(e);
        for (int p = 0; p < pedal::kMaxParams; ++p) {
            const bool real = p < fx->numParams();
            const juce::String id = juce::String(fxIds[e]) + "_" + (real ? juce::String(fx->paramDesc(p).name) : juce::String("p") + juce::String(p));
            const float def = real ? fx->getParamNorm(p) : 0.f;
            params_[e][p] = new juce::AudioParameterFloat(juce::ParameterID(id, 1), juce::String(fx->name()) + " " + (real ? fx->paramDesc(p).name : "-"), 0.f, 1.f, def);
            addParameter(params_[e][p]); lastParam_[e][p] = def;
        }
        enables_[e] = new juce::AudioParameterBool(juce::ParameterID(juce::String(fxIds[e]) + "_on", 1), juce::String(fx->name()) + " On", true);
        addParameter(enables_[e]); lastEnable_[e] = true; pedal_.setEnabled(e, true, false);   // emulator starts with everything on
    }
    pedal_.setListener(this);
    formats_.registerBasicFormats();
    loadEmbeddedLoop();
    loadNoteSamples();
}

void PedalProcessor::loadNoteSamples() {
    for (int i = 0; i < BinaryData::namedResourceListSize; ++i) {
        const juce::String name = BinaryData::namedResourceList[i];
        if (!name.startsWith("note_")) continue;
        const int midi = name.fromFirstOccurrenceOf("note_", false, false).upToFirstOccurrenceOf("_", false, false).getIntValue();
        int size = 0; const char* data = BinaryData::getNamedResource(BinaryData::namedResourceList[i], size);
        sampler_.addSample(midi, std::unique_ptr<juce::AudioFormatReader>(formats_.createReaderFor(std::make_unique<juce::MemoryInputStream>(data, size, false))));
    }
}

bool PedalProcessor::setLoop(std::unique_ptr<juce::AudioFormatReader> reader, const juce::String& name) {
    if (!reader || reader->lengthInSamples <= 0) return false;
    juce::AudioBuffer<float> buf(static_cast<int>(reader->numChannels), static_cast<int>(reader->lengthInSamples));
    reader->read(&buf, 0, buf.getNumSamples(), 0, true, true);
    const juce::SpinLock::ScopedLockType lock(loopLock_);   // audio thread uses tryEnter; a block may skip during the swap
    loop_ = std::move(buf); loopRate_ = reader->sampleRate; loopPos_ = 0.0; loopName_ = name;
    return true;
}
void PedalProcessor::loadEmbeddedLoop(int which) {
    const char* data = which == 1 ? BinaryData::test_loop_wav : which == 2 ? BinaryData::test_emaj7_arp_wav : BinaryData::test_e_gsharp_wav;
    const int size = which == 1 ? BinaryData::test_loop_wavSize : which == 2 ? BinaryData::test_emaj7_arp_wavSize : BinaryData::test_e_gsharp_wavSize;
    const char* name = which == 1 ? "test_loop.wav (chords, CC0)" : which == 2 ? "Emaj7 arpeggio (CC0)" : "E / G# every 5 s (CC0)";
    auto stream = std::make_unique<juce::MemoryInputStream>(data, size, false);
    setLoop(std::unique_ptr<juce::AudioFormatReader>(formats_.createReaderFor(std::move(stream))), name);
}
bool PedalProcessor::loadLoopFile(const juce::File& f) {
    return setLoop(std::unique_ptr<juce::AudioFormatReader>(formats_.createReaderFor(f)), f.getFileName());
}

void PedalProcessor::prepareToPlay(double sampleRate, int) {
    pedal::Config cfg; cfg.sampleRate = static_cast<float>(sampleRate); cfg.blockSize = 48; cfg.mem = &mem_;
    pedal::Preset keep; pedal_.toPreset(keep);
    pedal_.init(cfg); pedal_.applyPreset(keep);
    hw::AnalogParams ap;
    inL_.init(cfg.sampleRate, ap); inR_.init(cfg.sampleRate, ap); outL_.init(cfg.sampleRate, ap); outR_.init(cfg.sampleRate, ap);
    block_.resize(pedal::kMaxBlock);
    sampler_.prepare(sampleRate);
}

void PedalProcessor::onParamChanged(int e, int p, float n) {   // called from pedal_ on MIDI/preset changes: mirror into host params
    if (mirroring_) return; mirroring_ = true;
    if (params_[e][p] && std::fabs(params_[e][p]->get() - n) > 1e-5f) { params_[e][p]->setValueNotifyingHost(n); lastParam_[e][p] = n; }
    mirroring_ = false;
}
void PedalProcessor::onEnabledChanged(int e, bool on) {
    if (mirroring_) return; mirroring_ = true;
    if (enables_[e] && enables_[e]->get() != on) { enables_[e]->setValueNotifyingHost(on ? 1.f : 0.f); lastEnable_[e] = on; }
    mirroring_ = false;
}

void PedalProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) {
    juce::ScopedNoDenormals noDenormals;
    const auto t0 = juce::Time::getHighResolutionTicks();
    // host parameters -> pedal (only on change, so MIDI/preset changes are not overwritten)
    for (int e = 0; e < pedal::kNumEffects; ++e) {
        for (int p = 0; p < pedal_.effect(e)->numParams(); ++p) {
            const float v = params_[e][p]->get();
            if (std::fabs(v - lastParam_[e][p]) > 1e-6f) { lastParam_[e][p] = v; pedal_.setParam(e, p, v, false); }
        }
        const bool on = enables_[e]->get();
        if (on != lastEnable_[e]) { lastEnable_[e] = on; pedal_.setEnabled(e, on, false); }
    }
    keyboardState.processNextMidiBuffer(midi, 0, buffer.getNumSamples(), true);   // merge on-screen key presses
    for (const auto meta : midi) {
        const auto m = meta.getMessage();
        if (m.isNoteOn()) sampler_.noteOn(m.getNoteNumber(), m.getFloatVelocity());
        else if (m.isNoteOff()) sampler_.noteOff(m.getNoteNumber());
        else if (m.isAllNotesOff() || m.isAllSoundOff()) sampler_.allOff();
        else midi_.feed(m.getRawData(), m.getRawDataSize());
    }
    if (tapPending_.exchange(false)) pedal_.tap(pedal_.samplePosition());

    const int nIn = getTotalNumInputChannels();
    const int n = buffer.getNumSamples();
    if (buffer.getNumChannels() < 2) { buffer.clear(); return; }   // mono-only device: nothing sensible to do
    const bool hw = hwModel.load(); const float vpk = inputVpk.load();
    const float* inL = buffer.getReadPointer(0); const float* inR = nIn > 1 ? buffer.getReadPointer(1) : inL;
    float* outL = buffer.getWritePointer(0); float* outR = buffer.getWritePointer(1);
    bool stereo = nIn >= 2 && !forceMono.load();
    // ---- looping file source replaces the live input ----
    if (source.load() == Loop) {
        loopScratch_.setSize(2, n, false, false, true);
        loopScratch_.clear();
        const juce::SpinLock::ScopedTryLockType lock(loopLock_);
        if (lock.isLocked() && loop_.getNumSamples() > 1 && loopRate_ > 0.0) {
            const double ratio = loopRate_ / getSampleRate(); const int len = loop_.getNumSamples(); const float g = loopGain.load();
            const int lc = loop_.getNumChannels();
            for (int i = 0; i < n; ++i) {
                const int i0 = static_cast<int>(loopPos_); const float fr = static_cast<float>(loopPos_ - i0); const int i1 = (i0 + 1) % len;
                for (int c = 0; c < 2; ++c) { const float* s = loop_.getReadPointer(juce::jmin(c, lc - 1)); loopScratch_.setSample(c, i, (s[i0] + (s[i1] - s[i0]) * fr) * g); }
                loopPos_ += ratio; if (loopPos_ >= len) loopPos_ -= len;
            }
            stereo = lc >= 2 && !forceMono.load();
        }
        inL = loopScratch_.getReadPointer(0); inR = loopScratch_.getReadPointer(1);
    } else if (source.load() == Silence) {
        loopScratch_.setSize(2, n, false, false, true); loopScratch_.clear();
        inL = loopScratch_.getReadPointer(0); inR = loopScratch_.getReadPointer(1);
    }
    // keyboard sampler is mixed onto whatever the source is (mono into both channels)
    keysScratch_.setSize(2, n, false, false, true); keysScratch_.clear();
    sampler_.render(keysScratch_.getWritePointer(0), keysScratch_.getWritePointer(1), n);
    {
        const float g = keysGain.load();
        for (int c = 0; c < 2; ++c) { float* dst = keysScratch_.getWritePointer(c); const float* src = c == 0 ? inL : inR; for (int i = 0; i < n; ++i) dst[i] = src[i] + dst[i] * g; }
        inL = keysScratch_.getReadPointer(0); inR = keysScratch_.getReadPointer(1);
    }
    pedal_.setStereoInput(stereo);
    for (int pos = 0; pos < n; pos += pedal::kMaxBlock) {
        const int len = std::min(pedal::kMaxBlock, n - pos);
        for (int i = 0; i < len; ++i) {
            float l = inL[pos + i], r = inR[pos + i];
            if (hw) { l = inL_.toNormalized(inL_.process(l * vpk)); r = inR_.toNormalized(inR_.process(r * vpk)); }
            block_[i] = { l, r };
        }
        pedal_.process(block_.data(), len);
        for (int i = 0; i < len; ++i) {
            float l = block_[i].l, r = block_[i].r;
            if (hw) { l = outL_.process(l) / 3.1f; r = outR_.process(r) / 3.1f; }   // volts at the jack -> ~full scale
            outL[pos + i] = l; outR[pos + i] = r;
        }
    }
    const double secs = juce::Time::highResolutionTicksToSeconds(juce::Time::getHighResolutionTicks() - t0);
    loadPercent.store(static_cast<float>(100.0 * secs / (n / getSampleRate())));
}

void PedalProcessor::getStateInformation(juce::MemoryBlock& dest) { pedal::Preset p; pedal_.toPreset(p); dest.append(&p, sizeof p); }
void PedalProcessor::setStateInformation(const void* data, int size) {
    if (size != static_cast<int>(sizeof(pedal::Preset))) return;
    pedal::Preset p; std::memcpy(&p, data, sizeof p);
    if (pedal_.applyPreset(p)) for (int e = 0; e < pedal::kNumEffects; ++e) { for (int i = 0; i < pedal::kMaxParams; ++i) onParamChanged(e, i, pedal_.getParam(e, i)); onEnabledChanged(e, pedal_.isEnabled(e)); }
}
bool PedalProcessor::loadPresetFile(const juce::File& f) {
    if (!presetfile::load(f.getFullPathName().toStdString(), pedal_)) return false;
    for (int e = 0; e < pedal::kNumEffects; ++e) { for (int i = 0; i < pedal::kMaxParams; ++i) onParamChanged(e, i, pedal_.getParam(e, i)); onEnabledChanged(e, pedal_.isEnabled(e)); }
    return true;
}
bool PedalProcessor::savePresetFile(const juce::File& f) { return presetfile::save(f.getFullPathName().toStdString(), pedal_); }

juce::AudioProcessorEditor* PedalProcessor::createEditor() { return new PedalEditor(*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new PedalProcessor(); }
