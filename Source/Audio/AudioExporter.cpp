#include "AudioExporter.h"

namespace ghostcut
{

juce::String AudioExporter::writeRegion (const SourceSnapshot& src, int64_t start, int64_t end,
                                         const juce::File& dest, BitDepth depth)
{
    start = juce::jlimit<int64_t> (0, src.numSamples(), start);
    end   = juce::jlimit<int64_t> (start, src.numSamples(), end);
    if (end <= start) return "Region is empty.";

    if (! dest.getParentDirectory().createDirectory()) return "Cannot create folder: " + dest.getParentDirectory().getFullPathName();
    dest.deleteFile();

    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> stream (dest.createOutputStream());
    if (stream == nullptr) return "Cannot write file: " + dest.getFullPathName();

    const auto options = juce::AudioFormatWriterOptions{}
                             .withSampleRate (src.sampleRate)
                             .withNumChannels (src.audio.getNumChannels())
                             .withBitsPerSample ((int) depth)
                             .withSampleFormat (depth == BitDepth::Float32
                                                    ? juce::AudioFormatWriterOptions::SampleFormat::floatingPoint
                                                    : juce::AudioFormatWriterOptions::SampleFormat::integral);

    auto writer = wav.createWriterFor (stream, options);
    if (writer == nullptr) return "WAV writer could not be created for this format.";

    if (! writer->writeFromAudioSampleBuffer (src.audio, (int) start, (int) (end - start)))
        return "Write failed (disk full?).";
    return {};
}

juce::File AudioExporter::getDragExportFolder()
{
    return juce::File::getSpecialLocation (juce::File::userMusicDirectory)
               .getChildFile ("TRIPLE SEVEN AUDIO").getChildFile ("GHOSTCUT").getChildFile ("Fragments");
}

juce::String AudioExporter::makeFragmentFileName (const SourceSnapshot& src, int fragmentNumber, int64_t start)
{
    const auto base = juce::File::createLegalFileName (src.file.getFileNameWithoutExtension());
    const int ms = (int) std::lround ((double) start / src.sampleRate * 1000.0);
    return "GHOSTCUT_" + base + "_" + juce::String (fragmentNumber).paddedLeft ('0', 3)
           + "_" + juce::String (ms) + "ms.wav";
}

} // namespace ghostcut
