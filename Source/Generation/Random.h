#pragma once

#include <cstdint>
#include <cmath>

namespace ghostcut
{

/** PCG32 (O'Neill, Apache-2.0 algorithm). Deterministic on every platform/compiler,
    unlike std::uniform_*_distribution, so a seed always reproduces the same pattern. */
class Random32
{
public:
    explicit Random32 (uint64_t seed, uint64_t stream = 0x777u) noexcept
    {
        inc = (stream << 1u) | 1u;
        state = 0;
        next();
        state += seed;
        next();
    }

    uint32_t next() noexcept
    {
        const uint64_t old = state;
        state = old * 6364136223846793005ULL + inc;
        const uint32_t xorshifted = (uint32_t) (((old >> 18u) ^ old) >> 27u);
        const uint32_t rot = (uint32_t) (old >> 59u);
        return (xorshifted >> rot) | (xorshifted << ((-(int32_t) rot) & 31));
    }

    /** [0, 1) */
    double uniform() noexcept { return next() * (1.0 / 4294967296.0); }
    float uniformf() noexcept { return (float) uniform(); }
    bool chance (double p) noexcept { return uniform() < p; }
    /** [lo, hi] inclusive */
    int range (int lo, int hi) noexcept { return hi <= lo ? lo : lo + (int) (next() % (uint32_t) (hi - lo + 1)); }
    float rangef (float lo, float hi) noexcept { return lo + (hi - lo) * uniformf(); }

private:
    uint64_t state = 0, inc = 0;
};

inline uint64_t hashCombine (uint64_t a, uint64_t b) noexcept
{
    a ^= b + 0x9e3779b97f4a7c15ULL + (a << 6) + (a >> 2);
    return a;
}

} // namespace ghostcut
