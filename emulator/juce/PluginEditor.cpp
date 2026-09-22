#include "PluginEditor.h"

static const juce::Colour kPanel(0xff2b2f36), kLedOn(0xff5ee07a), kLedOff(0xff2a4a30), kCaught(0xff5ee07a), kWaiting(0xffe0a640);

PedalEditor::PedalEditor(PedalProcessor& p) : AudioProcessorEditor(&p), proc_(p), pedal_(p.pedal()), keyboard_(p.keyboardState, juce::MidiKeyboardComponent::horizontalKeyboard) {
    setOpaque(true);
    setSize(760, 660);
    // tempo entry
    bpmLabel_.setText("tempo", juce::dontSendNotification); addAndMakeVisible(bpmLabel_);
    bpmBox_.setTooltip("beats per minute: type a value and press Enter (sets delay subdivisions / tremolo sync)");
    bpmBox_.setInputRestrictions(6, "0123456789."); bpmBox_.setText(juce::String(60.f / pedal_.tempo(), 1), juce::dontSendNotification);
    bpmBox_.onReturnKey = [this] { const float bpm = bpmBox_.getText().getFloatValue(); if (bpm >= 20.f && bpm <= 300.f) pedal_.setTempo(60.f / bpm); bpmBox_.giveAwayKeyboardFocus(); };
    bpmBox_.onFocusLost = [this] { bpmBox_.setText(juce::String(60.f / pedal_.tempo(), 1), juce::dontSendNotification); };
    addAndMakeVisible(bpmBox_);
    // on-screen keyboard (guitar range E2..E6), mixed onto the source
    keyboard_.setAvailableRange(40, 88); keyboard_.setLowestVisibleKey(40); keyboard_.setKeyWidth(24.8f); keyboard_.setOctaveForMiddleC(4);
    keyboard_.setColour(juce::MidiKeyboardComponent::keyDownOverlayColourId, juce::Colour(0x805ee07a));
    addAndMakeVisible(keyboard_);
    keysGain_.setSliderStyle(juce::Slider::LinearHorizontal); keysGain_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 52, 18);
    keysGain_.setRange(-24.0, 12.0, 0.1); keysGain_.setValue(0.0); keysGain_.setTextValueSuffix(" dB");
    keysGain_.onValueChange = [this] { proc_.keysGain = juce::Decibels::decibelsToGain(static_cast<float>(keysGain_.getValue())); };
    addAndMakeVisible(keysGain_); keysLabel_.setText("keys (click or play a MIDI keyboard; A-K on the computer keyboard when the keys have focus)", juce::dontSendNotification); keysLabel_.setFont(juce::FontOptions(12.f)); addAndMakeVisible(keysLabel_);
    for (int k = 0; k < kKnobs; ++k) {
        auto& s = knobs_[k];
        s.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag); s.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        s.setRange(0.0, 1.0, 0.0); s.setValue(pedal_.getParam(bank_, k), juce::dontSendNotification);
        s.onValueChange = [this, k] { knobMoved(k); };
        addAndMakeVisible(s); addAndMakeVisible(knobLabels_[k]); addAndMakeVisible(knobValues_[k]);
        knobLabels_[k].setJustificationType(juce::Justification::centred); knobValues_[k].setJustificationType(juce::Justification::centred);
        knobValues_[k].setFont(juce::FontOptions(11.f));
        pots_[k].init(30.f); caught_[k] = true;
    }
    for (int e = 0; e < pedal::kNumEffects; ++e) {
        fxSwitch_[e].setButtonText(pedal_.effect(e)->name()); fxSwitch_[e].setClickingTogglesState(false);
        fxSwitch_[e].onClick = [this, e] {
            if (selectMode_ || juce::ModifierKeys::getCurrentModifiers().isPopupMenu()) { selectBank(e); return; }
            const bool turningOn = !pedal_.isEnabled(e);
            proc_.enableParam(e)->setValueNotifyingHost(turningOn ? 1.f : 0.f);
            if (turningOn && bank_ != e) selectBank(e);   // convenience: the knobs follow the effect you just switched on
        };
        addAndMakeVisible(fxSwitch_[e]);
        orderLeft_[e].setButtonText("<"); orderRight_[e].setButtonText(">");
        orderLeft_[e].onClick = [this, e] { moveEffect(e, -1); }; orderRight_[e].onClick = [this, e] { moveEffect(e, +1); };
        addAndMakeVisible(orderLeft_[e]); addAndMakeVisible(orderRight_[e]); addAndMakeVisible(orderLabel_[e]);
        orderLabel_[e].setJustificationType(juce::Justification::centred);
    }
    tap_.onStateChange = [this] {
        const bool down = tap_.isDown();
        if (down && !tapHeld_) { tapHeld_ = true; tapDownMs_ = juce::Time::currentTimeMillis(); }
        else if (!down && tapHeld_) { tapHeld_ = false; if (!selectMode_) proc_.requestTap(); selectMode_ = false; }
    };
    addAndMakeVisible(tap_);
    hwToggle_.setToggleState(true, juce::dontSendNotification); hwToggle_.onClick = [this] { proc_.hwModel = hwToggle_.getToggleState(); };
    monoToggle_.onClick = [this] { proc_.forceMono = monoToggle_.getToggleState(); };
    addAndMakeVisible(hwToggle_); addAndMakeVisible(monoToggle_);
    for (auto* b : { &hpBox_, &lpBox_ }) { b->addItemList({ "off", "A", "B" }, 1); b->setSelectedId(1, juce::dontSendNotification); addAndMakeVisible(*b); }
    hpBox_.onChange = [this] { pedal_.setGlobalHp(hpBox_.getSelectedId() - 1); };
    lpBox_.onChange = [this] { pedal_.setGlobalLp(lpBox_.getSelectedId() - 1); };
    auto freq = [this](juce::Slider& s, double lo, double hi, double v, std::function<void(float)> fn) {
        s.setSliderStyle(juce::Slider::LinearHorizontal); s.setTextBoxStyle(juce::Slider::TextBoxRight, false, 52, 18);
        s.setRange(lo, hi, 1.0); s.setSkewFactorFromMidPoint(std::sqrt(lo * hi)); s.setValue(v, juce::dontSendNotification); s.setTextValueSuffix(" Hz");
        s.onValueChange = [&s, fn] { fn(static_cast<float>(s.getValue())); }; addAndMakeVisible(s);
    };
    freq(hpA_, 20, 2000, pedal_.hpFreq(0), [this](float v) { pedal_.setHpFreq(0, v); }); freq(hpB_, 20, 2000, pedal_.hpFreq(1), [this](float v) { pedal_.setHpFreq(1, v); });
    freq(lpA_, 1000, 20000, pedal_.lpFreq(0), [this](float v) { pedal_.setLpFreq(0, v); }); freq(lpB_, 1000, 20000, pedal_.lpFreq(1), [this](float v) { pedal_.setLpFreq(1, v); });
    inLevel_.setSliderStyle(juce::Slider::LinearHorizontal); inLevel_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 52, 18);
    inLevel_.setRange(0.1, 6.0, 0.01); inLevel_.setValue(1.0); inLevel_.setTextValueSuffix(" Vpk"); inLevel_.onValueChange = [this] { proc_.inputVpk = static_cast<float>(inLevel_.getValue()); };
    addAndMakeVisible(inLevel_); inLevelLabel_.setText("input level at 0 dBFS", juce::dontSendNotification); addAndMakeVisible(inLevelLabel_);
    loadBtn_.onClick = [this] {
        chooser_ = std::make_unique<juce::FileChooser>("Load preset", juce::File(), "*.preset");
        chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this](const juce::FileChooser& fc) {
            if (fc.getResult().existsAsFile() && proc_.loadPresetFile(fc.getResult())) { hpBox_.setSelectedId(pedal_.globalHp() + 1, juce::dontSendNotification); lpBox_.setSelectedId(pedal_.globalLp() + 1, juce::dontSendNotification); refreshKnobLabels(); }
        });
    };
    saveBtn_.onClick = [this] {
        chooser_ = std::make_unique<juce::FileChooser>("Save preset", juce::File(), "*.preset");
        chooser_->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles, [this](const juce::FileChooser& fc) { if (fc.getResult() != juce::File()) proc_.savePresetFile(fc.getResult()); });
    };
    addAndMakeVisible(loadBtn_); addAndMakeVisible(saveBtn_);
    // ---- source: loop file / live input ----
    sourceBox_.addItemList({ "Live input", "Loop: E / G# 5 s", "Loop: chords", "Loop: Emaj7 arpeggio", "Loop: file...", "Keys only (no loop)" }, 1); sourceBox_.setSelectedId(2, juce::dontSendNotification);
    sourceBox_.onChange = [this] {
        const int id = sourceBox_.getSelectedId();
        if (id == 1) proc_.source = PedalProcessor::Live;
        else if (id == 2) { proc_.loadEmbeddedLoop(0); proc_.source = PedalProcessor::Loop; }
        else if (id == 3) { proc_.loadEmbeddedLoop(1); proc_.source = PedalProcessor::Loop; }
        else if (id == 4) { proc_.loadEmbeddedLoop(2); proc_.source = PedalProcessor::Loop; }
        else if (id == 6) proc_.source = PedalProcessor::Silence;
        else loopBtn_.triggerClick();
    };
    loopBtn_.onClick = [this] {
        chooser_ = std::make_unique<juce::FileChooser>("Load loop", juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg");
        chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this](const juce::FileChooser& fc) {
            if (fc.getResult().existsAsFile() && proc_.loadLoopFile(fc.getResult())) { proc_.source = PedalProcessor::Loop; sourceBox_.setSelectedId(5, juce::dontSendNotification); }
        });
    };
    loopGain_.setSliderStyle(juce::Slider::LinearHorizontal); loopGain_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 52, 18);
    loopGain_.setRange(-24.0, 12.0, 0.1); loopGain_.setValue(0.0); loopGain_.setTextValueSuffix(" dB");
    loopGain_.onValueChange = [this] { proc_.loopGain = juce::Decibels::decibelsToGain(static_cast<float>(loopGain_.getValue())); };
    addAndMakeVisible(sourceBox_); addAndMakeVisible(loopBtn_); addAndMakeVisible(loopGain_); addAndMakeVisible(sourceLabel_);
    sourceLabel_.setFont(juce::FontOptions(12.f));
    addAndMakeVisible(bankLabel_); addAndMakeVisible(loadLabel_); addAndMakeVisible(tempoLabel_);
    bankLabel_.setFont(juce::FontOptions(14.f, juce::Font::bold));
    refreshKnobLabels();
    startTimerHz(30);
}

void PedalEditor::selectBank(int e) {
    bank_ = e; selectMode_ = false;
    for (int k = 0; k < kKnobs; ++k) { pots_[k].release(); caught_[k] = false; }
    refreshKnobLabels();
}

void PedalEditor::refreshKnobLabels() {
    auto* fx = pedal_.effect(bank_);
    bankLabel_.setText(juce::String("Knobs: ") + fx->name() + "   (hold TAP + switch, or right-click a switch, to change)", juce::dontSendNotification);
    for (int k = 0; k < kKnobs; ++k) {
        const bool real = k < fx->numParams();
        knobLabels_[k].setText(real ? fx->paramDesc(k).name : "-", juce::dontSendNotification);
        knobs_[k].setEnabled(real);
    }
}

void PedalEditor::knobMoved(int k) {
    auto* fx = pedal_.effect(bank_);
    if (k >= fx->numParams()) return;
    const float pos = static_cast<float>(knobs_[k].getValue());
    const float cur = pedal_.getParam(bank_, k);
    if (!caught_[k]) { caught_[k] = pots_[k].takeover(pos, cur); if (!caught_[k]) return; }
    proc_.param(bank_, k)->setValueNotifyingHost(pots_[k].read(pos));
}

void PedalEditor::moveEffect(int slot, int dir) {
    uint8_t o[pedal::kNumEffects]; std::memcpy(o, pedal_.order(), sizeof o);
    const int j = slot + dir; if (j < 0 || j >= pedal::kNumEffects) return;
    std::swap(o[slot], o[j]); pedal_.setOrder(o);
}

void PedalEditor::timerCallback() {
    ++blink_;
    if (tapHeld_ && !selectMode_ && juce::Time::currentTimeMillis() - tapDownMs_ > 500) selectMode_ = true;
    auto* fx = pedal_.effect(bank_);
    for (int k = 0; k < kKnobs; ++k) {
        if (k < fx->numParams()) {
            const float v = fx->getParamReal(k); const auto& d = fx->paramDesc(k);
            juce::String txt = d.curve == pedal::Curve::Switch ? juce::String(v >= 0.5f ? "on" : "off")
                             : d.curve == pedal::Curve::Steps ? juce::String(static_cast<int>(v))
                             : juce::String(v, v < 10.f ? 2 : (v < 100.f ? 1 : 0)) + " " + d.unit;
            if (!caught_[k]) txt = "turn to " + juce::String(static_cast<int>(pedal_.getParam(bank_, k) * 100)) + "% to catch";
            knobValues_[k].setText(txt, juce::dontSendNotification);
            knobs_[k].setColour(juce::Slider::rotarySliderOutlineColourId, caught_[k] ? kCaught : kWaiting);
        } else knobValues_[k].setText("", juce::dontSendNotification);
    }
    for (int e = 0; e < pedal::kNumEffects; ++e) {
        const bool on = pedal_.isEnabled(e);
        fxSwitch_[e].setColour(juce::TextButton::buttonColourId, on ? juce::Colour(0xff3a5a40) : juce::Colour(0xff3a3f47));
        orderLabel_[e].setText(pedal_.effect(pedal_.order()[e])->name(), juce::dontSendNotification);
        if (ledDrawn_[e] != static_cast<int>(on)) { ledDrawn_[e] = on; repaint(ledRect_[e]); }
    }
    const int tapLed = selectMode_ ? ((blink_ / 3) % 2) + 1 : 0;
    if (tapLedDrawn_ != tapLed) { tapLedDrawn_ = tapLed; repaint(tapLedRect_); }
    const float bpm = 60.f / pedal_.tempo();
    const bool beat = std::fmod(static_cast<double>(pedal_.samplePosition()) / 48000.0, static_cast<double>(pedal_.tempo())) < 0.1;
    tempoLabel_.setText(juce::String("bpm ") + (beat ? "*" : " ") + (selectMode_ ? " [select fx]" : ""), juce::dontSendNotification);
    if (!bpmBox_.hasKeyboardFocus(true) && std::fabs(bpmBox_.getText().getFloatValue() - bpm) > 0.05f) bpmBox_.setText(juce::String(bpm, 1), juce::dontSendNotification);
    if (blink_ % 15 == 0) loadLabel_.setText("host DSP load " + juce::String(proc_.loadPercent.load(), 1) + " %", juce::dontSendNotification);
    if (blink_ % 15 == 0) sourceLabel_.setText(proc_.source.load() == PedalProcessor::Loop ? "loop: " + proc_.loopName() + "  " + juce::String(proc_.loopLengthSeconds(), 1) + " s" : "live input (enable it under Options > Audio Settings)", juce::dontSendNotification);
}

void PedalEditor::paint(juce::Graphics& g) {
    g.fillAll(kPanel);
    g.setColour(juce::Colours::white.withAlpha(0.08f)); g.fillRoundedRectangle(getLocalBounds().toFloat().reduced(8), 12.f);
    for (int e = 0; e < pedal::kNumEffects; ++e) {   // LEDs above the footswitches
        g.setColour(pedal_.isEnabled(e) ? kLedOn : kLedOff); g.fillEllipse(ledRect_[e].toFloat());
    }
    g.setColour(((blink_ / 3) % 2) && selectMode_ ? kWaiting : kLedOff); g.fillEllipse(tapLedRect_.toFloat());
    g.setColour(juce::Colours::white.withAlpha(0.7f)); g.setFont(juce::FontOptions(12.f));
    g.drawText("global high-cut", hpBox_.getX(), hpBox_.getY() - 16, 120, 14, juce::Justification::left);
    g.drawText("global low-cut", lpBox_.getX(), lpBox_.getY() - 16, 120, 14, juce::Justification::left);
    g.drawText("chain order", orderLeft_[0].getX(), orderLeft_[0].getY() - 16, 120, 14, juce::Justification::left);
}

void PedalEditor::resized() {
    auto r = getLocalBounds().reduced(16);
    bankLabel_.setBounds(r.removeFromTop(22));
    auto knobRow = r.removeFromTop(130);
    const int kw = knobRow.getWidth() / kKnobs;
    for (int k = 0; k < kKnobs; ++k) {
        auto c = knobRow.removeFromLeft(kw);
        knobLabels_[k].setBounds(c.removeFromTop(18)); knobValues_[k].setBounds(c.removeFromBottom(16)); knobs_[k].setBounds(c.reduced(8, 0));
    }
    r.removeFromTop(8);
    auto mid = r.removeFromTop(120);
    auto cuts = mid.removeFromLeft(290);
    cuts.removeFromTop(16); auto hpRow = cuts.removeFromTop(24); hpBox_.setBounds(hpRow.removeFromLeft(60)); hpA_.setBounds(hpRow.removeFromLeft(115)); hpB_.setBounds(hpRow);
    cuts.removeFromTop(20); auto lpRow = cuts.removeFromTop(24); lpBox_.setBounds(lpRow.removeFromLeft(60)); lpA_.setBounds(lpRow.removeFromLeft(115)); lpB_.setBounds(lpRow);
    mid.removeFromLeft(16);
    auto order = mid.removeFromLeft(200); order.removeFromTop(16);
    for (int e = 0; e < pedal::kNumEffects; ++e) { auto row = order.removeFromTop(24); orderLeft_[e].setBounds(row.removeFromLeft(24)); orderRight_[e].setBounds(row.removeFromRight(24)); orderLabel_[e].setBounds(row); order.removeFromTop(2); }
    mid.removeFromLeft(16);
    auto misc = mid; hwToggle_.setBounds(misc.removeFromTop(22)); monoToggle_.setBounds(misc.removeFromTop(22));
    inLevelLabel_.setBounds(misc.removeFromTop(16)); inLevel_.setBounds(misc.removeFromTop(22)); loadLabel_.setBounds(misc.removeFromTop(18));
    { auto tr = misc.removeFromTop(24); bpmLabel_.setBounds(tr.removeFromLeft(48)); bpmBox_.setBounds(tr.removeFromLeft(62)); tempoLabel_.setBounds(tr); }
    r.removeFromTop(8);
    auto presets = r.removeFromTop(26); loadBtn_.setBounds(presets.removeFromLeft(110)); presets.removeFromLeft(8); saveBtn_.setBounds(presets.removeFromLeft(110));
    presets.removeFromLeft(24); sourceBox_.setBounds(presets.removeFromLeft(150)); presets.removeFromLeft(8); loopBtn_.setBounds(presets.removeFromLeft(130)); presets.removeFromLeft(8); loopGain_.setBounds(presets);
    r.removeFromTop(4); sourceLabel_.setBounds(r.removeFromTop(16));
    r.removeFromTop(14);
    auto sw = r.removeFromTop(60);
    const int w = sw.getWidth() / 5;
    for (int e = 0; e < pedal::kNumEffects; ++e) fxSwitch_[e].setBounds(sw.removeFromLeft(w).reduced(12, 4));
    tap_.setBounds(sw.reduced(12, 4));
    r.removeFromTop(10);
    { auto kr = r.removeFromTop(18); keysLabel_.setBounds(kr.removeFromLeft(520)); keysGain_.setBounds(kr); }
    keyboard_.setBounds(r.removeFromTop(90));
    for (int e = 0; e < pedal::kNumEffects; ++e) { auto b = fxSwitch_[e].getBounds(); ledRect_[e] = { b.getCentreX() - 6, b.getY() - 18, 12, 12 }; }
    { auto b = tap_.getBounds(); tapLedRect_ = { b.getCentreX() - 6, b.getY() - 18, 12, 12 }; }
}
