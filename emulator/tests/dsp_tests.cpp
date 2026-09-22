// Property tests for the DSP core and the hardware model. No golden files:
// every check is against a physical/mathematical expectation with a tolerance.
#include "pedal/pedal.h"
#include "pedal/midi.h"
#include "hwmodel/analog_model.h"
#include "cli/host_alloc.h"
#include <cstdio>
#include <cmath>
#include <vector>
#include <complex>
#include <cstring>
#include <algorithm>

static int g_fail = 0, g_pass = 0;
#define CHECK(cond, ...) do { if (cond) { ++g_pass; } else { ++g_fail; std::printf("FAIL %s:%d: ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

using pedal::Frame;
static constexpr float FS = 48000.f;

static void fft(std::vector<std::complex<float>>& a) {
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i) { size_t bit = n >> 1; for (; j & bit; bit >>= 1) j ^= bit; j ^= bit; if (i < j) std::swap(a[i], a[j]); }
    for (size_t len = 2; len <= n; len <<= 1) {
        const float ang = -2.f * pedal::kPi / len; const std::complex<float> wl(std::cos(ang), std::sin(ang));
        for (size_t i = 0; i < n; i += len) { std::complex<float> w(1.f, 0.f); for (size_t j = 0; j < len / 2; ++j) { auto u = a[i + j], v = a[i + j + len / 2] * w; a[i + j] = u + v; a[i + j + len / 2] = u - v; w *= wl; } }
    }
}
static float rms(const std::vector<float>& v, size_t from = 0, size_t to = 0) {
    if (to == 0) to = v.size(); double s = 0; for (size_t i = from; i < to; ++i) s += double(v[i]) * v[i]; return std::sqrt(s / (to - from));
}
static float db(float x) { return 20.f * std::log10(x > 1e-12f ? x : 1e-12f); }

struct Rig {
    HostAllocator mem; pedal::Pedal p; pedal::Config cfg;
    Rig(int block = 48) { cfg.sampleRate = FS; cfg.blockSize = block; cfg.mem = &mem; if (!p.init(cfg)) std::abort(); for (int e = 0; e < 4; ++e) p.setEnabled(e, false); }
    // run mono signal through, return left channel
    std::vector<float> run(const std::vector<float>& x) {
        std::vector<float> y(x.size()); std::vector<Frame> buf(cfg.blockSize);
        for (size_t pos = 0; pos < x.size(); pos += cfg.blockSize) {
            const int n = static_cast<int>(std::min<size_t>(cfg.blockSize, x.size() - pos));
            for (int i = 0; i < n; ++i) buf[i] = { x[pos + i], x[pos + i] };
            p.process(buf.data(), n);
            for (int i = 0; i < n; ++i) y[pos + i] = buf[i].l;
        }
        return y;
    }
};
static std::vector<float> sine(float hz, float amp, size_t n) { std::vector<float> v(n); for (size_t i = 0; i < n; ++i) v[i] = amp * std::sin(pedal::kTwoPi * hz * i / FS); return v; }
static void setReal(pedal::Pedal& p, int e, const char* name, float v) {
    for (int i = 0; i < p.effect(e)->numParams(); ++i) if (!std::strcmp(p.effect(e)->paramDesc(i).name, name)) { p.effect(e)->setParamReal(i, v); return; }
    std::printf("no such param %s\n", name); std::abort();
}

// ---------------------------------------------------------------- filters
static void testBiquad() {
    for (auto type : { pedal::Biquad::HighPass, pedal::Biquad::LowPass }) {
        pedal::Biquad b; b.set(type, FS, 1000.f);
        auto x = sine(1000.f, 1.f, 48000); std::vector<float> y(x.size()); for (size_t i = 0; i < x.size(); ++i) y[i] = b.process(x[i]);
        const float g = db(rms(y, 24000) / rms(x, 24000));
        CHECK(std::fabs(g + 3.01f) < 0.15f, "biquad %s gain at fc = %.2f dB (expect -3.01)", type == pedal::Biquad::HighPass ? "HP" : "LP", g);
    }
    pedal::Biquad lp; lp.setOnePoleLP(FS, 2000.f);
    auto x = sine(2000.f, 1.f, 48000); std::vector<float> y(x.size()); for (size_t i = 0; i < x.size(); ++i) y[i] = lp.process(x[i]);
    CHECK(std::fabs(db(rms(y, 24000) / rms(x, 24000)) + 3.01f) < 0.15f, "one-pole LP at fc = %.2f dB", db(rms(y, 24000) / rms(x, 24000)));
}

// ---------------------------------------------------------------- oversampler
static void testOversampler() {
    for (int f : { 2, 4 }) {
        pedal::Oversampler up, dn; up.init(f); dn.init(f);
        auto x = sine(5000.f, 0.5f, 9600); std::vector<float> y(x.size()); float tmp[4];
        for (size_t i = 0; i < x.size(); ++i) { up.up(x[i], tmp); y[i] = dn.down(tmp); }
        const float lat = static_cast<float>(up.latency());
        double err = 0; int cnt = 0;
        for (size_t i = 4000; i + 1 < x.size(); ++i) { const float ref = 0.5f * std::sin(pedal::kTwoPi * 5000.f * (i - lat) / FS); err += std::fabs(y[i] - ref); ++cnt; }
        CHECK(err / cnt < 0.01, "oversampler %dx passthrough error %.4f (latency %.1f)", f, err / cnt, lat);
    }
}

// ---------------------------------------------------------------- delay
static void testDelay() {
    Rig r; auto& p = r.p; p.setEnabled(1, true);
    setReal(p, 1, "Time", 500.f); setReal(p, 1, "Mix", 1.f); setReal(p, 1, "Feedback", 0.f); setReal(p, 1, "Mod", 0.f);
    setReal(p, 1, "HighCut", 20000.f); setReal(p, 1, "LowCut", 20.f);
    p.delay().reset();
    std::vector<float> x(48000, 0.f); x[100] = 1.f;
    // let smoothers settle before the impulse: run one block of silence first through a fresh rig
    auto y = r.run(x);
    size_t peak = 0; for (size_t i = 0; i < y.size(); ++i) if (std::fabs(y[i]) > std::fabs(y[peak])) peak = i;
    CHECK(std::labs(long(peak) - long(100 + 24000)) <= 1, "delay peak at %zu (expect 24100)", peak);
    { float e = 0.f; for (size_t i = peak - 10; i <= peak + 10; ++i) e += y[i];   // write-path LP/HP + tanh spread the impulse
      CHECK(e > 0.6f && e < 1.1f, "delay impulse integral near peak %.3f", e); }

    // tap tempo subdivision: 120 bpm eighth = 250 ms
    setReal(p, 1, "Subdiv", pedal::Delay::SubEighth); p.setTempo(0.5f); p.delay().reset();
    x.assign(48000, 0.f); x[100] = 1.f;
    // discard a run to let the time smoother settle
    r.run(std::vector<float>(24000, 0.f)); p.delay().reset();
    y = r.run(x); peak = 0; for (size_t i = 0; i < y.size(); ++i) if (std::fabs(y[i]) > std::fabs(y[peak])) peak = i;
    CHECK(std::labs(long(peak) - long(100 + 12000)) <= 1, "delay subdiv peak at %zu (expect 12100)", peak);

    // trails: disable mid-way, output must continue then decay to silence
    setReal(p, 1, "Subdiv", 0); setReal(p, 1, "Time", 100.f); setReal(p, 1, "Feedback", 0.5f); p.delay().reset();
    r.run(std::vector<float>(24000, 0.f)); p.delay().reset();
    auto burst = sine(440.f, 0.5f, 4800); r.run(burst);
    p.setEnabled(1, false);
    auto tail = r.run(std::vector<float>(96000, 0.f));
    CHECK(rms(tail, 0, 4800) > 0.05f, "delay trail present after bypass (rms %.3f)", rms(tail, 0, 4800));
    CHECK(rms(tail, 90000) < 1e-4f, "delay trail decays (rms %.6f)", rms(tail, 90000));
    // bypass with a self-oscillating tail (feedback > 1) must still let the dry signal through at unity and the tail must die
    setReal(p, 1, "Feedback", 1.1f); setReal(p, 1, "Mix", 0.8f); p.setEnabled(1, true); p.delay().reset(); r.run(sine(440.f, 0.5f, 24000));
    p.setEnabled(1, false); r.run(std::vector<float>(48000 * 4, 0.f));
    auto dry = r.run(sine(1000.f, 0.5f, 48000));
    CHECK(std::fabs(rms(dry, 24000) / 0.3536f - 1.f) < 0.05f, "bypassed delay passes dry at unity after a runaway tail (ratio %.3f)", rms(dry, 24000) / 0.3536f);
}

// ---------------------------------------------------------------- tremolo
static void testTremolo() {
    Rig r; auto& p = r.p; p.setEnabled(0, true);
    setReal(p, 0, "Rate", 5.f); setReal(p, 0, "Depth", 1.f); setReal(p, 0, "Shape", 0);
    r.run(std::vector<float>(4800, 0.f));
    auto y = r.run(sine(1000.f, 1.f, 48000));
    // envelope: peak per 1 ms window
    float mn = 1e9f, mx = 0.f;
    for (size_t w = 4800; w + 48 <= y.size(); w += 48) { float pk = 0.f; for (size_t i = w; i < w + 48; ++i) pk = std::max(pk, std::fabs(y[i])); mn = std::min(mn, pk); mx = std::max(mx, pk); }
    CHECK(mx > 0.95f && mn < 0.05f, "tremolo depth 1: env max %.3f min %.3f", mx, mn);
    // period: count zero crossings of envelope-ish -> use autocorrelation at 200 ms (5 Hz) lag
    setReal(p, 0, "Depth", 0.5f); y = r.run(sine(1000.f, 1.f, 96000));
    std::vector<float> env; for (size_t w = 48000; w + 48 <= y.size(); w += 48) { float pk = 0.f; for (size_t i = w; i < w + 48; ++i) pk = std::max(pk, std::fabs(y[i])); env.push_back(pk); }
    const int lag = 200; double num = 0, den = 0; const double mean = [&] { double s = 0; for (float v : env) s += v; return s / env.size(); }();
    for (size_t i = 0; i + lag < env.size(); ++i) { num += (env[i] - mean) * (env[i + lag] - mean); den += (env[i] - mean) * (env[i] - mean); }
    CHECK(num / den > 0.95, "tremolo 5 Hz periodicity (autocorr at 200 ms = %.3f)", num / den);
    // harmonic mode must keep DC/average level and remain bounded
    setReal(p, 0, "Harmonic", 1.f); y = r.run(sine(1000.f, 1.f, 48000));
    CHECK(rms(y, 24000) > 0.5f && rms(y, 24000) < 1.f, "harmonic tremolo level %.3f", rms(y, 24000));
}

// ---------------------------------------------------------------- reverb
static float rt60(Rig& r) {
    std::vector<float> x(48000 * 6, 0.f); x[0] = 1.f;
    auto y = r.run(x);
    // Schroeder backward integration
    std::vector<double> edc(y.size()); double acc = 0; for (size_t i = y.size(); i-- > 0;) { acc += double(y[i]) * y[i]; edc[i] = acc; }
    const double e0 = edc[2400];  // skip pre-delay/early part
    size_t t5 = 0, t35 = 0;
    for (size_t i = 2400; i < edc.size(); ++i) { const double d = 10 * std::log10(edc[i] / e0); if (!t5 && d < -5) t5 = i; if (!t35 && d < -35) { t35 = i; break; } }
    if (!t35) return 1e9f;
    return 2.f * (t35 - t5) / FS;   // T30 extrapolated to 60 dB
}
static void testReverb() {
    Rig r; auto& p = r.p; p.setEnabled(2, true);
    setReal(p, 2, "Mix", 1.f); setReal(p, 2, "PreDelay", 0.f); setReal(p, 2, "Mod", 0.f);
    setReal(p, 2, "Decay", 0.5f); r.run(std::vector<float>(4800, 0.f)); p.reverb().reset();
    const float t1 = rt60(r);
    setReal(p, 2, "Decay", 0.85f); r.run(std::vector<float>(4800, 0.f)); p.reverb().reset();
    const float t2 = rt60(r);
    CHECK(t1 > 0.2f && t1 < 3.f, "reverb RT60 at decay 0.5 = %.2f s", t1);
    CHECK(t2 > t1 * 1.5f, "reverb RT60 grows with decay (%.2f -> %.2f s)", t1, t2);
    // stability at maximum decay with a loud input
    setReal(p, 2, "Decay", 0.98f); p.reverb().reset();
    auto y = r.run(sine(200.f, 1.f, 48000 * 4));
    float pk = 0.f; for (float v : y) pk = std::max(pk, std::fabs(v));
    CHECK(pk < 20.f && std::isfinite(pk), "reverb bounded at max decay (peak %.2f)", pk);
    // trails on bypass
    setReal(p, 2, "Decay", 0.7f); p.reverb().reset(); r.run(sine(440.f, 0.5f, 4800)); p.setEnabled(2, false);
    auto tail = r.run(std::vector<float>(48000 * 6, 0.f));
    CHECK(rms(tail, 0, 4800) > 0.01f, "reverb trail after bypass (rms %.4f)", rms(tail, 0, 4800));
    CHECK(rms(tail, 48000 * 5) < 1e-3f, "reverb trail decays (rms %.6f)", rms(tail, 48000 * 5));
}

// ---------------------------------------------------------------- fuzz aliasing
static float nonHarmonicRatioDb(const std::vector<float>& y, int fundBin, size_t N) {
    std::vector<std::complex<float>> a(N); for (size_t i = 0; i < N; ++i) a[i] = y[y.size() - N + i];
    fft(a);
    double total = 0, harm = 0;
    for (size_t k = 1; k < N / 2; ++k) { const double m = std::norm(a[k]); total += m; const int mod = static_cast<int>(k % fundBin); if (mod <= 2 || mod >= fundBin - 2) harm += m; }
    return 10.f * std::log10((total - harm) / total);
}
static void testFuzz() {
    const size_t N = 1 << 15; const int fundBin = 683; const float f0 = fundBin * FS / N;   // 1000.5 Hz, bin-aligned
    float ratio[3] = {};
    for (int os = 0; os < 3; ++os) {
        Rig r; auto& p = r.p; p.setEnabled(3, true);
        setReal(p, 3, "Gain", 40.f); setReal(p, 3, "Mode", pedal::Fuzz::Hard); setReal(p, 3, "Oversample", os); setReal(p, 3, "Tone", 0.f);
        setReal(p, 3, "Gate", -90.f); setReal(p, 3, "LowCut", 20.f); setReal(p, 3, "Volume", 0.f);
        r.run(std::vector<float>(4800, 0.f));
        auto y = r.run(sine(f0, 0.5f, N + 8192));
        ratio[os] = nonHarmonicRatioDb(y, fundBin, N);
        float pk = 0.f; for (float v : y) pk = std::max(pk, std::fabs(v));
        CHECK(pk < 1.5f && pk > 0.5f, "fuzz output level sane at %dx (peak %.2f)", 1 << os, pk);
    }
    std::printf("  fuzz non-harmonic energy: 1x %.1f dB, 2x %.1f dB, 4x %.1f dB\n", ratio[0], ratio[1], ratio[2]);
    CHECK(ratio[2] < ratio[0] - 10.f, "4x oversampling + ADAA reduces aliasing by >10 dB (%.1f -> %.1f)", ratio[0], ratio[2]);
    CHECK(ratio[2] < -40.f, "4x aliasing floor below -40 dB (%.1f)", ratio[2]);
    // gate closes on silence
    Rig r; auto& p = r.p; p.setEnabled(3, true); setReal(p, 3, "Gain", 60.f); setReal(p, 3, "Gate", -40.f);
    r.run(sine(200.f, 0.5f, 4800)); auto q = r.run(std::vector<float>(48000, 1e-4f));
    CHECK(rms(q, 24000) < 1e-3f, "fuzz gate closes on -80 dB input (rms %.5f)", rms(q, 24000));
}

// ---------------------------------------------------------------- chain / midi / presets
static void testChainAndControl() {
    Rig r; auto& p = r.p;
    uint8_t bad[4] = { 0, 0, 1, 2 }; CHECK(!p.setOrder(bad), "invalid order rejected");
    uint8_t o1[4] = { 3, 1, 0, 2 }, o2[4] = { 1, 3, 0, 2 };
    p.setEnabled(1, true); p.setEnabled(3, true); setReal(p, 1, "Feedback", 0.f); setReal(p, 1, "Mix", 0.5f); setReal(p, 1, "Time", 37.f); setReal(p, 1, "Mod", 0.f);
    CHECK(p.setOrder(o1), "order accepted"); p.delay().reset(); p.fuzz().reset(); r.run(std::vector<float>(9600, 0.f)); p.delay().reset();
    auto y1 = r.run(sine(300.f, 0.3f, 24000));
    CHECK(p.setOrder(o2), "order accepted"); p.delay().reset(); p.fuzz().reset(); r.run(std::vector<float>(9600, 0.f)); p.delay().reset();
    auto y2 = r.run(sine(300.f, 0.3f, 24000));
    double diff = 0; for (size_t i = 12000; i < y1.size(); ++i) diff += std::fabs(y1[i] - y2[i]); diff /= 12000;
    CHECK(diff > 1e-3, "fuzz->delay differs from delay->fuzz (mean |diff| %.4f)", diff);

    // MIDI: 14-bit CC on delay Time (fx1 param 0 -> CC 8 / 40)
    pedal::MidiParser m(p);
    const uint8_t cc[] = { 0xB0, 8, 0x40, 0xB0, 40, 0x00 }; m.feed(cc, 6);
    CHECK(std::fabs(p.getParam(1, 0) - 8192.f / 16383.f) < 1e-4f, "14-bit CC sets param (%.5f)", p.getParam(1, 0));
    const uint8_t en[] = { 0xB0, 66, 127 }; m.feed(en, 3); CHECK(p.isEnabled(2), "CC 66 enables reverb");
    const uint8_t sx[] = { 0xF0, 0x7D, 0x01, 2, 1, 0, 3, 0xF7 }; m.feed(sx, 8);
    CHECK(p.order()[0] == 2 && p.order()[3] == 3, "SysEx sets order");
    const uint8_t hp[] = { 0xF0, 0x7D, 0x04, 1, (uint8_t)(300 >> 7), (uint8_t)(300 & 0x7F), 0xF7, 0xB0, 70, 2 }; m.feed(hp, 10);
    CHECK(p.globalHp() == 2 && std::fabs(p.hpFreq(1) - 300.f) < 0.5f, "SysEx HP freq + CC 70 mode (%d, %.0f Hz)", p.globalHp(), p.hpFreq(1));
    // state dump round trip into a second pedal
    uint8_t dump[pedal::MidiParser::kDumpMax]; const int n = pedal::MidiParser::encodeStateDump(p, dump);
    Rig r2; pedal::MidiParser m2(r2.p); m2.feed(dump, n);
    bool same = true; for (int e = 0; e < 4; ++e) { same &= r2.p.isEnabled(e) == p.isEnabled(e); same &= r2.p.order()[e] == p.order()[e]; for (int i = 0; i < 8; ++i) same &= std::fabs(r2.p.getParam(e, i) - p.getParam(e, i)) < 1e-4f; }
    CHECK(same && r2.p.globalHp() == 2 && std::fabs(r2.p.hpFreq(1) - 300.f) < 0.5f, "state dump round-trips (%d bytes)", n);
    // preset round trip
    pedal::Preset pr; p.toPreset(pr); Rig r3; CHECK(r3.p.applyPreset(pr), "preset applies");
    pedal::Preset pr2; r3.p.toPreset(pr2);
    bool eq = pr.globalHp == pr2.globalHp && pr.globalLp == pr2.globalLp && pr.tempoSpb == pr2.tempoSpb && pr.inputTrimDb == pr2.inputTrimDb && pr.outputTrimDb == pr2.outputTrimDb;
    for (int e = 0; e < 4; ++e) { eq &= pr.order[e] == pr2.order[e] && pr.enabled[e] == pr2.enabled[e]; for (int i = 0; i < 8; ++i) eq &= pr.params[e][i] == pr2.params[e][i]; }
    for (int s = 0; s < 2; ++s) eq &= pr.hpFreq[s] == pr2.hpFreq[s] && pr.lpFreq[s] == pr2.lpFreq[s];
    CHECK(eq, "preset round-trips field-exact");
    // tap tempo
    p.tap(0); p.tap(24000); p.tap(48000); CHECK(std::fabs(p.tempo() - 0.5f) < 1e-3f, "tap tempo 120 bpm (%.3f spb)", p.tempo());
    // global cuts at fc
    Rig r4; r4.p.setHpFreq(0, 1000.f); r4.p.setGlobalHp(1); auto yy = r4.run(sine(1000.f, 1.f, 48000));
    CHECK(std::fabs(db(rms(yy, 24000) / 0.7071f) + 3.01f) < 0.2f, "global HP -3 dB at fc (%.2f dB)", db(rms(yy, 24000) / 0.7071f));
    // mono input duplicates L to R
    Rig r5; r5.p.setStereoInput(false); std::vector<Frame> b(4); for (auto& f : b) f = { 0.5f, 0.f }; r5.p.process(b.data(), 4);
    CHECK(std::fabs(b[3].r - 0.5f) < 1e-6f, "TS plug -> dual mono");
}

// ---------------------------------------------------------------- hardware model
static void testHardwareModel() {
    hw::AnalogParams ap; hw::InputStage in; in.init(FS, ap); hw::OutputStage out; out.init(FS, ap);
    auto x = sine(1000.f, 1.f, 48000); std::vector<float> y(x.size()); for (size_t i = 0; i < x.size(); ++i) y[i] = in.process(x[i]);
    const float gIn = rms(y, 24000) / rms(x, 24000);
    const float gExp = ap.inAtten * std::fabs(ap.inStage2Gain);
    CHECK(std::fabs(gIn - gExp) < 0.01f, "input stage gain at 1 kHz = %.4f (expect %.4f)", gIn, gExp);
    // 20 kHz roll-off from the three RC corners should be within ~2 dB
    x = sine(20000.f, 1.f, 48000); for (size_t i = 0; i < x.size(); ++i) y[i] = in.process(x[i]);
    const float g20 = db(rms(y, 24000) / rms(x, 24000)) - db(gIn);
    CHECK(g20 < 0.f && g20 > -3.f, "input stage 20 kHz relative gain %.2f dB", g20);
    // gain structure: the op-amp rail limit, not the codec, sets the ceiling (12 Vpp in -> ~1.48 V, below the 1.8 V abs max)
    x = sine(1000.f, 6.f, 48000); for (size_t i = 0; i < x.size(); ++i) y[i] = in.process(x[i]);
    float pk = 0.f; for (size_t i = 24000; i < y.size(); ++i) pk = std::max(pk, std::fabs(y[i]));
    CHECK(pk < ap.codecClipV && pk > 1.4f, "op-amp-limited ceiling below codec abs max (peak %.3f)", pk);
    // output stage: 0 dBFS -> 1 Vrms * 2.2 * load
    x = sine(1000.f, 0.7071f, 48000); for (size_t i = 0; i < x.size(); ++i) y[i] = out.process(x[i]);
    const float vout = rms(y, 24000);
    CHECK(std::fabs(vout - 0.7071f * 2.2f * ap.outLoadGain) < 0.05f, "output stage -3 dBFS -> %.3f Vrms (expect 1.54)", vout);
    // pot takeover
    hw::PotModel pot; pot.init(1000.f); pot.release();
    CHECK(!pot.takeover(0.2f, 0.6f), "pot not caught before crossing");
    CHECK(!pot.takeover(0.4f, 0.6f), "pot still not caught");
    CHECK(pot.takeover(0.7f, 0.6f), "pot caught after crossing");
}

int main() {
    testBiquad(); testOversampler(); testDelay(); testTremolo(); testReverb(); testFuzz(); testChainAndControl(); testHardwareModel();
    std::printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
