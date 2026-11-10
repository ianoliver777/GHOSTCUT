#pragma once

#include <cmath>
#include <algorithm>
#include <vector>

namespace ghostcut::dsp
{

/** Allocation-free RBJ biquad (Transposed Direct Form II). Coefficients can be
    recomputed on the audio thread. */
struct Biquad
{
    enum class Type { LowPass, HighPass, LowShelf, HighShelf, Peak, BandPass };

    float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    float z1 = 0, z2 = 0;

    void reset() noexcept { z1 = z2 = 0; }

    void set (Type t, double sr, double freq, double q = 0.7071, double gainDb = 0.0) noexcept
    {
        freq = std::clamp (freq, 10.0, sr * 0.49);
        const double w = 2.0 * 3.14159265358979323846 * freq / sr;
        const double cw = std::cos (w), sw = std::sin (w);
        const double alpha = sw / (2.0 * std::max (0.05, q));
        const double A = std::pow (10.0, gainDb / 40.0);
        double B0 = 1, B1 = 0, B2 = 0, A0 = 1, A1 = 0, A2 = 0;
        switch (t)
        {
            case Type::LowPass:  B0 = (1 - cw) / 2; B1 = 1 - cw; B2 = (1 - cw) / 2; A0 = 1 + alpha; A1 = -2 * cw; A2 = 1 - alpha; break;
            case Type::HighPass: B0 = (1 + cw) / 2; B1 = -(1 + cw); B2 = (1 + cw) / 2; A0 = 1 + alpha; A1 = -2 * cw; A2 = 1 - alpha; break;
            case Type::BandPass: B0 = alpha; B1 = 0; B2 = -alpha; A0 = 1 + alpha; A1 = -2 * cw; A2 = 1 - alpha; break;
            case Type::Peak:     B0 = 1 + alpha * A; B1 = -2 * cw; B2 = 1 - alpha * A; A0 = 1 + alpha / A; A1 = -2 * cw; A2 = 1 - alpha / A; break;
            case Type::LowShelf:
            {
                const double s = 2 * std::sqrt (A) * alpha;
                B0 = A * ((A + 1) - (A - 1) * cw + s); B1 = 2 * A * ((A - 1) - (A + 1) * cw); B2 = A * ((A + 1) - (A - 1) * cw - s);
                A0 = (A + 1) + (A - 1) * cw + s; A1 = -2 * ((A - 1) + (A + 1) * cw); A2 = (A + 1) + (A - 1) * cw - s;
                break;
            }
            case Type::HighShelf:
            {
                const double s = 2 * std::sqrt (A) * alpha;
                B0 = A * ((A + 1) + (A - 1) * cw + s); B1 = -2 * A * ((A - 1) + (A + 1) * cw); B2 = A * ((A + 1) + (A - 1) * cw - s);
                A0 = (A + 1) - (A - 1) * cw + s; A1 = 2 * ((A - 1) - (A + 1) * cw); A2 = (A + 1) - (A - 1) * cw - s;
                break;
            }
        }
        b0 = (float) (B0 / A0); b1 = (float) (B1 / A0); b2 = (float) (B2 / A0);
        a1 = (float) (A1 / A0); a2 = (float) (A2 / A0);
    }

    inline float process (float x) noexcept
    {
        const float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
};

/** Fixed-capacity delay line with fractional (cubic) reads. Capacity set in prepare. */
class DelayLine
{
public:
    void prepare (int maxSamples) { buf.assign ((size_t) std::max (4, maxSamples + 4), 0.0f); w = 0; }
    void clear() noexcept { std::fill (buf.begin(), buf.end(), 0.0f); }
    int capacity() const noexcept { return (int) buf.size() - 4; }

    inline void push (float x) noexcept { buf[(size_t) w] = x; if (++w >= (int) buf.size()) w = 0; }

    /** delay in samples (>= 1) measured from the most recent push. */
    inline float read (float delay) const noexcept
    {
        delay = std::clamp (delay, 1.0f, (float) capacity());
        const int n = (int) buf.size();
        float pos = (float) w - delay;
        while (pos < 0) pos += (float) n;
        const int i1 = (int) pos;
        const float t = pos - (float) i1;
        const float y0 = buf[(size_t) ((i1 - 1 + n) % n)], y1 = buf[(size_t) i1];
        const float y2 = buf[(size_t) ((i1 + 1) % n)], y3 = buf[(size_t) ((i1 + 2) % n)];
        const float c1 = 0.5f * (y2 - y0), c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3, c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * t + c2) * t + c1) * t + y1;
    }

    inline float readInt (int delay) const noexcept
    {
        const int n = (int) buf.size();
        return buf[(size_t) ((w - std::clamp (delay, 1, n - 1) + n) % n)];
    }

private:
    std::vector<float> buf;
    int w = 0;
};

/** Linear control smoother: moves toward `target` at a fixed rate and lands on it
    exactly (so 0 really is off and 1 really is fully wet). No allocation. */
struct Smooth
{
    float value = 0, target = 0, rate = 0.001f;
    /** ms to traverse `range` (e.g. 1 for 0..1 controls). */
    void setTime (double sr, double ms, float range = 1.0f) { rate = (float) (range / std::max (1.0, ms * 0.001 * sr)); }
    void reset (float v) { value = target = v; }
    inline float next() noexcept
    {
        const float d = target - value;
        value = std::abs (d) <= rate ? target : value + (d > 0 ? rate : -rate);
        return value;
    }
};

/** Exponential glide (for delay times: smooth tape-like pitch bend on tempo changes). */
struct Glide
{
    float value = 0, target = 0, coeff = 0.999f;
    void setTime (double sr, double ms) { coeff = (float) std::exp (-1.0 / (std::max (0.1, ms) * 0.001 * sr)); }
    void reset (float v) { value = target = v; }
    inline float next() noexcept { value = target + coeff * (value - target); return value; }
};

} // namespace ghostcut::dsp
