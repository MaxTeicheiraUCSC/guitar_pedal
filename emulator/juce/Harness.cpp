// In-process stress harness: drives PedalProcessor + PedalEditor the way a user would,
// with the audio thread running concurrently. Built with ASan/UBSan (see CMakeLists).
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <thread>
#include <atomic>
#include <random>

int main() {
    juce::ScopedJuceInitialiser_GUI init;
    auto* mm = juce::MessageManager::getInstance(); mm->setCurrentThreadAsMessageThread();
    {   // sampler in isolation on a fresh processor
        auto proc = std::make_unique<PedalProcessor>(); proc->setPlayConfigDetails(2, 2, 48000.0, 512); proc->prepareToPlay(48000.0, 512);
        proc->source = PedalProcessor::Silence; proc->hwModel = false; for (int e = 0; e < 4; ++e) proc->enableParam(e)->setValueNotifyingHost(0.f);
        juce::AudioBuffer<float> buf(2, 512); juce::MidiBuffer midi;
        buf.clear(); midi.clear(); proc->processBlock(buf, midi); std::printf("silence rms %.5f\n", buf.getRMSLevel(0, 0, 512));
        { proc->source = PedalProcessor::Loop; float pk = 0.f; double acc = 0; const int nb = static_cast<int>(proc->loopLengthSeconds() * 48000 / 512) + 1;
          for (int b = 0; b < nb; ++b) { buf.clear(); midi.clear(); proc->processBlock(buf, midi); pk = std::max(pk, buf.getMagnitude(0, 512)); acc += buf.getRMSLevel(0, 0, 512); }
          std::printf("fresh processor, loop only: peak %.3f mean rms %.4f\n", pk, acc / nb); proc->source = PedalProcessor::Silence; }
        proc->keyboardState.noteOn(1, 52, 1.0f);
        for (int b = 0; b < 20; ++b) { buf.clear(); midi.clear(); proc->processBlock(buf, midi); if (b % 5 == 0) std::printf("held block %d rms %.4f mean %.4f\n", b, buf.getRMSLevel(0, 0, 512), buf.getSample(0, 0)); }
        proc->keyboardState.noteOff(1, 52, 0.f);
        for (int b = 0; b < 30; ++b) { buf.clear(); midi.clear(); proc->processBlock(buf, midi); if (b % 5 == 0) std::printf("released block %d rms %.4f\n", b, buf.getRMSLevel(0, 0, 512)); }
    }
    for (double sr : { 48000.0, 44100.0, 96000.0 }) {
        std::printf("--- sample rate %.0f\n", sr);
        auto proc = std::make_unique<PedalProcessor>();
        proc->setPlayConfigDetails(2, 2, sr, 512);
        proc->prepareToPlay(sr, 512);
        std::atomic<bool> run { true };
        std::thread audio([&] {
            juce::AudioBuffer<float> buf(2, 512); juce::MidiBuffer midi; std::mt19937 rng(1);
            std::uniform_real_distribution<float> d(-0.5f, 0.5f);
            for (int n = 0; run; ++n) {
                for (int c = 0; c < 2; ++c) for (int i = 0; i < 512; ++i) buf.setSample(c, i, d(rng));
                midi.clear();
                if (n % 7 == 0) { const uint8_t cc[] = { 0xB0, uint8_t(n % 32), uint8_t(n % 128) }; midi.addEvent(cc, 3, 0); }
                if (n % 50 == 0) { const uint8_t sx[] = { 0xF0, 0x7D, 0x01, 1, 0, 3, 2, 0xF7 }; midi.addEvent(sx, 8, 0); }
                proc->processBlock(buf, midi);
                for (int c = 0; c < 2; ++c) for (int i = 0; i < 512; ++i) if (!std::isfinite(buf.getSample(c, i))) { std::printf("NaN/inf in output!\n"); std::abort(); }
            }
        });
        {
            std::unique_ptr<juce::AudioProcessorEditor> ed(proc->createEditor());
            auto* editor = dynamic_cast<PedalEditor*>(ed.get());
            ed->setSize(760, 520);
            std::mt19937 rng(2); std::uniform_real_distribution<float> u(0.f, 1.f);
            for (int step = 0; step < 400; ++step) {
                mm->runDispatchLoopUntil(5);
                editor->testSetKnob(step % 6, u(rng));
                if (step % 13 == 0) editor->testClickEffect(step % 4);
                if (step % 29 == 0) editor->testSelectBank(step % 4);
                if (step % 17 == 0) editor->testMoveEffect(step % 3, +1);
                if (step % 23 == 0) editor->testSetCuts(step % 3, (step / 3) % 3, 50.f + u(rng) * 1000.f, 2000.f + u(rng) * 10000.f);
                if (step % 31 == 0) editor->testTap();
                if (step % 41 == 0) editor->testToggleHw();
                if (step % 5 == 0) proc->keyboardState.noteOn(1, 40 + (step % 48), 0.8f);
                if (step % 5 == 3) proc->keyboardState.allNotesOff(1);
                if (step % 53 == 0) proc->source = (step / 53) % 2;
                if (step % 101 == 0) proc->loadEmbeddedLoop();
                if (step % 97 == 0) { juce::MemoryBlock mb; proc->getStateInformation(mb); proc->setStateInformation(mb.getData(), (int)mb.getSize()); }
            }
            mm->runDispatchLoopUntil(50);
        }
        run = false; audio.join();
        {   // loop source must produce signal from a silent input
            proc->source = PedalProcessor::Loop; proc->hwModel = false;
            for (int e = 0; e < 4; ++e) proc->enableParam(e)->setValueNotifyingHost(0.f);
            proc->pedal().setGlobalHp(0); proc->pedal().setGlobalLp(0); proc->loopGain = 1.f;   // undo whatever the UI stress left behind
            proc->keyboardState.allNotesOff(1); { juce::AudioBuffer<float> b3(2, 512); juce::MidiBuffer m3; for (int q = 0; q < 40; ++q) { b3.clear(); m3.clear(); proc->processBlock(b3, m3); } }   // let sampler voices release
            std::printf("   loop '%s' %.1f s, gain %.2f, in trim %.1f dB, out trim %.1f dB, hp %d lp %d\n", proc->loopName().toRawUTF8(), proc->loopLengthSeconds(), proc->loopGain.load(), proc->pedal().tempo() * 0 + 0.f, 0.f, proc->pedal().globalHp(), proc->pedal().globalLp());
            const int blocks = static_cast<int>(proc->loopLengthSeconds() * sr / 512) + 1;         // one full pass of the loop
            juce::AudioBuffer<float> buf(2, 512); juce::MidiBuffer midi; double acc = 0; float pk = 0.f;
            for (int b = 0; b < blocks; ++b) { buf.clear(); midi.clear(); proc->processBlock(buf, midi); acc += buf.getRMSLevel(0, 0, 512); pk = std::max(pk, buf.getMagnitude(0, 512)); }
            std::printf("loop source: mean block rms %.4f, peak %.3f over one loop (expect rms > 0.02, peak ~0.5)\n", acc / blocks, pk);
            if (acc / blocks < 0.02 || pk < 0.3) { std::printf("LOOP SOURCE SILENT\n"); return 1; }
            // keyboard sampler: a note on a silent source must produce sound that decays after note-off
            proc->source = PedalProcessor::Silence; proc->keyboardState.noteOn(1, 52, 1.0f);
            double on = 0; for (int b2 = 0; b2 < 20; ++b2) { buf.clear(); midi.clear(); proc->processBlock(buf, midi); on += buf.getRMSLevel(0, 0, 512); }
            proc->keyboardState.noteOff(1, 52, 0.f);
            for (int b2 = 0; b2 < 60; ++b2) { buf.clear(); midi.clear(); proc->processBlock(buf, midi); if (b2 % 10 == 0) std::printf("   block %d rms %.4f\n", b2, buf.getRMSLevel(0, 0, 512)); }
            buf.clear(); midi.clear(); proc->processBlock(buf, midi); const float after = buf.getRMSLevel(0, 0, 512);
            std::printf("keys: E3 rms %.4f while held, %.5f 0.6 s after release\n", on / 20, after);
            if (on / 20 < 0.02 || after > 1e-3f) { std::printf("KEYS BROKEN\n"); return 1; }
        }
        proc->releaseResources();
        std::printf("ok at %.0f, load %.1f%%\n", sr, proc->loadPercent.load());
    }
    std::printf("harness done\n");
    return 0;
}
