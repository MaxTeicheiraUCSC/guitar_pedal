// Daisy Seed3 wrapper around the portable DSP core.
// UNVERIFIED: written against libDaisy's public API but not compiled here (no arm-none-eabi toolchain on the dev machine).
//
// Pin map (Seed3 datasheet names) — keep in sync with hardware/kicad and docs/design.md §2
//   ADC A0..A5 (pins 22-27)  : knobs 1-6 (B10k, 3V3A -> GND)
//   D0..D3 (pins 1-4)        : effect footswitches 1-4  (SPST momentary to GND, internal pull-up)
//   D4 (pin 5)               : TAP footswitch
//   D5,D6 (pins 6,7)         : global HP toggle (ON-OFF-ON: two contacts to GND)
//   D7,D8 (pins 8,9)         : global LP toggle
//   D9..D12 (pins 10-13)     : effect LEDs 1-4 ; D26 (pin 33): tempo LED
//   D13/D14 (pins 14/15)     : USART1 TX/RX <-> ESP32-C3 (MIDI bytes, 115200 8N1)
//   D27 (pin 34)             : input jack ring switch (stereo plug present, to GND)
#include "daisy_seed.h"
#include "pedal/pedal.h"
#include "pedal/midi.h"

using namespace daisy;

static DaisySeed hw;

// ---- SDRAM arena for the DSP core ----
static uint8_t DSY_SDRAM_BSS sdramArena[4 * 1024 * 1024];
class SdramAllocator final : public pedal::Allocator {
public:
    void* alloc(size_t bytes, size_t align) override {
        size_t p = (used_ + align - 1) & ~(align - 1);
        if (p + bytes > sizeof(sdramArena)) return nullptr;
        used_ = p + bytes; return sdramArena + p;
    }
private:
    size_t used_ = 0;
};

static SdramAllocator mem;
static pedal::Pedal   pedalCore;
static pedal::MidiParser midi(pedalCore);
static UartHandler    uart;
static CpuLoadMeter   loadMeter;
static pedal::Frame   block[pedal::kMaxBlock];
constexpr int kBlock = 48;   // 1 ms @ 48 kHz

// ---- controls ----
static Switch fxSw[4], tapSw, hpUp, hpDn, lpUp, lpDn, ringSw;
static GPIO   led[4], tempoLed;
static AnalogControl knob[6];
static int   bank = 3;                   // effect edited by the knob bank
static bool  knobCaught[6] = { true, true, true, true, true, true };
static float knobLast[6] = {};
static uint32_t tapDownMs = 0; static bool selectMode = false;

// echo state changes to the ESP32 so BLE/web clients stay in sync
class UartEcho final : public pedal::PedalListener {
public:
    void onParamChanged(int e, int p, float n) override { uint8_t b[6]; send(b, pedal::MidiParser::encodeParamCC(e, p, n, b)); }
    void onEnabledChanged(int e, bool on) override      { uint8_t b[3]; send(b, pedal::MidiParser::encodeEnableCC(e, on, b)); }
    void onOrderChanged(const uint8_t* o) override      { uint8_t b[8]; send(b, pedal::MidiParser::encodeOrder(o, b)); }
    void onTempoChanged(float spb) override             { uint8_t b[6]; send(b, pedal::MidiParser::encodeTempo(spb, b)); }
    void onPresetRequest(bool save, int slot) override;
private:
    void send(const uint8_t* b, int n) { uart.BlockingTransmit(const_cast<uint8_t*>(b), n, 10); }
} echo;

// ---- presets in QSPI ----
struct PresetBank { pedal::Preset slots[8]; };
static PersistentStorage<PresetBank> storage(hw.qspi);
void UartEcho::onPresetRequest(bool save, int slot) {
    if (slot < 0 || slot >= 8) return;
    auto& bankData = storage.GetSettings();
    if (save) { pedalCore.toPreset(bankData.slots[slot]); storage.Save(); }
    else if (pedalCore.applyPreset(bankData.slots[slot])) {
        uint8_t dump[pedal::MidiParser::kDumpMax]; const int n = pedal::MidiParser::encodeStateDump(pedalCore, dump);
        uart.BlockingTransmit(dump, n, 50);
    }
}

static void AudioCallback(AudioHandle::InputBuffer in, AudioHandle::OutputBuffer out, size_t size) {
    loadMeter.OnBlockStart();
    for (size_t i = 0; i < size; ++i) block[i] = { in[0][i], in[1][i] };
    pedalCore.process(block, static_cast<int>(size));
    for (size_t i = 0; i < size; ++i) { out[0][i] = block[i].l; out[1][i] = block[i].r; }
    loadMeter.OnBlockEnd();
}

static int toggleState(Switch& up, Switch& dn) { return up.Pressed() ? 1 : (dn.Pressed() ? 2 : 0); }

static void pollControls() {
    for (auto& s : fxSw) s.Debounce();
    tapSw.Debounce(); hpUp.Debounce(); hpDn.Debounce(); lpUp.Debounce(); lpDn.Debounce(); ringSw.Debounce();
    const uint32_t now = System::GetNow();

    // TAP: press = tap tempo; hold 500 ms = select mode; hold 3 s (with fx1+fx4) = DFU
    if (tapSw.RisingEdge()) tapDownMs = now;
    if (tapSw.Pressed() && !selectMode && now - tapDownMs > 500) selectMode = true;
    if (tapSw.FallingEdge()) { if (!selectMode) pedalCore.tap(pedalCore.samplePosition()); selectMode = false; }
    if (fxSw[0].Pressed() && fxSw[3].Pressed() && fxSw[0].TimeHeldMs() > 2000 && fxSw[3].TimeHeldMs() > 2000) System::ResetToBootloader();

    for (int e = 0; e < 4; ++e) if (fxSw[e].RisingEdge()) {
        if (selectMode) { bank = e; for (auto& c : knobCaught) c = false; }
        else pedalCore.toggle(e);
    }
    static int lastHp = -1, lastLp = -1;
    const int hpS = toggleState(hpUp, hpDn), lpS = toggleState(lpUp, lpDn);
    if (hpS != lastHp) { lastHp = hpS; pedalCore.setGlobalHp(hpS); }
    if (lpS != lastLp) { lastLp = lpS; pedalCore.setGlobalLp(lpS); }
    pedalCore.setStereoInput(ringSw.Pressed());

    // knobs with soft take-over
    for (int k = 0; k < 6; ++k) {
        knob[k].Process();
        const float v = knob[k].Value();
        if (k >= pedalCore.effect(bank)->numParams()) continue;
        const float cur = pedalCore.getParam(bank, k);
        if (!knobCaught[k]) { if ((knobLast[k] - cur) * (v - cur) <= 0.f || fabsf(v - cur) < 0.01f) knobCaught[k] = true; }
        if (knobCaught[k] && fabsf(v - cur) > 0.002f) pedalCore.setParam(bank, k, v);
        knobLast[k] = v;
    }
    for (int e = 0; e < 4; ++e) led[e].Write(selectMode ? (e == bank && (now / 100) % 2) : pedalCore.isEnabled(e));
    const float beatPos = fmodf(static_cast<float>(pedalCore.samplePosition()) / 48000.f, pedalCore.tempo());
    tempoLed.Write(beatPos < 0.08f);
}

int main() {
    hw.Init(true);   // boost to 480 MHz
    hw.StartLog(false);   // serial log for the cpu line below (no wait for a host)
    hw.SetAudioBlockSize(kBlock);
    hw.SetAudioSampleRate(SaiHandle::Config::SampleRate::SAI_48KHZ);

    pedal::Config cfg; cfg.sampleRate = hw.AudioSampleRate(); cfg.blockSize = kBlock; cfg.mem = &mem;
    pedalCore.init(cfg);
    pedalCore.setListener(&echo);

    // controls
    const Pin fxPins[4] = { seed::D0, seed::D1, seed::D2, seed::D3 };
    for (int e = 0; e < 4; ++e) fxSw[e].Init(fxPins[e], 1000.f);
    tapSw.Init(seed::D4, 1000.f); hpUp.Init(seed::D5, 1000.f); hpDn.Init(seed::D6, 1000.f); lpUp.Init(seed::D7, 1000.f); lpDn.Init(seed::D8, 1000.f);
    ringSw.Init(seed::D27, 1000.f);
    const Pin ledPins[4] = { seed::D9, seed::D10, seed::D11, seed::D12 };
    for (int e = 0; e < 4; ++e) led[e].Init(ledPins[e], GPIO::Mode::OUTPUT);
    tempoLed.Init(seed::D26, GPIO::Mode::OUTPUT);
    AdcChannelConfig adcCfg[6]; const Pin knobPins[6] = { seed::A0, seed::A1, seed::A2, seed::A3, seed::A4, seed::A5 };
    for (int k = 0; k < 6; ++k) adcCfg[k].InitSingle(knobPins[k]);
    hw.adc.Init(adcCfg, 6); hw.adc.Start();
    for (int k = 0; k < 6; ++k) knob[k].Init(hw.adc.GetPtr(k), 1000.f);

    // UART to the ESP32-C3
    UartHandler::Config ucfg; ucfg.baudrate = 115200; ucfg.periph = UartHandler::Config::Peripheral::USART_1;
    ucfg.pin_config.tx = seed::D13; ucfg.pin_config.rx = seed::D14;
    uart.Init(ucfg);
    static uint8_t rxBuf[256]; uart.DmaReceiveFifo();   // libDaisy FIFO receive (v7+)

    // presets
    PresetBank def; for (auto& s : def.slots) pedal::Pedal::defaultPreset(s);
    storage.Init(def);
    pedalCore.applyPreset(storage.GetSettings().slots[0]);

    loadMeter.Init(hw.AudioSampleRate(), kBlock);
    hw.StartAudio(AudioCallback);

    uint32_t lastLog = 0;
    while (true) {
        pollControls();
        while (uart.Readable()) midi.feed(uart.PopRx());
        if (midi.stateRequested()) { uint8_t dump[pedal::MidiParser::kDumpMax]; uart.BlockingTransmit(dump, pedal::MidiParser::encodeStateDump(pedalCore, dump), 50); }
        const uint32_t now = System::GetNow();
        if (now - lastLog > 1000) { lastLog = now; hw.PrintLine("cpu avg %d%% max %d%%", static_cast<int>(loadMeter.GetAvgCpuLoad() * 100.f), static_cast<int>(loadMeter.GetMaxCpuLoad() * 100.f)); }
        System::Delay(1);
    }
}
