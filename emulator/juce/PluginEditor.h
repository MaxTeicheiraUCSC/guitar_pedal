#pragma once
#include "PluginProcessor.h"

// Virtual front panel laid out like the enclosure: 6 shared knobs (bank select
// by holding TAP + effect switch, or right-click a switch), 4 effect footswitches
// + TAP, two 3-position HP/LP toggles, chain order strip, LEDs.
// Knobs emulate absolute pots: they keep their position when the bank changes and
// only take over a parameter after passing through its current value.
class PedalEditor final : public juce::AudioProcessorEditor, private juce::Timer {
public:
    explicit PedalEditor(PedalProcessor&);
    ~PedalEditor() override = default;
    void paint(juce::Graphics&) override;
    void resized() override;
    // test hooks (used by Harness.cpp)
    void testSetKnob(int k, float v) { knobs_[k].setValue(v, juce::sendNotificationSync); }
    void testClickEffect(int e) { fxSwitch_[e].triggerClick(); }
    void testSelectBank(int e) { selectBank(e); }
    void testMoveEffect(int slot, int dir) { moveEffect(slot, dir); }
    void testSetCuts(int hp, int lp, float hpHz, float lpHz) { hpBox_.setSelectedId(hp + 1, juce::sendNotificationSync); lpBox_.setSelectedId(lp + 1, juce::sendNotificationSync); hpA_.setValue(hpHz, juce::sendNotificationSync); lpB_.setValue(lpHz, juce::sendNotificationSync); }
    void testTap() { proc_.requestTap(); }
    void testToggleHw() { hwToggle_.setToggleState(!hwToggle_.getToggleState(), juce::sendNotificationSync); }
private:
    void timerCallback() override;
    void selectBank(int effect);
    void knobMoved(int k);
    void refreshKnobLabels();
    void moveEffect(int slot, int dir);

    PedalProcessor& proc_;
    pedal::Pedal& pedal_;
    static constexpr int kKnobs = 6;
    juce::Slider knobs_[kKnobs];
    juce::Label knobLabels_[kKnobs], knobValues_[kKnobs];
    hw::PotModel pots_[kKnobs];
    bool caught_[kKnobs] = {};
    int bank_ = 3;   // effect the knobs edit (fuzz first)
    juce::TextButton fxSwitch_[pedal::kNumEffects], tap_ { "TAP" }, loadBtn_ { "Load preset" }, saveBtn_ { "Save preset" };
    juce::ToggleButton hwToggle_ { "Analog + codec model" }, monoToggle_ { "TS plug (mono)" };
    juce::ComboBox hpBox_, lpBox_, sourceBox_;
    juce::TextButton loopBtn_ { "Load loop file..." };
    juce::Slider loopGain_;
    juce::Label sourceLabel_;
    juce::Slider hpA_, hpB_, lpA_, lpB_, inLevel_;
    juce::Label bankLabel_, loadLabel_, orderLabel_[pedal::kNumEffects], tempoLabel_, inLevelLabel_, bpmLabel_;
    juce::TextEditor bpmBox_;
    juce::MidiKeyboardComponent keyboard_;
    juce::Slider keysGain_;
    juce::Label keysLabel_;
    juce::TextButton orderLeft_[pedal::kNumEffects], orderRight_[pedal::kNumEffects];
    bool tapHeld_ = false, selectMode_ = false; juce::int64 tapDownMs_ = 0;
    int blink_ = 0;
    // repaint only what changed: LED rectangles and their last drawn state
    juce::Rectangle<int> ledRect_[pedal::kNumEffects], tapLedRect_;
    int ledDrawn_[pedal::kNumEffects] = { -1, -1, -1, -1 }; int tapLedDrawn_ = -1; int bankDrawn_ = -1;
    std::unique_ptr<juce::FileChooser> chooser_;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PedalEditor)
};
