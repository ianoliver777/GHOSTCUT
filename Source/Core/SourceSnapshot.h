#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <memory>
#include <vector>

namespace ghostcut
{

/** Min/max overview pyramid used for fast waveform drawing. */
struct WaveformPeaks
{
    static constexpr int baseSamplesPerBin = 64;

    struct Bin { float min = 0.0f, max = 0.0f; };

    // levels[0] has baseSamplesPerBin samples per bin, each next level doubles it.
    std::vector<std::vector<Bin>> levels;

    void build (const juce::AudioBuffer<float>& audio);
    int samplesPerBin (int level) const noexcept { return baseSamplesPerBin << level; }
};

/** Immutable decoded audio for the current source.
    Created on a worker thread, then shared read-only by UI, analysis, export and
    the audio thread. Never modified after it has been published. */
struct SourceSnapshot
{
    juce::AudioBuffer<float> audio;  // 1 or 2 channels at the file's native rate
    double sampleRate = 44100.0;

    juce::File  file;               // where it came from
    juce::String displayName;
    juce::int64 fileSize = 0;
    juce::String contentHash;      // used to detect a different file at the same path
    int64_t trimStart = 0;          // offset of audio[0] within the original file
    int64_t trimEnd   = -1;         // -1 = to end of file
    juce::String variant;           // "" = the original audio, otherwise the separation method id

    WaveformPeaks peaks;

    int64_t numSamples() const noexcept { return audio.getNumSamples(); }
    double lengthSeconds() const noexcept { return (double) audio.getNumSamples() / sampleRate; }
};

using SnapshotPtr = std::shared_ptr<const SourceSnapshot>;

} // namespace ghostcut
