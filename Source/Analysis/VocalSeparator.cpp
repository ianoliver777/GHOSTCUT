#include "VocalSeparator.h"
#include "OnnxRuntime.h"
#include "Stft.h"
#include <juce_cryptography/juce_cryptography.h>

namespace ghostcut
{

//==============================================================================
// Parameters for models whose exact files were verified (input/output shape read from
// the ONNX graph, parameters from UVR's published model table, separation measured).
struct KnownModel { const char* hash; const char* name; int nfft; int dimF; int dimT; float compensate; };
static const KnownModel knownModels[] = {
    { "970b3f9492014d18fefeedfe4773cb42", "Kim Vocal 2 (MDX-Net)",        7680, 3072, 256, 1.009f },
    { "77d07b2667ddf05b9e3175941b4454a0", "UVR-MDX-NET Voc FT (MDX-Net)", 7680, 3072, 256, 1.021f },
};

juce::File VocalModels::getFolder()
{
    return juce::File::getSpecialLocation (juce::File::userMusicDirectory)
               .getChildFile ("TRIPLE SEVEN AUDIO").getChildFile ("GHOSTCUT").getChildFile ("Models");
}

static juce::String uvrHash (const juce::File& f)
{
    juce::FileInputStream in (f);
    if (! in.openedOk()) return {};
    const juce::int64 tail = 10000 * 1024;
    const juce::int64 size = in.getTotalLength();
    in.setPosition (std::max<juce::int64> (0, size - tail));
    juce::MemoryBlock mb;
    in.readIntoMemoryBlock (mb);
    return juce::MD5 (mb).toHexString();
}

bool VocalModels::inspect (const juce::File& f, VocalModelInfo& out, juce::String& error)
{
    if (! f.existsAsFile() || ! f.hasFileExtension ("onnx")) { error = "Not an .onnx file."; return false; }
    out = {};
    out.file = f;
    out.displayName = f.getFileNameWithoutExtension();
    out.uvrHash = uvrHash (f);
    for (auto& k : knownModels)
        if (out.uvrHash == k.hash)
        {
            out.known = true; out.displayName = k.name;
            out.nfft = k.nfft; out.dimF = k.dimF; out.dimT = k.dimT; out.compensate = k.compensate;
        }
    if (! out.known)
    {
        error = "Unrecognised model. Supported: Kim_Vocal_2.onnx, UVR-MDX-NET-Voc_FT.onnx (see README).";
        return false;
    }
    return true;
}

bool VocalModels::install (const juce::File& source, VocalModelInfo& out, juce::String& error)
{
    if (! inspect (source, out, error)) return false;
    const auto dest = getFolder().getChildFile (source.getFileName());
    if (dest != source)
    {
        getFolder().createDirectory();
        if (! source.copyFileTo (dest)) { error = "Could not copy model into " + getFolder().getFullPathName(); return false; }
    }
    out.file = dest;
    return true;
}

juce::Array<VocalModelInfo> VocalModels::scan()
{
    juce::Array<VocalModelInfo> list;
    for (auto& f : getFolder().findChildFiles (juce::File::findFiles, false, "*.onnx"))
    {
        VocalModelInfo m; juce::String e;
        if (inspect (f, m, e)) list.add (m);
    }
    return list;
}

//==============================================================================
juce::String VocalSeparator::methodId (const Options& o)
{
    switch (o.method)
    {
        case Method::CenterExtract: return "center1";
        case Method::MdxModel:      return "mdx_" + o.model.uvrHash.substring (0, 12);
        case Method::None:          break;
    }
    return "none";
}

juce::String VocalSeparator::methodLabel (const Options& o)
{
    switch (o.method)
    {
        case Method::CenterExtract: return "Center extract (DSP)";
        case Method::MdxModel:      return o.model.displayName;
        case Method::None:          break;
    }
    return "None";
}

juce::File VocalSeparator::getCacheFolder()
{
    return juce::File::getSpecialLocation (juce::File::userMusicDirectory)
               .getChildFile ("TRIPLE SEVEN AUDIO").getChildFile ("GHOSTCUT").getChildFile ("Cache");
}

juce::File VocalSeparator::cacheFileFor (const SourceSnapshot& s, const Options& o)
{
    return getCacheFolder().getChildFile (s.contentHash + "_" + juce::String (s.trimStart) + "_"
                                          + juce::String (s.trimStart + s.numSamples()) + "_" + methodId (o) + ".wav");
}

juce::int64 VocalSeparator::getCacheSizeBytes()
{
    juce::int64 total = 0;
    for (auto& f : getCacheFolder().findChildFiles (juce::File::findFiles, false, "*.wav;*.gcff")) total += f.getSize();
    return total;
}

void VocalSeparator::clearCache()
{
    for (auto& f : getCacheFolder().findChildFiles (juce::File::findFiles, false, "*.wav;*.gcff")) f.deleteFile();
}

//==============================================================================
juce::AudioBuffer<float> VocalSeparator::resample (const juce::AudioBuffer<float>& in, double fromRate, double toRate)
{
    // Kaiser-windowed sinc, 32 zero crossings each side. Offline only (worker threads).
    if (std::abs (fromRate - toRate) < 0.5) return in;
    const double ratio = fromRate / toRate;            // input step per output sample
    const double cutoff = std::min (1.0, 1.0 / ratio) * 0.97; // anti-alias when downsampling
    const int zc = 32;
    const double halfWidth = zc / cutoff;              // in input samples
    const int n = in.getNumSamples();
    const int outLen = (int) std::lround (n / ratio);
    juce::AudioBuffer<float> out (in.getNumChannels(), outLen);

    constexpr double beta = 8.6;
    auto bessel0 = [] (double x) { double s = 1, t = 1; for (int k = 1; k < 30; ++k) { t *= (x / (2 * k)) * (x / (2 * k)); s += t; } return s; };
    const double i0b = bessel0 (beta);

    // tabulated kernel for speed: 512 steps per input sample
    constexpr int steps = 512;
    const int tableLen = (int) std::ceil (halfWidth * steps) + 2;
    std::vector<float> table ((size_t) tableLen);
    for (int i = 0; i < tableLen; ++i)
    {
        const double x = (double) i / steps;               // distance in input samples
        const double r = x / halfWidth;
        const double w = r < 1.0 ? bessel0 (beta * std::sqrt (1.0 - r * r)) / i0b : 0.0;
        const double arg = juce::MathConstants<double>::pi * x * cutoff;
        table[(size_t) i] = (float) (cutoff * (x < 1.0e-12 ? 1.0 : std::sin (arg) / arg) * w);
    }

    for (int ch = 0; ch < in.getNumChannels(); ++ch)
    {
        const float* x = in.getReadPointer (ch);
        float* y = out.getWritePointer (ch);
        for (int j = 0; j < outLen; ++j)
        {
            const double t = j * ratio;
            const int k0 = (int) std::ceil (t - halfWidth), k1 = (int) std::floor (t + halfWidth);
            double acc = 0.0;
            for (int k = std::max (0, k0); k <= std::min (n - 1, k1); ++k)
            {
                const double d = std::abs (t - k) * steps;
                const int di = (int) d;
                const float fr = (float) (d - di);
                if (di + 1 >= tableLen) continue;
                acc += x[k] * (table[(size_t) di] + fr * (table[(size_t) di + 1] - table[(size_t) di]));
            }
            y[j] = (float) acc;
        }
    }
    return out;
}

//==============================================================================
bool VocalSeparator::runMdx (const juce::AudioBuffer<float>& mix, const VocalModelInfo& m, int threads,
                             juce::AudioBuffer<float>& vocals, const Progress& progress, const KeepGoing& keepGoing,
                             juce::String& err)
{
    auto model = OnnxRuntime::get().loadModel (m.file, threads, err);
    if (model == nullptr) return false;

    const auto shape = model->inputShape();
    if (shape.size() != 4 || shape[1] != 4 || shape[2] != m.dimF || shape[3] != m.dimT)
    {
        err = "Model input shape doesn't match an MDX vocal model.";
        return false;
    }

    const int n = mix.getNumSamples();
    const int chunk = m.hop * (m.dimT - 1);
    const int trim = m.nfft / 2;
    const int gen = chunk - 2 * trim;
    const int pad = gen - n % gen;
    const int total = trim + n + pad + trim;

    std::vector<float> padded[2];
    for (int c = 0; c < 2; ++c)
    {
        padded[c].assign ((size_t) total, 0.0f);
        const float* src = mix.getReadPointer (std::min (c, mix.getNumChannels() - 1));
        std::copy (src, src + n, padded[c].begin() + trim);
    }

    vocals.setSize (2, n);
    vocals.clear();
    Stft stft (m.nfft, m.hop);
    const int bins = stft.numBins();
    const size_t plane = (size_t) m.dimF * (size_t) m.dimT;
    std::vector<float> in (4 * plane), out (4 * plane);
    std::vector<std::complex<float>> spec;
    std::vector<std::complex<float>> full ((size_t) bins * (size_t) m.dimT);
    std::vector<float> chunkOut ((size_t) chunk);

    const int numChunks = (n + pad) / gen;
    for (int k = 0; k < numChunks; ++k)
    {
        if (keepGoing && ! keepGoing()) { err = "Cancelled"; return false; }
        const int start = k * gen;

        for (int c = 0; c < 2; ++c)
        {
            stft.forward (padded[c].data() + start, chunk, spec);
            const int frames = stft.numFrames (chunk); // == dimT
            jassert (frames == m.dimT);
            float* re = in.data() + (size_t) (2 * c) * plane;
            float* im = in.data() + (size_t) (2 * c + 1) * plane;
            for (int b = 0; b < m.dimF; ++b)
                for (int t = 0; t < m.dimT; ++t)
                {
                    const auto v = spec[(size_t) b * (size_t) frames + (size_t) t];
                    re[(size_t) b * (size_t) m.dimT + (size_t) t] = v.real();
                    im[(size_t) b * (size_t) m.dimT + (size_t) t] = v.imag();
                }
        }

        if (! model->run (in.data(), out.data(), err)) return false;

        for (int c = 0; c < 2; ++c)
        {
            std::fill (full.begin(), full.end(), std::complex<float> {});
            const float* re = out.data() + (size_t) (2 * c) * plane;
            const float* im = out.data() + (size_t) (2 * c + 1) * plane;
            for (int b = 0; b < m.dimF; ++b)
                for (int t = 0; t < m.dimT; ++t)
                    full[(size_t) b * (size_t) m.dimT + (size_t) t] = { re[(size_t) b * (size_t) m.dimT + (size_t) t],
                                                                        im[(size_t) b * (size_t) m.dimT + (size_t) t] };
            stft.inverse (full, m.dimT, chunk, chunkOut.data());
            const int dst = start; // in un-padded coordinates
            const int count = std::min (gen, n - dst);
            if (count > 0)
            {
                float* v = vocals.getWritePointer (c);
                for (int i = 0; i < count; ++i) v[dst + i] = chunkOut[(size_t) (trim + i)] * m.compensate;
            }
        }
        if (progress) progress ((float) (k + 1) / (float) numChunks);
    }
    return true;
}

bool VocalSeparator::runCenterExtract (const juce::AudioBuffer<float>& mix, juce::AudioBuffer<float>& vocals,
                                       const Progress& progress, const KeepGoing& keepGoing)
{
    // Vocals in most mixes are mixed to the centre. Keep bins where L and R are coherent
    // and level-matched, drop the low end (kick/bass) and side content.
    const int n = mix.getNumSamples();
    constexpr int nfft = 4096, hop = 1024;
    Stft stft (nfft, hop);
    const int block = hop * 512; // process in long blocks to bound memory
    const int ov = nfft * 2;
    vocals.setSize (2, n);
    vocals.clear();

    std::vector<std::complex<float>> L, R, V;
    std::vector<float> outBlock;
    const double binHz = 44100.0 / nfft;
    for (int start = 0; start < n; start += block)
    {
        if (keepGoing && ! keepGoing()) return false;
        const int a = std::max (0, start - ov), b = std::min (n, start + block + ov);
        const int len = b - a;
        stft.forward (mix.getReadPointer (0) + a, len, L);
        stft.forward (mix.getReadPointer (1) + a, len, R);
        const int frames = stft.numFrames (len), bins = stft.numBins();
        V.assign (L.size(), {});
        for (int k = 0; k < bins; ++k)
        {
            const double hz = k * binHz;
            const float hp = (float) juce::jlimit (0.0, 1.0, (hz - 90.0) / 80.0); // fade in 90..170 Hz
            for (int t = 0; t < frames; ++t)
            {
                const size_t i = (size_t) k * (size_t) frames + (size_t) t;
                const auto l = L[i], r = R[i];
                const float el = std::norm (l), er = std::norm (r);
                const float coh = 2.0f * (l.real() * r.real() + l.imag() * r.imag()) / (el + er + 1.0e-12f);
                const float mask = std::pow (juce::jlimit (0.0f, 1.0f, coh), 6.0f) * hp;
                V[i] = 0.5f * (l + r) * mask;
            }
        }
        outBlock.resize ((size_t) len);
        stft.inverse (V, frames, len, outBlock.data());
        const int copyFrom = start - a, count = std::min (block, n - start);
        for (int c = 0; c < 2; ++c)
            std::copy (outBlock.begin() + copyFrom, outBlock.begin() + copyFrom + count, vocals.getWritePointer (c) + start);
        if (progress) progress ((float) std::min (n, start + block) / (float) n);
    }
    return true;
}

//==============================================================================
static std::shared_ptr<SourceSnapshot> makeLike (const SourceSnapshot& src, juce::AudioBuffer<float>&& audio)
{
    auto s = std::make_shared<SourceSnapshot>();
    s->audio = std::move (audio);
    s->sampleRate = src.sampleRate;
    s->file = src.file;
    s->displayName = src.displayName + " [vocals]";
    s->fileSize = src.fileSize;
    s->contentHash = src.contentHash;
    s->trimStart = src.trimStart;
    s->trimEnd = src.trimEnd;
    s->peaks.build (s->audio);
    return s;
}

VocalSeparator::Result VocalSeparator::separate (const SourceSnapshot& src, const Options& opt,
                                                 const Progress& progress, const KeepGoing& keepGoing)
{
    Result r;
    if (opt.method == Method::None) { r.error = "No separation method selected."; return r; }

    // 1. cache
    const auto cache = cacheFileFor (src, opt);
    if (cache.existsAsFile())
    {
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatReader> rd (wav.createReaderFor (cache.createInputStream().release(), true));
        if (rd != nullptr && rd->lengthInSamples == src.numSamples() && (int) rd->numChannels == src.audio.getNumChannels())
        {
            juce::AudioBuffer<float> a ((int) rd->numChannels, (int) rd->lengthInSamples);
            rd->read (&a, 0, a.getNumSamples(), 0, true, true);
            r.vocals = makeLike (src, std::move (a));
            r.vocals->variant = methodId (opt);
            r.fromCache = true;
            if (progress) progress (1.0f);
            return r;
        }
    }

    // 2. to stereo @ 44.1 kHz (what the models were trained on)
    juce::AudioBuffer<float> stereo (2, (int) src.numSamples());
    stereo.copyFrom (0, 0, src.audio, 0, 0, stereo.getNumSamples());
    stereo.copyFrom (1, 0, src.audio, src.audio.getNumChannels() > 1 ? 1 : 0, 0, stereo.getNumSamples());

    if (opt.method == Method::CenterExtract && src.audio.getNumChannels() < 2)
    {
        r.error = "Center extract needs a stereo file. Install an ML vocal model for mono sources.";
        return r;
    }

    auto at44 = resample (stereo, src.sampleRate, 44100.0);
    juce::AudioBuffer<float> voc44;
    auto sub = [&] (float lo, float hi) { return [progress, lo, hi] (float p) { if (progress) progress (lo + (hi - lo) * p); }; };

    bool ok = false;
    if (opt.method == Method::MdxModel)
        ok = runMdx (at44, opt.model, opt.threads, voc44, sub (0.02f, 0.95f), keepGoing, r.error);
    else
        ok = runCenterExtract (at44, voc44, sub (0.02f, 0.95f), keepGoing);
    if (! ok) { if (r.error.isEmpty()) r.error = "Cancelled"; return r; }

    // 3. back to the source rate and channel count, exact length
    auto back = resample (voc44, 44100.0, src.sampleRate);
    juce::AudioBuffer<float> out (src.audio.getNumChannels(), (int) src.numSamples());
    out.clear();
    const int count = std::min (out.getNumSamples(), back.getNumSamples());
    if (out.getNumChannels() == 1)
    {
        out.addFrom (0, 0, back, 0, 0, count, 0.5f);
        out.addFrom (0, 0, back, 1, 0, count, 0.5f);
    }
    else
        for (int c = 0; c < 2; ++c) out.copyFrom (c, 0, back, c, 0, count);

    // 4. write cache (float WAV, source rate) — best effort
    if (getCacheFolder().createDirectory())
    {
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> os (cache.createOutputStream());
        if (os != nullptr)
        {
            auto options = juce::AudioFormatWriterOptions{}.withSampleRate (src.sampleRate)
                               .withNumChannels (out.getNumChannels()).withBitsPerSample (32)
                               .withSampleFormat (juce::AudioFormatWriterOptions::SampleFormat::floatingPoint);
            if (auto w = wav.createWriterFor (os, options))
                w->writeFromAudioSampleBuffer (out, 0, out.getNumSamples());
        }
    }

    r.vocals = makeLike (src, std::move (out));
    r.vocals->variant = methodId (opt);
    if (progress) progress (1.0f);
    return r;
}

} // namespace ghostcut
