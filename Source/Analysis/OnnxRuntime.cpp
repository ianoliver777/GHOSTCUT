#include "OnnxRuntime.h"

#if GHOSTCUT_HAS_ORT
 #include <onnxruntime_c_api.h>
#endif

namespace ghostcut
{

juce::String OnnxRuntime::libraryFileName()
{
   #if JUCE_MAC
    return "libonnxruntime.dylib";
   #elif JUCE_WINDOWS
    return "onnxruntime.dll";
   #else
    return "libonnxruntime.so";
   #endif
}

juce::Array<juce::File> OnnxRuntime::candidateLocations()
{
    const auto exe = juce::File::getSpecialLocation (juce::File::currentExecutableFile);
    juce::Array<juce::File> c;
   #if JUCE_MAC
    c.add (exe.getParentDirectory().getSiblingFile ("Frameworks").getChildFile (libraryFileName()));
   #endif
    c.add (exe.getSiblingFile (libraryFileName()));
    return c;
}

OnnxRuntime& OnnxRuntime::get()
{
    static OnnxRuntime instance; // thread-safe init (C++11)
    return instance;
}

#if ! GHOSTCUT_HAS_ORT

OnnxRuntime::OnnxRuntime() { error = "This build was made without ONNX Runtime."; }
std::unique_ptr<OnnxRuntime::Model> OnnxRuntime::loadModel (const juce::File&, int, juce::String& err) { err = error; return nullptr; }

#else

namespace
{
    const OrtApi* ortApi (const void* p) { return static_cast<const OrtApi*> (p); }

    juce::String takeError (const OrtApi* api, OrtStatus* s)
    {
        if (s == nullptr) return {};
        juce::String msg (api->GetErrorMessage (s));
        api->ReleaseStatus (s);
        return msg.isEmpty() ? juce::String ("ONNX Runtime error") : msg;
    }

    class OrtModel final : public OnnxRuntime::Model
    {
    public:
        OrtModel (const OrtApi* a) : api (a) {}

        ~OrtModel() override
        {
            if (memInfo)  api->ReleaseMemoryInfo (memInfo);
            if (session)  api->ReleaseSession (session);
            if (options)  api->ReleaseSessionOptions (options);
            if (env)      api->ReleaseEnv (env);
        }

        juce::String open (const juce::File& file, int threads)
        {
            juce::String e;
            if ((e = takeError (api, api->CreateEnv (ORT_LOGGING_LEVEL_WARNING, "GHOSTCUT", &env))).isNotEmpty()) return e;
            if ((e = takeError (api, api->CreateSessionOptions (&options))).isNotEmpty()) return e;
            api->SetIntraOpNumThreads (options, std::max (1, threads));
            api->SetInterOpNumThreads (options, 1);
            api->SetSessionGraphOptimizationLevel (options, ORT_ENABLE_ALL);

           #if JUCE_WINDOWS
            const auto path = file.getFullPathName();
            e = takeError (api, api->CreateSession (env, path.toWideCharPointer(), options, &session));
           #else
            e = takeError (api, api->CreateSession (env, file.getFullPathName().toRawUTF8(), options, &session));
           #endif
            if (e.isNotEmpty()) return e;

            OrtAllocator* alloc = nullptr;
            api->GetAllocatorWithDefaultOptions (&alloc);
            size_t nIn = 0, nOut = 0;
            api->SessionGetInputCount (session, &nIn);
            api->SessionGetOutputCount (session, &nOut);
            if (nIn != 1 || nOut < 1) return "Model must have exactly one input and at least one output.";

            char* name = nullptr;
            if ((e = takeError (api, api->SessionGetInputName (session, 0, alloc, &name))).isNotEmpty()) return e;
            inName = name; api->AllocatorFree (alloc, name);
            if ((e = takeError (api, api->SessionGetOutputName (session, 0, alloc, &name))).isNotEmpty()) return e;
            outName = name; api->AllocatorFree (alloc, name);

            OrtTypeInfo* ti = nullptr;
            if ((e = takeError (api, api->SessionGetInputTypeInfo (session, 0, &ti))).isNotEmpty()) return e;
            const OrtTensorTypeAndShapeInfo* tsi = nullptr;
            api->CastTypeInfoToTensorInfo (ti, &tsi);
            size_t dims = 0;
            api->GetDimensionsCount (tsi, &dims);
            shape.resize (dims);
            api->GetDimensions (tsi, shape.data(), dims);
            ONNXTensorElementDataType type {};
            api->GetTensorElementType (tsi, &type);
            api->ReleaseTypeInfo (ti);
            if (type != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT) return "Model input must be float32.";
            if (! shape.empty() && shape[0] <= 0) shape[0] = 1; // dynamic batch -> 1

            return takeError (api, api->CreateCpuMemoryInfo (OrtArenaAllocator, OrtMemTypeDefault, &memInfo));
        }

        std::vector<int64_t> inputShape() const override { return shape; }

        bool run (const float* input, float* output, juce::String& err) override
        {
            size_t count = 1;
            for (auto d : shape) count *= (size_t) std::max<int64_t> (1, d);

            OrtValue* in = nullptr;
            err = takeError (api, api->CreateTensorWithDataAsOrtValue (memInfo, const_cast<float*> (input), count * sizeof (float),
                                                                       shape.data(), shape.size(),
                                                                       ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, &in));
            if (err.isNotEmpty()) return false;

            const char* inNames[]  = { inName.c_str() };
            const char* outNames[] = { outName.c_str() };
            OrtValue* out = nullptr;
            err = takeError (api, api->Run (session, nullptr, inNames, &in, 1, outNames, 1, &out));
            api->ReleaseValue (in);
            if (err.isNotEmpty()) return false;

            OrtTensorTypeAndShapeInfo* oi = nullptr;
            api->GetTensorTypeAndShape (out, &oi);
            size_t outCount = 0;
            api->GetTensorShapeElementCount (oi, &outCount);
            api->ReleaseTensorTypeAndShapeInfo (oi);

            float* data = nullptr;
            err = takeError (api, api->GetTensorMutableData (out, (void**) &data));
            if (err.isEmpty() && outCount == count) std::memcpy (output, data, count * sizeof (float));
            else if (err.isEmpty()) err = "Model output shape differs from input shape.";
            api->ReleaseValue (out);
            return err.isEmpty();
        }

    private:
        const OrtApi* api;
        OrtEnv* env = nullptr;
        OrtSessionOptions* options = nullptr;
        OrtSession* session = nullptr;
        OrtMemoryInfo* memInfo = nullptr;
        std::string inName, outName;
        std::vector<int64_t> shape;
    };
}

OnnxRuntime::OnnxRuntime()
{
    for (auto& f : candidateLocations())
    {
        if (! f.existsAsFile()) continue;
        if (! lib.open (f.getFullPathName())) { error = "Could not load " + f.getFullPathName(); continue; }

        using GetApiBase = const OrtApiBase* (ORT_API_CALL*)();
        auto fn = (GetApiBase) lib.getFunction ("OrtGetApiBase");
        if (fn == nullptr) { error = "OrtGetApiBase missing in " + f.getFileName(); lib.close(); continue; }

        const OrtApiBase* base = fn();
        api = base->GetApi (ORT_API_VERSION);
        if (api == nullptr)
        {
            error = "ONNX Runtime " + juce::String (base->GetVersionString()) + " is older than this build needs.";
            lib.close();
            continue;
        }
        version = base->GetVersionString();
        libFile = f;
        error.clear();
        return;
    }
    if (error.isEmpty()) error = "ONNX Runtime library not found inside the plugin bundle.";
}

std::unique_ptr<OnnxRuntime::Model> OnnxRuntime::loadModel (const juce::File& onnxFile, int numThreads, juce::String& err)
{
    if (api == nullptr) { err = error; return nullptr; }
    if (! onnxFile.existsAsFile()) { err = "Model file not found: " + onnxFile.getFullPathName(); return nullptr; }
    auto m = std::make_unique<OrtModel> (ortApi (api));
    err = m->open (onnxFile, numThreads);
    if (err.isNotEmpty()) return nullptr;
    return m;
}

#endif

} // namespace ghostcut
