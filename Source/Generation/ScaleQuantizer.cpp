#include "ScaleQuantizer.h"

namespace ghostcut
{

juce::StringArray ScaleQuantizer::scaleNames()
{
    return { "Minor", "Major", "Dorian", "Phrygian", "Harmonic minor", "Pentatonic minor", "Pentatonic major", "Chromatic" };
}

juce::StringArray ScaleQuantizer::keyNames()
{
    return { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
}

const std::vector<int>& ScaleQuantizer::intervals (Scale s)
{
    static const std::vector<int> table[] = {
        { 0, 2, 3, 5, 7, 8, 10 },          // natural minor
        { 0, 2, 4, 5, 7, 9, 11 },          // major
        { 0, 2, 3, 5, 7, 9, 10 },          // dorian
        { 0, 1, 3, 5, 7, 8, 10 },          // phrygian
        { 0, 2, 3, 5, 7, 8, 11 },          // harmonic minor
        { 0, 3, 5, 7, 10 },                // pentatonic minor
        { 0, 2, 4, 7, 9 },                 // pentatonic major
        { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 },
    };
    const int i = juce::jlimit (0, (int) Scale::numScales - 1, (int) s);
    return table[i];
}

int ScaleQuantizer::degreeToMidi (int rootMidi, Scale s, int degree)
{
    const auto& iv = intervals (s);
    const int n = (int) iv.size();
    const int oct = degree >= 0 ? degree / n : -((-degree + n - 1) / n);
    const int idx = degree - oct * n;
    return rootMidi + oct * 12 + iv[(size_t) idx];
}

bool ScaleQuantizer::inScale (int midi, int key, Scale s)
{
    const int pc = ((midi - key) % 12 + 12) % 12;
    for (int v : intervals (s)) if (v == pc) return true;
    return false;
}

int ScaleQuantizer::quantize (int midi, int key, Scale s)
{
    for (int d = 0; d <= 6; ++d)
    {
        if (inScale (midi - d, key, s)) return midi - d;
        if (inScale (midi + d, key, s)) return midi + d;
    }
    return midi;
}

} // namespace ghostcut
