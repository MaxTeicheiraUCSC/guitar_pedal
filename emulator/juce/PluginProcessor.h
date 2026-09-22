#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "pedal/pedal.h"
#include "pedal/midi.h"
#include "hwmodel/analog_model.h"
#include "cli/host_alloc.h"
#include "cli/preset_file.h"
#include "Sampler.h"

// The pedal as an AudioProcessor. Effect parameters are exposed as host
// parameters (normalised, same 0..1 domain as the firmware / MIDI map) so a DAW
// can automate them; everything else (order, cuts, tempo) is processor state.
class PedalProcessor final : public juce::AudioProcessor, private pedal::PedalListener {
public:
    PedalProcessor();
    ~PedalProcessor() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    bool isBusesLayoutSupported(const BusesLayout& l) const override {
        return l.getMainOutputChannelSet() == juce::AudioChannelSet::stereo()
            && (l.getMainInputChannelSet() == juce::AudioChannelSet::stereo() || l.getMainInputChannelSet() == juce::AudioChannelSet::mono());
    }
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "PedalEmu"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 6.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return "Live"; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock& dest) override;
    void setStateInformation(const void* data, int size) override;

    // ---- accessed by the editor (message thread) ----
    pedal::Pedal& pedal() { return pedal_; }
    juce::AudioParameterFloat* param(int effect, int p) { return params_[effect][p]; }
    juce::AudioParameterBool*  enableParam(int effect) { return enables_[effect]; }
    // ---- input source: live audio or a looping file (default: the embedded CC0 test loop) ----
    enum Source { Live = 0, Loop = 1, Silence = 2 };   // keyboard notes are always mixed on top of the source
    std::atomic<int>   source { Loop };
    std::atomic<float> loopGain { 1.f };
    bool loadLoopFile(const juce::File& f);      // any format JUCE reads; message thread
    void loadEmbeddedLoop(int which = 0);     // 0: E/G# 5 s alternation (default), 1: chord-progression loop, 2: Emaj7 arpeggio
    juce::String loopName() const { return loopName_; }
    double loopLengthSeconds() const { return loopRate_ > 0 ? loop_.getNumSamples() / loopRate_ : 0.0; }
    juce::MidiKeyboardState keyboardState;       // on-screen keys + external MIDI notes -> sampler
    std::atomic<float> keysGain { 1.f };
    std::atomic<bool> hwModel { true };
    std::atomic<bool> forceMono { false };
    std::atomic<float> inputVpk { 1.0f };      // scales normalised input to volts at the jack when hwModel is on
    std::atomic<float> loadPercent { 0.f };    // DSP time / block time
    void requestTap() { tapPending_ = true; }
    bool loadPresetFile(const juce::File& f);
    bool savePresetFile(const juce::File& f);

private:
    void onParamChanged(int e, int p, float n) override;   // pedal -> host parameter mirror
    void onEnabledChanged(int e, bool on) override;

    HostAllocator mem_;
    pedal::Pedal pedal_;
    pedal::MidiParser midi_ { pedal_ };
    juce::AudioParameterFloat* params_[pedal::kNumEffects][pedal::kMaxParams] = {};
    juce::AudioParameterBool*  enables_[pedal::kNumEffects] = {};
    float lastParam_[pedal::kNumEffects][pedal::kMaxParams] = {};
    bool  lastEnable_[pedal::kNumEffects] = {};
    hw::InputStage inL_, inR_; hw::OutputStage outL_, outR_;
    std::vector<pedal::Frame> block_;
    std::atomic<bool> tapPending_ { false };
    bool setLoop(std::unique_ptr<juce::AudioFormatReader> reader, const juce::String& name);
    juce::AudioFormatManager formats_;
    juce::AudioBuffer<float> loop_;             // file-rate samples
    double loopRate_ = 0.0, loopPos_ = 0.0;
    juce::String loopName_;
    juce::SpinLock loopLock_;
    juce::AudioBuffer<float> loopScratch_;
    Sampler sampler_;
    juce::AudioBuffer<float> keysScratch_;
    void loadNoteSamples();
    bool mirroring_ = false;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PedalProcessor)
};
