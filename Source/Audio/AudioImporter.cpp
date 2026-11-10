#include "AudioImporter.h"

namespace ghostcut
{

void WaveformPeaks::build (const juce::AudioBuffer<float>& audio)
{
    levels.clear();
    const int n = audio.getNumSamples();
    const int nch = audio.getNumChannels();
    if (n == 0 || nch == 0) return;

    std::vector<Bin> base ((size_t) ((n + baseSamplesPerBin - 1) / baseSamplesPerBin));
    for (size_t b = 0; b < base.size(); ++b)
    {
        const int s0 = (int) b * baseSamplesPerBin;
        const int s1 = std::min (n, s0 + baseSamplesPerBin);
        float lo = 1.0f, hi = -1.0f;
        for (int ch = 0; ch < nch; ++ch)
        {
            const float* d = audio.getReadPointer (ch);
            for (int i = s0; i < s1; ++i) { lo = std::min (lo, d[i]); hi = std::max (hi, d[i]); }
        }
        base[b] = { lo, hi };
    }
    levels.push_back (std::move (base));

    while (levels.back().size() > 1)
    {
        const auto& prev = levels.back();
        std::vector<Bin> next ((prev.size() + 1) / 2);
        for (size_t i = 0; i < next.size(); ++i)
        {
            const auto& a = prev[i * 2];
            const auto& b = (i * 2 + 1 < prev.size()) ? prev[i * 2 + 1] : a;
            next[i] = { std::min (a.min, b.min), std::max (a.max, b.max) };
        }
        levels.push_back (std::move (next));
    }
}

AudioImporter::AudioImporter()
{
    formats.registerBasicFormats(); // WAV, AIFF, FLAC, Ogg, MP3 (JUCE_USE_MP3AUDIOFORMAT), CoreAudio on mac
}

juce::String AudioImporter::getWildcard() const
{
    return "*.wav;*.wave;*.mp3;*.aif;*.aiff;*.flac";
}

bool AudioImporter::canImport (const juce::File& f) const
{
    return f.existsAsFile() && f.hasFileExtension ("wav;wave;mp3;aif;aiff;flac");
}

juce::String AudioImporter::computeContentHash (const juce::File& file)
{
    juce::FileInputStream in (file);
    if (! in.openedOk()) return {};

    const juce::int64 size = in.getTotalLength();
    constexpr int chunk = 256 * 1024;
    juce::MemoryBlock data;

    juce::MemoryBlock head ((size_t) std::min<juce::int64> (chunk, size));
    in.read (head.getData(), (int) head.getSize());
    data.append (head.getData(), head.getSize());

    if (size > chunk * 2)
    {
        in.setPosition (size - chunk);
        juce::MemoryBlock tail ((size_t) chunk);
        in.read (tail.getData(), chunk);
        data.append (tail.getData(), tail.getSize());
    }

    data.append (&size, sizeof (size));
    // 64-bit FNV-1a: a fingerprint, not a security hash.
    uint64_t h = 1469598103934665603ull;
    auto* bytes = static_cast<const uint8_t*> (data.getData());
    for (size_t i = 0; i < data.getSize(); ++i) { h ^= bytes[i]; h *= 1099511628211ull; }
    return juce::String::toHexString ((juce::int64) h);
}

AudioImporter::Result AudioImporter::load (const juce::File& file, int64_t trimStart, int64_t trimEnd,
                                           const std::function<void (float)>& progress,
                                           const std::function<bool()>& shouldContinue)
{
    Result r;
    if (! file.existsAsFile()) { r.error = "File not found: " + file.getFullPathName(); return r; }

    std::unique_ptr<juce::AudioFormatReader> reader;
    {
        const juce::ScopedLock sl (formatLock);
        reader.reset (formats.createReaderFor (file));
    }
    if (reader == nullptr) { r.error = "Unsupported or corrupt audio file: " + file.getFileName(); return r; }
    if (reader->sampleRate <= 0 || reader->lengthInSamples <= 0) { r.error = "File contains no audio."; return r; }

    const int64_t total = reader->lengthInSamples;
    const int64_t s0 = juce::jlimit<int64_t> (0, total, trimStart);
    const int64_t s1 = trimEnd < 0 ? total : juce::jlimit<int64_t> (s0, total, trimEnd);
    const int64_t len = s1 - s0;

    if (len <= 0) { r.error = "Trim range is empty."; return r; }
    if ((double) len / reader->sampleRate > maxLengthSeconds)
    {
        r.error = "Recording is longer than 30 minutes. Trim it in your DAW first.";
        return r;
    }

    auto snap = std::make_shared<SourceSnapshot>();
    const int srcCh = (int) reader->numChannels;
    const int outCh = srcCh >= 2 ? 2 : 1;      // >2 channels: keep L/R
    snap->audio.setSize (outCh, (int) len);
    snap->sampleRate = reader->sampleRate;

    juce::AudioBuffer<float> chunkBuf (std::max (2, srcCh), 65536);
    int64_t done = 0;
    while (done < len)
    {
        if (shouldContinue && ! shouldContinue()) { r.error = "Cancelled"; return r; }
        const int n = (int) std::min<int64_t> (65536, len - done);
        reader->read (&chunkBuf, 0, n, s0 + done, true, true);
        for (int ch = 0; ch < outCh; ++ch)
            snap->audio.copyFrom (ch, (int) done, chunkBuf, ch, 0, n);
        done += n;
        if (progress) progress (0.9f * (float) done / (float) len);
    }

    snap->file = file;
    snap->displayName = file.getFileName();
    snap->fileSize = file.getSize();
    snap->contentHash = computeContentHash (file);
    snap->trimStart = s0;
    snap->trimEnd = trimEnd < 0 ? -1 : s1;
    snap->peaks.build (snap->audio);
    if (progress) progress (1.0f);

    r.snapshot = std::move (snap);
    return r;
}

std::shared_ptr<SourceSnapshot> AudioImporter::crop (const SourceSnapshot& src, int64_t start, int64_t end)
{
    start = juce::jlimit<int64_t> (0, src.numSamples(), start);
    end   = juce::jlimit<int64_t> (start, src.numSamples(), end);
    if (end - start < 16) return nullptr;

    auto snap = std::make_shared<SourceSnapshot>();
    snap->audio.setSize (src.audio.getNumChannels(), (int) (end - start));
    for (int ch = 0; ch < src.audio.getNumChannels(); ++ch)
        snap->audio.copyFrom (ch, 0, src.audio, ch, (int) start, (int) (end - start));
    snap->sampleRate  = src.sampleRate;
    snap->file        = src.file;
    snap->displayName = src.displayName;
    snap->fileSize    = src.fileSize;
    snap->contentHash = src.contentHash;
    snap->trimStart   = src.trimStart + start;
    snap->trimEnd     = src.trimStart + end;
    snap->variant     = src.variant;
    snap->peaks.build (snap->audio);
    return snap;
}

} // namespace ghostcut
