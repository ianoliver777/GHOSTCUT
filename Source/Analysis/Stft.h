#pragma once

#include <complex>
#include <vector>

namespace ghostcut
{

/**
    STFT / ISTFT that matches torch.stft / torch.istft with
        center=True, pad_mode="reflect", window=hann(periodic), normalized=False, onesided=True.
    That is the convention MDX-Net separation models were trained with.
    Arbitrary FFT sizes (e.g. 7680) are supported via pocketfft (BSD-3).
*/
class Stft
{
public:
    Stft (int fftSize, int hopSize);

    int fftSize() const noexcept { return nfft; }
    int hopSize() const noexcept { return hop; }
    int numBins() const noexcept { return nfft / 2 + 1; }
    int numFrames (int numSamples) const noexcept { return 1 + numSamples / hop; }

    /** out is [numBins][numFrames], bin-major. */
    void forward (const float* x, int n, std::vector<std::complex<float>>& out) const;

    /** Inverse of forward; writes exactly `length` samples. spec is [numBins][numFrames]. */
    void inverse (const std::vector<std::complex<float>>& spec, int frames, int length, float* out) const;

private:
    int nfft, hop;
    std::vector<float> window;
};

} // namespace ghostcut
