#include "Stft.h"
#include "../../ThirdParty/pocketfft/pocketfft_hdronly.h"
#include <cmath>
#include <algorithm>

namespace ghostcut
{

Stft::Stft (int fftSize, int hopSize) : nfft (fftSize), hop (hopSize), window ((size_t) fftSize)
{
    for (int i = 0; i < nfft; ++i)
        window[(size_t) i] = (float) (0.5 - 0.5 * std::cos (2.0 * 3.14159265358979323846 * i / nfft)); // periodic
}

static inline int reflectIndex (int i, int n)
{
    // numpy/torch "reflect": edge sample not repeated
    if (n == 1) return 0;
    const int period = 2 * (n - 1);
    i = ((i % period) + period) % period;
    return i < n ? i : period - i;
}

void Stft::forward (const float* x, int n, std::vector<std::complex<float>>& out) const
{
    const int frames = numFrames (n);
    const int bins = numBins();
    out.assign ((size_t) bins * (size_t) frames, {});

    std::vector<float> frame ((size_t) nfft);
    std::vector<std::complex<float>> spec ((size_t) bins);
    const pocketfft::shape_t shape { (size_t) nfft };
    const pocketfft::stride_t sIn { sizeof (float) }, sOut { sizeof (std::complex<float>) };
    const int half = nfft / 2;

    for (int f = 0; f < frames; ++f)
    {
        const int start = f * hop - half;
        for (int i = 0; i < nfft; ++i)
            frame[(size_t) i] = x[reflectIndex (start + i, n)] * window[(size_t) i];
        pocketfft::r2c (shape, sIn, sOut, 0, pocketfft::FORWARD, frame.data(), spec.data(), 1.0f);
        for (int b = 0; b < bins; ++b)
            out[(size_t) b * (size_t) frames + (size_t) f] = spec[(size_t) b];
    }
}

void Stft::inverse (const std::vector<std::complex<float>>& spec, int frames, int length, float* out) const
{
    const int bins = numBins();
    const int total = nfft + hop * (frames - 1);
    std::vector<float> acc ((size_t) total, 0.0f), wsum ((size_t) total, 0.0f);
    std::vector<std::complex<float>> col ((size_t) bins);
    std::vector<float> frame ((size_t) nfft);
    const pocketfft::shape_t shape { (size_t) nfft };
    const pocketfft::stride_t sIn { sizeof (std::complex<float>) }, sOut { sizeof (float) };

    for (int f = 0; f < frames; ++f)
    {
        for (int b = 0; b < bins; ++b) col[(size_t) b] = spec[(size_t) b * (size_t) frames + (size_t) f];
        pocketfft::c2r (shape, sIn, sOut, 0, pocketfft::BACKWARD, col.data(), frame.data(), 1.0f / (float) nfft);
        const int o = f * hop;
        for (int i = 0; i < nfft; ++i)
        {
            acc[(size_t) (o + i)] += frame[(size_t) i] * window[(size_t) i];
            wsum[(size_t) (o + i)] += window[(size_t) i] * window[(size_t) i];
        }
    }

    const int half = nfft / 2;
    for (int i = 0; i < length; ++i)
    {
        const int k = i + half;
        out[i] = (k < total && wsum[(size_t) k] > 1.0e-11f) ? acc[(size_t) k] / wsum[(size_t) k] : 0.0f;
    }
}

} // namespace ghostcut
