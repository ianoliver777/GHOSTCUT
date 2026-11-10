#pragma once

#include "../Core/SourceSnapshot.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <functional>

namespace ghostcut
{

/** An installed MDX-Net vocal model (ONNX). Weights are never bundled with GHOSTCUT:
    the user installs them into the Models folder. See README "Vocal separation models". */
struct VocalModelInfo
{
    juce::File file;
    juce::String displayName;
    juce::String uvrHash;      // md5 of the last 10 MB, the id UVR uses for parameter lookup
    bool known = false;        // parameters verified for this exact file
    int nfft = 7680, hop = 1024, dimF = 3072, dimT = 256;
    float compensate = 1.0f;
};

class VocalModels
{
public:
    static juce::File getFolder();                    // ~/Music/TRIPLE SEVEN AUDIO/GHOSTCUT/Models
    static juce::Array<VocalModelInfo> scan();
    static bool inspect (const juce::File&, VocalModelInfo& out, juce::String& error);
    /** Copies a model into the Models folder after checking it is a supported MDX model. */
    static bool install (const juce::File& source, VocalModelInfo& out, juce::String& error);
};

class VocalSeparator
{
public:
    enum class Method { None = 0, CenterExtract = 1, MdxModel = 2 };

    struct Options
    {
        Method method = Method::CenterExtract;
        VocalModelInfo model;    // for MdxModel
        int threads = 2;
    };

    struct Result
    {
        std::shared_ptr<SourceSnapshot> vocals; // same rate, length and channel count as the source
        juce::String error;
        bool fromCache = false;
    };

    using Progress = std::function<void (float)>;
    using KeepGoing = std::function<bool()>;

    /** Runs on a worker thread. Uses the on-disk stem cache when possible. */
    static Result separate (const SourceSnapshot& src, const Options& opt,
                            const Progress& progress, const KeepGoing& keepGoing);

    static juce::String methodId (const Options& opt);  // stable id used for cache + state
    static juce::String methodLabel (const Options& opt);
    static juce::File getCacheFolder();
    static juce::File cacheFileFor (const SourceSnapshot& src, const Options& opt);
    static juce::int64 getCacheSizeBytes();
    static void clearCache();

    // building blocks, exposed for tests
    static bool runMdx (const juce::AudioBuffer<float>& stereo44k, const VocalModelInfo& m, int threads,
                        juce::AudioBuffer<float>& vocalsOut, const Progress&, const KeepGoing&, juce::String& err);
    static bool runCenterExtract (const juce::AudioBuffer<float>& stereo, juce::AudioBuffer<float>& vocalsOut,
                                  const Progress&, const KeepGoing&);
    static juce::AudioBuffer<float> resample (const juce::AudioBuffer<float>& in, double fromRate, double toRate);
};

} // namespace ghostcut
