// GHOSTCUT component tests. Build target: GhostcutTests. Exit code 0 = all passed.
#include "Core/Session.h"
#include "Analysis/PhraseSegmenter.h"
#include "Audio/AudioExporter.h"
#include "Audio/AudioImporter.h"
#include "Audio/SamplerEngine.h"
#include "Analysis/Stft.h"
#include "Analysis/VocalSeparator.h"
#include "Analysis/OnnxRuntime.h"
#include "Analysis/FeatureExtractor.h"
#include "Analysis/FragmentRanker.h"
#include "Analysis/SearchQuery.h"
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


//==============================================================================
// Phase 2
//==============================================================================
namespace
{
    /** Tone with controllable vibrato, glide and breath noise. */
    std::shared_ptr<SourceSnapshot> makeVoice (double sr, double len, double hzStart, double hzEnd,
                                               double vibRate, double vibDepthSemis, double noise, double attack = 0.03)
    {
        auto s = std::make_shared<SourceSnapshot>();
        s->sampleRate = sr;
        const int pad = (int) (0.3 * sr), n = (int) (len * sr);
        s->audio.setSize (1, n + 2 * pad);
        s->audio.clear();
        juce::Random rng (99);
        double phase = 0.0;
        for (int i = 0; i < n; ++i)
        {
            const double t = i / sr;
            const double base = hzStart * std::pow (hzEnd / hzStart, t / len);
            const double f = base * std::pow (2.0, vibDepthSemis / 12.0 * std::sin (2.0 * juce::MathConstants<double>::pi * vibRate * t));
            phase += 2.0 * juce::MathConstants<double>::pi * f / sr;
            const double env = std::min ({ 1.0, t / attack, (len - t) / 0.08 });
            const double v = 0.5 * std::sin (phase) + 0.25 * std::sin (2 * phase) + 0.12 * std::sin (3 * phase)
                             + noise * (rng.nextFloat() * 2.0 - 1.0);
            s->audio.setSample (0, pad + i, (float) (0.4 * env * v));
        }
        s->peaks.build (s->audio);
        return s;
    }

    Fragment measureWhole (const SourceSnapshot& s)
    {
        auto frames = PhraseSegmenter::computeFrames (s);
        Fragment f;
        f.start = (int64_t) (0.3 * s.sampleRate);
        f.end = s.numSamples() - (int64_t) (0.3 * s.sampleRate);
        PhraseSegmenter::measure (f, s, frames);
        return f;
    }

    float sdrDb (const juce::AudioBuffer<float>& ref, const juce::AudioBuffer<float>& est)
    {
        double num = 0, den = 0;
        for (int c = 0; c < ref.getNumChannels(); ++c)
            for (int i = 0; i < std::min (ref.getNumSamples(), est.getNumSamples()); ++i)
            {
                const double r = ref.getSample (c, i), e = est.getSample (c, i);
                num += r * r; den += (r - e) * (r - e);
            }
        return (float) (10.0 * std::log10 (num / std::max (den, 1e-20)));
    }

    juce::AudioBuffer<float> readWav (const juce::File& f, double& sr)
    {
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatReader> r (wav.createReaderFor (f.createInputStream().release(), true));
        juce::AudioBuffer<float> b;
        if (r == nullptr) return b;
        b.setSize ((int) r->numChannels, (int) r->lengthInSamples);
        r->read (&b, 0, b.getNumSamples(), 0, true, true);
        sr = r->sampleRate;
        return b;
    }
}

struct StftTests : juce::UnitTest
{
    StftTests() : juce::UnitTest ("Stft", "GHOSTCUT") {}
    void runTest() override
    {
        for (int nfft : { 7680, 4096 })
        {
            beginTest ("perfect reconstruction, n_fft " + juce::String (nfft));
            Stft st (nfft, 1024);
            juce::Random rng (5);
            std::vector<float> x (50000), y (50000);
            for (auto& v : x) v = rng.nextFloat() * 2.0f - 1.0f;
            std::vector<std::complex<float>> spec;
            st.forward (x.data(), (int) x.size(), spec);
            st.inverse (spec, st.numFrames ((int) x.size()), (int) x.size(), y.data());
            float err = 0.0f;
            for (size_t i = 0; i < x.size(); ++i) err = std::max (err, std::abs (x[i] - y[i]));
            expectLessThan (err, 1.0e-4f);
        }

        beginTest ("sine lands in the right bin (torch layout)");
        Stft st (7680, 1024);
        std::vector<float> x (261120);
        const int bin = 300; // 300 * 44100 / 7680 = 1722.66 Hz
        for (size_t i = 0; i < x.size(); ++i) x[i] = std::sin (2.0 * juce::MathConstants<double>::pi * bin * (double) i / 7680.0);
        std::vector<std::complex<float>> spec;
        st.forward (x.data(), (int) x.size(), spec);
        expectEquals (st.numFrames ((int) x.size()), 256); // MDX chunk -> dim_t
        const int frames = 256, mid = 128;
        int best = 0; float bestMag = 0;
        for (int b = 0; b < st.numBins(); ++b)
            if (std::abs (spec[(size_t) b * frames + mid]) > bestMag) { bestMag = std::abs (spec[(size_t) b * frames + mid]); best = b; }
        expectEquals (best, bin);
        expectWithinAbsoluteError (bestMag, 7680.0f / 4.0f, 10.0f); // hann coherent gain * N/2
    }
};

struct SeparationTests : juce::UnitTest
{
    SeparationTests() : juce::UnitTest ("VocalSeparator", "GHOSTCUT") {}
    void runTest() override
    {
        beginTest ("resample round trip 48k -> 44.1k -> 48k");
        {
            juce::AudioBuffer<float> b (1, 48000);
            for (int i = 0; i < 48000; ++i) b.setSample (0, i, (float) std::sin (2.0 * juce::MathConstants<double>::pi * 1000.0 * i / 48000.0));
            auto a = VocalSeparator::resample (b, 48000, 44100);
            expectEquals (a.getNumSamples(), 44100);
            auto c = VocalSeparator::resample (a, 44100, 48000);
            float err = 0.0f;
            for (int i = 2000; i < 46000; ++i) err = std::max (err, std::abs (c.getSample (0, i) - b.getSample (0, i)));
            expectLessThan (err, 0.02f);
        }

        beginTest ("center extract keeps centre, removes sides and bass");
        {
            const int n = 44100 * 3;
            juce::AudioBuffer<float> mix (2, n), centre (2, n);
            juce::Random rng (3);
            for (int i = 0; i < n; ++i)
            {
                const float c = 0.3f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 440.0 * i / 44100.0);
                const float side = 0.3f * (rng.nextFloat() * 2.0f - 1.0f);
                const float bass = 0.4f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 50.0 * i / 44100.0);
                centre.setSample (0, i, c); centre.setSample (1, i, c);
                mix.setSample (0, i, c + side + bass);
                mix.setSample (1, i, c - side + bass);
            }
            juce::AudioBuffer<float> out;
            expect (VocalSeparator::runCenterExtract (mix, out, {}, {}));
            expectGreaterThan (sdrDb (centre, out), sdrDb (centre, mix) + 10.0f);
        }

        beginTest ("ONNX Runtime loads from next to the binary");
        {
            auto& ort = OnnxRuntime::get();
            logMessage ("ORT: " + (ort.isAvailable() ? ort.getVersion() + " @ " + ort.getLibraryFile().getFullPathName() : ort.getError()));
           #if GHOSTCUT_HAS_ORT
            expect (ort.isAvailable(), ort.getError());
           #endif
        }

        // Real-model test: set GHOSTCUT_TEST_MODEL=/path/Kim_Vocal_2.onnx and
        // GHOSTCUT_TEST_MIX_DIR=<folder with mix.wav + voc_true.wav>
        const auto modelPath = juce::SystemStats::getEnvironmentVariable ("GHOSTCUT_TEST_MODEL", {});
        const auto mixDir = juce::SystemStats::getEnvironmentVariable ("GHOSTCUT_TEST_MIX_DIR", {});
        if (modelPath.isNotEmpty() && mixDir.isNotEmpty())
        {
            beginTest ("MDX model separates vocals (real model)");
            VocalModelInfo info; juce::String err;
            expect (VocalModels::inspect (juce::File (modelPath), info, err), err);
            double sr = 0;
            auto mix = readWav (juce::File (mixDir).getChildFile ("mix.wav"), sr);
            auto ref = readWav (juce::File (mixDir).getChildFile ("voc_true.wav"), sr);
            juce::AudioBuffer<float> voc;
            const auto t0 = juce::Time::getMillisecondCounterHiRes();
            expect (VocalSeparator::runMdx (mix, info, std::max (1, juce::SystemStats::getNumCpus() / 2), voc, {}, {}, err), err);
            const auto secs = (juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0;
            const float sdr = sdrDb (ref, voc), base = sdrDb (ref, mix);
            logMessage ("MDX SDR " + juce::String (sdr, 2) + " dB (mix baseline " + juce::String (base, 2) + " dB), "
                        + juce::String (secs, 1) + " s for " + juce::String (mix.getNumSamples() / sr, 1) + " s audio");
            expectGreaterThan (sdr, 15.0f);
        }
        else logMessage ("(skipping real-model test: GHOSTCUT_TEST_MODEL not set)");
    }
};

struct FeatureTests : juce::UnitTest
{
    FeatureTests() : juce::UnitTest ("FeatureExtractor", "GHOSTCUT") {}
    void runTest() override
    {
        beginTest ("vibrato rate and depth");
        auto vib = measureWhole (*makeVoice (44100, 2.0, 330, 330, 5.5, 0.5, 0.0));
        expectWithinAbsoluteError (vib.features.vibratoRateHz, 5.5f, 0.8f);
        expectWithinAbsoluteError (vib.features.vibratoDepthSemis, 0.5f, 0.25f);
        auto flat = measureWhole (*makeVoice (44100, 2.0, 330, 330, 0, 0, 0.0));
        expectEquals (flat.features.vibratoRateHz, 0.0f);
        expectGreaterThan (flat.features.sustainSec, 1.6f);

        beginTest ("descending glide: slope, bend, range");
        auto glide = measureWhole (*makeVoice (44100, 1.5, 440, 220, 0, 0, 0.0));
        expectLessThan (glide.features.pitchSlope, -5.0f);           // ~ -8 st/s
        expectGreaterThan (glide.features.bendSemis, 1.0f);
        expectGreaterThan (glide.features.pitchRangeSemis, 9.0f);
        expectLessThan (glide.features.sustainSec, 0.4f);

        beginTest ("breathiness: noisy > clean");
        auto breathy = measureWhole (*makeVoice (44100, 1.5, 330, 330, 0, 0, 0.6));
        expectGreaterThan (breathy.features.breathiness, flat.features.breathiness + 0.1f);
        expectGreaterThan (breathy.features.airiness, flat.features.airiness);

        beginTest ("attack time");
        auto slow = measureWhole (*makeVoice (44100, 1.5, 330, 330, 0, 0, 0.0, 0.4));
        expectGreaterThan (slow.features.attackSec, 0.2f);
        expectLessThan (flat.features.attackSec, 0.1f);

        beginTest ("scores separate obvious cases");
        auto high = measureWhole (*makeVoice (44100, 2.0, 784, 784, 0, 0, 0.0)); // G5, pure, held
        expectGreaterThan (high.scores[FragmentScores::Heavenly], high.scores[FragmentScores::Experimental]);
        expectGreaterThan (high.scores[FragmentScores::Heavenly], 0.6f);
        expectGreaterThan (vib.scores[FragmentScores::Nostalgic], flat.scores[FragmentScores::Nostalgic]);
        expectGreaterThan (glide.scores[FragmentScores::Haunted] + glide.scores[FragmentScores::Experimental],
                           flat.scores[FragmentScores::Haunted] + flat.scores[FragmentScores::Experimental]);
        expect (FragmentRanker::explain (high, FragmentScores::Heavenly, 4).contains ("held note"));

        beginTest ("syllable granularity cuts more than phrases");
        {
            auto src = std::make_shared<SourceSnapshot>();
            src->sampleRate = 44100;
            src->audio.setSize (1, 44100 * 3);
            src->audio.clear();
            // one continuous phrase of 6 "syllables": level dips between notes, no silence
            double phase = 0.0;
            const double notes[] = { 262, 294, 330, 349, 392, 440 };
            for (int i = 0; i < 44100 * 3; ++i)
            {
                const double t = i / 44100.0;
                const int k = std::min (5, (int) ((t - 0.2) / 0.43));
                const double local = std::fmod (t - 0.2, 0.43);
                const double env = (t < 0.2 || t > 2.78) ? 0.0 : 0.25 + 0.75 * std::sin (juce::MathConstants<double>::pi * local / 0.43);
                phase += 2.0 * juce::MathConstants<double>::pi * notes[std::max (0, k)] / 44100.0;
                src->audio.setSample (0, i, (float) (0.3 * env * std::sin (phase)));
            }
            auto frames = PhraseSegmenter::computeFrames (*src);
            PhraseSegmenter::Settings st;
            const auto phrases = PhraseSegmenter::segment (*src, frames, st);
            st.granularity = PhraseSegmenter::Settings::Granularity::Syllables;
            const auto syl = PhraseSegmenter::segment (*src, frames, st);
            logMessage ("phrases " + juce::String ((int) phrases.size()) + ", syllables " + juce::String ((int) syl.size()));
            expectEquals ((int) phrases.size(), 1);
            expectGreaterOrEqual ((int) syl.size(), 5);
            expectLessOrEqual ((int) syl.size(), 7);
        }
    }
};

struct SearchTests : juce::UnitTest
{
    SearchTests() : juce::UnitTest ("SearchQuery", "GHOSTCUT") {}

    static bool has (const SearchQuery& q, const juce::String& label)
    {
        for (auto& c : q.constraints) if (c.label == label) return true;
        return false;
    }

    void runTest() override
    {
        beginTest ("brief example queries map to features");
        auto q1 = SearchQuery::parse ("Airy high-pitched vocal with a sustained vowel.");
        expect (has (q1, "airy top end") && has (q1, "high register") && has (q1, "held note"));
        expect (q1.ignoredWords.isEmpty(), q1.ignoredWords.joinIntoString (","));

        auto q2 = SearchQuery::parse ("Soft, breathy vocal with descending melody.");
        expect (has (q2, "soft attack") && has (q2, "breathy") && has (q2, "descending"));

        auto q3 = SearchQuery::parse ("Long falsetto note with an unusual pitch bend.");
        expect (has (q3, "held note") && has (q3, "high register") && has (q3, "pitch bend") && has (q3, "EXPERIMENTAL score"));

        auto q4 = SearchQuery::parse ("Short ghostly vocal fragment suitable for reversing.");
        expect (has (q4, "short") && has (q4, "HAUNTED score") && has (q4, "long tail (reverses into a swell)"));
        expect (q4.ignoredWords.isEmpty(), q4.ignoredWords.joinIntoString (","));

        beginTest ("numbers, notes, negation, unknown words");
        auto q5 = SearchQuery::parse ("under 2 seconds above A3 no vibrato zorblax");
        expect (has (q5, "length <= 2.0 s"));
        expect (has (q5, "register above A3"));
        expect (has (q5, "not vibrato"));
        expect (q5.ignoredWords.contains ("zorblax"));
        auto q6 = SearchQuery::parse ("around e4 under 500ms");
        expect (has (q6, "register near E4") && has (q6, "length <= 0.50 s"));

        beginTest ("matching ranks the right fragment first");
        auto high = measureWhole (*makeVoice (44100, 2.0, 784, 784, 0, 0, 0.35)); // high, held, breathy
        auto low = measureWhole (*makeVoice (44100, 0.6, 130, 110, 0, 0, 0.0));   // low, short, falling
        expectGreaterThan (q1.match (high), q1.match (low) + 0.3f);
        auto q7 = SearchQuery::parse ("short deep descending");
        expectGreaterThan (q7.match (low), q7.match (high) + 0.3f);
    }
};

struct SeparationSessionTests : juce::UnitTest
{
    SeparationSessionTests() : juce::UnitTest ("Session separation", "GHOSTCUT") {}
    void runTest() override
    {
        auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("ghostcut_sep_test");
        dir.deleteRecursively();
        dir.createDirectory();
        auto wavFile = dir.getChildFile ("stereo_mix.wav");
        {
            auto src = makeTestSource (44100.0, 2);
            auto& a = const_cast<juce::AudioBuffer<float>&> (src->audio);
            juce::Random rng (11);
            for (int i = 0; i < a.getNumSamples(); ++i)
            {
                const float side = 0.05f * (rng.nextFloat() * 2.0f - 1.0f);
                a.addSample (0, i, side); a.addSample (1, i, -side);
            }
            expect (AudioExporter::writeRegion (*src, 0, src->numSamples(), wavFile, AudioExporter::BitDepth::Float32).isEmpty());
        }

        SamplerEngine engine;
        juce::ValueTree saved;
        VocalSeparator::Options opts;
        opts.method = VocalSeparator::Method::CenterExtract;

        beginTest ("separate -> vocals view -> re-analysis");
        {
            Session s (engine);
            s.setSeparationOptions (opts);
            s.importFile (wavFile);
            expect (waitUntil ([&] { return s.hasSource() && ! s.isBusy() && ! s.getFragments().empty(); }));
            s.separate();
            expect (waitUntil ([&] { return s.hasVocals() && ! s.isBusy(); }), s.getStatusText());
            expect (s.getView() == Session::View::Vocals);
            expect (s.getSnapshot() != s.getOriginalSnapshot());
            expectEquals ((int) s.getFragments().size(), 3);
            expect (VocalSeparator::cacheFileFor (*s.getOriginalSnapshot(), opts).existsAsFile());

            beginTest ("search + sort order");
            s.setSearchText ("high sustained");
            expect (s.getSort() == Session::Sort::Match);
            const auto order = s.getDisplayOrder();
            expectEquals (order.front(), 2); // A4 is the highest of the three
            saved = s.getStateTree();
        }

        beginTest ("restore brings back vocals from cache and the search");
        {
            Session s (engine);
            s.setStateTreeFromAnyThread (saved);
            expect (waitUntil ([&] { return s.hasVocals() && ! s.isBusy(); }), s.getStatusText());
            expect (s.getView() == Session::View::Vocals);
            expectEquals (s.getSearchText(), juce::String ("high sustained"));
            expect (s.getStatusText().contains ("cached") || s.getStatusText().contains ("ready"), s.getStatusText());
            expectEquals ((int) s.getFragments().size(), 3);
            expect (s.getFragments()[0].measured);
        }

        VocalSeparator::clearCache();
        engine.resetWhileAudioStopped();
        dir.deleteRecursively();
    }
};

static StftTests stftTests;
static SeparationTests separationTests;
static FeatureTests featureTests;
static SearchTests searchTests;
static SeparationSessionTests separationSessionTests;

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
