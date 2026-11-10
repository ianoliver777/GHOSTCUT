#pragma once

#include <juce_core/juce_core.h>
#include <memory>

namespace ghostcut
{

/**
    Loads ONNX Runtime (MIT) dynamically at run time from inside the plugin bundle:
      macOS    <bundle>/Contents/Frameworks/libonnxruntime.dylib
      Windows  <bundle>/Contents/x86_64-win/onnxruntime.dll (next to the plugin binary)
      Linux    next to the plugin binary: libonnxruntime.so
    Loading by absolute path avoids picking up a different onnxruntime.dll from the
    system or the host. If the library is missing, ML separation is simply reported as
    unavailable and the DSP fallback keeps working.
*/
class OnnxRuntime
{
public:
    static OnnxRuntime& get();

    bool isAvailable() const noexcept { return api != nullptr; }
    juce::String getError() const { return error; }
    juce::String getVersion() const { return version; }
    juce::File getLibraryFile() const { return libFile; }

    /** Runs an MDX-style model: input/output float tensors of shape [1, 4, F, T].
        Owns one session per model path; thread-safe for one caller at a time. */
    class Model
    {
    public:
        virtual ~Model() = default;
        virtual bool run (const float* input, float* output, juce::String& err) = 0;
        virtual std::vector<int64_t> inputShape() const = 0;
    };

    std::unique_ptr<Model> loadModel (const juce::File& onnxFile, int numThreads, juce::String& err);

    static juce::String libraryFileName();
    static juce::Array<juce::File> candidateLocations();

private:
    OnnxRuntime();
    juce::DynamicLibrary lib;
    const void* api = nullptr; // const OrtApi*
    juce::String error, version;
    juce::File libFile;
};

} // namespace ghostcut
