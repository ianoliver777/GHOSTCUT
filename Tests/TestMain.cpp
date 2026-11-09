// GHOSTCUT component tests. Build target: GhostcutTests. Exit code 0 = all passed.
#include "Core/Session.h"
#include "Analysis/PhraseSegmenter.h"
#include "Audio/AudioExporter.h"
#include "Audio/AudioImporter.h"
#include "Audio/SamplerEngine.h"
#include <juce_events/juce_events.h>

using namespace ghostcut;

namespace
{
    /** Synthetic "vocal": harmonic tone with vibrato and soft attack/decay. */
    void addTone (juce::AudioBuffer<float>& b, double sr, double startSec, double lenSec, double hz)
    {
        const int s0 = (int) (startSec * sr), n = (int) (lenSec * sr);
        double phase = 0.0;
        for (int i = 0; i < n && s0 + i < b.getNumSamples(); ++i)
        {
            const double t = i / sr;
            const double f = hz * std::pow (2.0, 0.15 / 12.0 * std::sin (2.0 * juce::MathConstants<double>::pi * 5.0 * t));
            phase += 2.0 * juce::MathConstants<double>::pi * f / sr;
            const double env = std::min ({ 1.0, t / 0.04, (lenSec - t) / 0.08 });
            const double v = 0.5 * std::sin (phase) + 0.25 * std::sin (2 * phase) + 0.12 * std::sin (3 * phase);
            for (int ch = 0; ch < b.getNumChannels(); ++ch)
                b.addSample (ch, s0 + i, (float) (0.5 * env * v));
        }
    }

    std::shared_ptr<SourceSnapshot> makeTestSource (double sr = 44100.0, int channels = 1)
    {
        auto s = std::make_shared<SourceSnapshot>();
        s->sampleRate = sr;
        s->audio.setSize (channels, (int) (sr * 4.6));
        s->audio.clear();
        juce::Random rng (777);
        for (int ch = 0; ch < channels; ++ch)
            for (int i = 0; i < s->audio.getNumSamples(); ++i)
                s->audio.setSample (ch, i, (rng.nextFloat() - 0.5f) * 0.0005f); // -72 dB noise floor
        addTone (s->audio, sr, 0.50, 0.80, 220.0); // A3 = 57
        addTone (s->audio, sr, 1.90, 0.90, 329.63); // E4 = 64
        addTone (s->audio, sr, 3.40, 0.70, 440.0); // A4 = 69
        s->displayName = "test";
        s->peaks.build (s->audio);
        return s;
    }

    bool waitUntil (std::function<bool()> cond, int timeoutMs = 20000)
    {
        const auto end = juce::Time::getMillisecondCounter() + (juce::uint32) timeoutMs;
        while (! cond())
        {
            juce::MessageManager::getInstance()->runDispatchLoopUntil (10);
            if (juce::Time::getMillisecondCounter() > end) return false;
        }
        return true;
    }
}

//==============================================================================
struct SegmenterTests : juce::UnitTest
{
    SegmenterTests() : juce::UnitTest ("PhraseSegmenter", "GHOSTCUT") {}
    void runTest() override
    {
        for (double sr : { 44100.0, 48000.0, 96000.0 })
        {
            beginTest ("three phrases found @ " + juce::String (sr));
            auto src = makeTestSource (sr);
            auto frames = PhraseSegmenter::computeFrames (*src);
            auto frs = PhraseSegmenter::segment (*src, frames, {});
            expectEquals ((int) frs.size(), 3);
            if (frs.size() != 3) continue;

            const double starts[] = { 0.50, 1.90, 3.40 }, ends[] = { 1.30, 2.80, 4.10 };
            const int notes[] = { 57, 64, 69 };
            for (int i = 0; i < 3; ++i)
            {
                const double s = (double) frs[(size_t) i].start / sr, e = (double) frs[(size_t) i].end / sr;
                expectWithinAbsoluteError (s, starts[i], 0.06, "start " + juce::String (i));
                expectWithinAbsoluteError (e, ends[i], 0.15, "end " + juce::String (i));
                expectEquals (frs[(size_t) i].detectedNote, notes[i], "pitch " + juce::String (i));
                expectGreaterThan (frs[(size_t) i].features.voicedRatio, 0.8f);
                expectLessThan (frs[(size_t) i].features.pitchStabilitySemis, 0.5f);
            }
        }

        beginTest ("silence gives no fragments");
        {
            SourceSnapshot s;
            s.sampleRate = 44100;
            s.audio.setSize (1, 44100);
            s.audio.clear();
            auto f = PhraseSegmenter::computeFrames (s);
            expect (PhraseSegmenter::segment (s, f, {}).empty());
        }

        beginTest ("long phrases are split under max length");
        {
            SourceSnapshot s;
            s.sampleRate = 44100;
            s.audio.setSize (1, 44100 * 12);
            s.audio.clear();
            addTone (s.audio, 44100, 0.2, 11.5, 300.0);
            auto f = PhraseSegmenter::computeFrames (s);
            PhraseSegmenter::Settings st;
            st.maxLengthMs = 4000;
            auto frs = PhraseSegmenter::segment (s, f, st);
            expectGreaterThan ((int) frs.size(), 2);
            for (auto& fr : frs) expectLessOrEqual ((double) fr.length() / 44100.0, 4.2);
        }
    }
};

//==============================================================================
struct ExchangeTests : juce::UnitTest
{
    ExchangeTests() : juce::UnitTest ("AudioThreadExchange", "GHOSTCUT") {}
    void runTest() override
    {
        beginTest ("objects are released only after the audio thread lets go");
        AudioThreadExchange<FragmentTable> ex;
        auto a = std::make_shared<FragmentTable>();
        std::weak_ptr<FragmentTable> wa = a;
        ex.publish (std::move (a));
        bool changed = false;
        expect (ex.acquire (changed) != nullptr && changed);

        auto b = std::make_shared<FragmentTable>();
        std::weak_ptr<FragmentTable> wb = b;
        ex.publish (std::move (b));
        ex.collectGarbage();
        expect (! wa.expired(), "A still in use by audio thread");
        ex.acquire (changed);
        expect (changed);
        ex.collectGarbage();
        expect (wa.expired(), "A freed after swap");
        expect (! wb.expired());

        beginTest ("pending object replaced before pickup is freed");
        auto c = std::make_shared<FragmentTable>();
        std::weak_ptr<FragmentTable> wc = c;
        ex.publish (std::move (c));
        ex.publish (std::make_shared<FragmentTable>());
        expect (wc.expired());

        beginTest ("concurrent stress");
        std::atomic<bool> stop { false };
        std::thread audio ([&] { bool ch; while (! stop) { auto* t = ex.acquire (ch); if (t) juce::ignoreUnused (t->fragments.size()); } });
        for (int i = 0; i < 20000; ++i)
        {
            auto t = std::make_shared<FragmentTable>();
            t->fragments.resize ((size_t) (i % 7));
            ex.publish (std::move (t));
        }
        stop = true;
        audio.join();
        ex.collectGarbage();
        expect (true);
    }
};

//==============================================================================
struct EngineTests : juce::UnitTest
{
    EngineTests() : juce::UnitTest ("SamplerEngine", "GHOSTCUT") {}

    static float renderNote (SamplerEngine& eng, const SamplerEngine::Params& p, int note, int blocks, bool& finite)
    {
        juce::AudioBuffer<float> out (2, 512);
        float peak = 0.0f;
        finite = true;
        for (int b = 0; b < blocks; ++b)
        {
            juce::MidiBuffer midi;
            if (b == 0) midi.addEvent (juce::MidiMessage::noteOn (1, note, (juce::uint8) 127), 17);
            eng.process (out, midi, p);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < 512; ++i)
                {
                    const float v = out.getSample (ch, i);
                    if (! std::isfinite (v)) finite = false;
                    peak = std::max (peak, std::abs (v));
                }
        }
        return peak;
    }

    void runTest() override
    {
        auto src = makeTestSource (48000.0, 2);
        auto table = std::make_shared<FragmentTable>();
        table->fragments.push_back ({ (int64_t) (0.5 * 48000), (int64_t) (1.3 * 48000), 57 });
        table->fragments.push_back ({ (int64_t) (1.9 * 48000), (int64_t) (2.8 * 48000), 64 });

        SamplerEngine eng;
        eng.prepare (44100.0, 512);
        eng.publishSource (src);
        eng.publishFragments (table);
        SamplerEngine::Params p;
        bool finite = true;

        beginTest ("slice key plays its fragment");
        expectGreaterThan (renderNote (eng, p, 61, 20, finite), 0.1f);
        expect (finite);

        beginTest ("unmapped key is silent");
        eng.sendCommand ({ EngineCommand::Type::StopAll });
        {
            juce::AudioBuffer<float> tmp (2, 512);
            for (int b = 0; b < 10; ++b) eng.process (tmp, {}, p); // let the release finish
            expectEquals (eng.getActiveVoiceCount(), 0);
        }
        expectLessThan (renderNote (eng, p, 90, 10, finite), 1.0e-6f);

        beginTest ("chromatic mode plays selected fragment");
        p.keyMode = SamplerEngine::KeyMode::Chromatic;
        eng.setSelectedFragment (1);
        expectGreaterThan (renderNote (eng, p, 76, 20, finite), 0.1f);
        expect (finite);

        beginTest ("one-shot length follows sample-rate ratio");
        {
            SamplerEngine e2;
            e2.prepare (44100.0, 512);
            e2.publishSource (src);
            e2.publishFragments (table);
            SamplerEngine::Params q;
            juce::AudioBuffer<float> out (2, 512);
            int lastActive = 0;
            for (int b = 0; b < 200; ++b)
            {
                juce::MidiBuffer midi;
                if (b == 0) midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 127), 0);
                e2.process (out, midi, q);
                if (e2.getActiveVoiceCount() > 0) lastActive = b;
            }
            const double played = (lastActive + 1) * 512 / 44100.0;
            expectWithinAbsoluteError (played, 0.8, 0.03); // 0.8 s fragment at 48k plays 0.8 s at 44.1k
        }

        beginTest ("source swap kills voices, no crash");
        eng.publishSource (makeTestSource (44100.0, 1));
        juce::AudioBuffer<float> out (2, 512);
        eng.process (out, {}, p);
        expectEquals (eng.getActiveVoiceCount(), 0);
        eng.collectGarbage();
    }
};

//==============================================================================
struct ExportTests : juce::UnitTest
{
    ExportTests() : juce::UnitTest ("AudioExporter", "GHOSTCUT") {}
    void runTest() override
    {
        beginTest ("24-bit export is bit-exact for 24-bit material");
        auto src = makeTestSource (44100.0, 2);
        auto& a = const_cast<juce::AudioBuffer<float>&> (src->audio);
        for (int ch = 0; ch < a.getNumChannels(); ++ch)
            for (int i = 0; i < a.getNumSamples(); ++i)
                a.setSample (ch, i, std::round (a.getSample (ch, i) * 8388607.0f) / 8388607.0f);

        auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("ghostcut_export_test.wav");
        expect (AudioExporter::writeRegion (*src, 22050, 66150, file, AudioExporter::BitDepth::Int24).isEmpty());

        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatReader> r (wav.createReaderFor (file.createInputStream().release(), true));
        expect (r != nullptr);
        if (r == nullptr) return;
        expectEquals ((int) r->numChannels, 2);
        expectEquals ((int) r->bitsPerSample, 24);
        expectEquals ((int) r->lengthInSamples, 44100);
        expectEquals (r->sampleRate, 44100.0);
        juce::AudioBuffer<float> back (2, 44100);
        r->read (&back, 0, 44100, 0, true, true);
        float maxErr = 0.0f;
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < 44100; ++i)
                maxErr = std::max (maxErr, std::abs (back.getSample (ch, i) - src->audio.getSample (ch, 22050 + i)));
        expectLessThan (maxErr, 1.0e-6f);
        file.deleteFile();
    }
};

//==============================================================================
struct SessionTests : juce::UnitTest
{
    SessionTests() : juce::UnitTest ("Session", "GHOSTCUT") {}
    void runTest() override
    {
        auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("ghostcut_session_test");
        dir.deleteRecursively();
        dir.createDirectory();
        auto wavFile = dir.getChildFile ("vocal.wav");
        auto src = makeTestSource (44100.0, 1);
        expect (AudioExporter::writeRegion (*src, 0, src->numSamples(), wavFile, AudioExporter::BitDepth::Int24).isEmpty());

        SamplerEngine engine;
        juce::ValueTree saved;
        std::vector<Fragment> before;

        beginTest ("import + auto analysis");
        {
            Session s (engine);
            s.importFile (wavFile);
            expect (waitUntil ([&] { return s.hasSource() && ! s.isBusy() && ! s.getFragments().empty(); }), "import timed out");
            expectEquals ((int) s.getFragments().size(), 3);

            beginTest ("manual edits survive save");
            s.setSelection (44100 * 2, 44100 * 2 + 22050);
            s.addFragmentFromSelection();
            s.setFragmentRootOverride (0, 60);
            expectEquals ((int) s.getFragments().size(), 4);
            before = s.getFragments();
            saved = s.getStateTree();
        }

        beginTest ("state restores fragments and source");
        {
            Session s (engine);
            s.setStateTreeFromAnyThread (saved);
            expect (waitUntil ([&] { return s.hasSource() && ! s.isBusy(); }), "restore timed out");
            const auto& after = s.getFragments();
            expectEquals ((int) after.size(), (int) before.size());
            for (size_t i = 0; i < std::min (after.size(), before.size()); ++i)
            {
                expectEquals ((juce::int64) after[i].start, (juce::int64) before[i].start);
                expectEquals ((juce::int64) after[i].end, (juce::int64) before[i].end);
                expectEquals (after[i].rootOverride, before[i].rootOverride);
                expect (after[i].manual == before[i].manual);
            }
            expect (! s.hasError(), s.getStatusText());
        }

        beginTest ("missing source keeps fragments and the reference");
        {
            auto moved = dir.getChildFile ("moved.wav");
            wavFile.moveFileTo (moved);
            Session s (engine);
            s.setStateTreeFromAnyThread (saved);
            waitUntil ([&] { return ! s.isBusy(); }, 2000);
            expect (! s.hasSource());
            expect (s.hasError());
            expectEquals ((int) s.getFragments().size(), (int) before.size());
            const auto resaved = s.getStateTree();
            expectEquals (resaved.getChildWithName ("SOURCE").getProperty ("path").toString(), wavFile.getFullPathName());

            beginTest ("relocate restores audio, keeps fragments");
            s.relocateMissingSource (moved);
            expect (waitUntil ([&] { return s.hasSource() && ! s.isBusy(); }));
            expectEquals ((int) s.getFragments().size(), (int) before.size());
            expect (! s.hasError(), s.getStatusText()); // same content hash
        }

        beginTest ("trim shifts fragments into new coordinates");
        {
            Session s (engine);
            s.importFile (dir.getChildFile ("moved.wav"));
            expect (waitUntil ([&] { return s.hasSource() && ! s.isBusy() && ! s.getFragments().empty(); }));
            const auto second = s.getFragments()[1];
            s.setSelection (44100, 44100 * 3);
            s.trimToSelection();
            waitUntil ([&] { return ! s.isBusy(); });
            expectEquals ((juce::int64) s.getSnapshot()->numSamples(), (juce::int64) 88200);
            expectEquals ((juce::int64) s.getSnapshot()->trimStart, (juce::int64) 44100);
            bool found = false;
            for (auto& f : s.getFragments()) if (f.start == second.start - 44100) found = true;
            expect (found);
        }

        engine.resetWhileAudioStopped();
        dir.deleteRecursively();
    }
};

static SegmenterTests segmenterTests;
static ExchangeTests exchangeTests;
static EngineTests engineTests;
static ExportTests exportTests;
static SessionTests sessionTests;

int main()
{
    juce::ScopedJuceInitialiser_GUI init; // message manager for Session's async callbacks
    juce::UnitTestRunner runner;
    runner.setAssertOnFailure (false);
    runner.runTestsInCategory ("GHOSTCUT");

    int failures = 0;
    for (int i = 0; i < runner.getNumResults(); ++i)
        failures += runner.getResult (i)->failures;
    std::cout << (failures == 0 ? "ALL TESTS PASSED" : "TESTS FAILED: " + std::to_string (failures)) << std::endl;
    return failures == 0 ? 0 : 1;
}
