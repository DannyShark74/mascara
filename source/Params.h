#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "Engine.h"

namespace mascara::pid
{
inline constexpr const char* pitch = "pitch";
inline constexpr const char* formant = "formant";
inline constexpr const char* link = "link";
inline constexpr const char* mode = "mode";
inline constexpr const char* key = "key";
inline constexpr const char* scale = "scale";
inline constexpr const char* retune = "retune";
inline constexpr const char* tracking = "tracking";
inline constexpr const char* drive = "drive";
inline constexpr const char* mix = "mix";
inline constexpr const char* dbl = "double";
inline constexpr const char* width = "width";
inline constexpr const char* output = "output";
} // namespace mascara::pid

namespace mascara
{
inline const juce::StringArray& keyNames()
{
    static const juce::StringArray k { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return k;
}

inline juce::String noteName (float midi)
{
    const int n = juce::roundToInt (midi);
    return keyNames()[((n % 12) + 12) % 12] + juce::String (n / 12 - 1);
}

inline juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    using F = juce::AudioParameterFloat;
    using A = juce::AudioParameterFloatAttributes;
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> v;
    auto add = [&] (const char* id, const char* name, juce::NormalisableRange<float> r, float def,
                    std::function<juce::String (float, int)> text)
    {
        v.push_back (std::make_unique<F> (juce::ParameterID { id, 1 }, name, r, def, A().withStringFromValueFunction (std::move (text))));
    };
    auto pct = [] (float x, int) { return juce::String (juce::roundToInt (x * 100.0f)) + " %"; };
    auto st  = [] (float x, int)
    {
        if (std::abs (x) < 0.005f) return juce::String ("0 st");
        return (x > 0 ? "+" : "") + juce::String (x, std::abs (x - std::round (x)) < 0.005f ? 0 : 2) + " st";
    };

    add (pid::pitch,   "Pitch",   { -12.0f, 12.0f, 0.0f }, 0.0f, st);
    add (pid::formant, "Formant", { -12.0f, 12.0f, 0.0f }, 0.0f, st);
    v.push_back (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { pid::link, 1 }, "Link", false));
    v.push_back (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { pid::mode, 1 }, "Mode",
                                                               juce::StringArray { "Transpose", "Quantize", "Robot" }, 0));
    v.push_back (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { pid::key, 1 }, "Key", keyNames(), 0));
    juce::StringArray scales;
    for (int i = 0; i < Scales::count; ++i) scales.add (Scales::name (i));
    v.push_back (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { pid::scale, 1 }, "Scale", scales, 0));
    add (pid::retune, "Retune", juce::NormalisableRange<float> (0.0f, 400.0f, 0.0f, 0.5f), 0.0f,
         [] (float x, int) { return x < 0.5f ? juce::String ("Hard") : juce::String (juce::roundToInt (x)) + " ms"; });
    add (pid::tracking, "Tracking", { 0.0f, 1.0f }, 0.5f, pct);
    add (pid::drive, "Drive", { 0.0f, 1.0f }, 0.0f, pct);
    add (pid::mix, "Mix", { 0.0f, 1.0f }, 1.0f, pct);
    add (pid::dbl, "Double", { 0.0f, 1.0f }, 0.0f, pct);
    add (pid::width, "Width", { 0.0f, 1.0f }, 0.8f, pct);
    add (pid::output, "Output", { -18.0f, 12.0f }, 0.0f,
         [] (float x, int) { return (x > 0.04f ? "+" : "") + juce::String (x, 1) + " dB"; });
    return { v.begin(), v.end() };
}

// ---- factory presets
struct Preset
{
    const char* name;
    std::vector<std::pair<const char*, float>> values;   // parameter id -> plain value; the rest is default
};

inline const std::vector<Preset>& factoryPresets()
{
    static const std::vector<Preset> p {
        { "Init", {} },
        { "Modern Double", { { pid::dbl, 0.7f }, { pid::width, 0.85f } } },
        { "Wide Gang Vocal", { { pid::dbl, 1.0f }, { pid::width, 1.0f }, { pid::formant, -0.6f } } },
        { "Chipmunk", { { pid::pitch, 7.0f }, { pid::link, 1.0f } } },
        { "Deep Voice", { { pid::pitch, -5.0f }, { pid::formant, -3.0f } } },
        { "Monster", { { pid::pitch, -12.0f }, { pid::formant, -6.0f }, { pid::drive, 0.45f } } },
        { "Helium", { { pid::formant, 7.0f } } },
        { "Big Throat", { { pid::formant, -4.0f } } },
        { "Octave Up", { { pid::pitch, 12.0f } } },
        { "Octave Down Layer", { { pid::pitch, -12.0f }, { pid::mix, 0.45f } } },
        { "Fifth Harmony", { { pid::pitch, 7.0f }, { pid::mix, 0.4f }, { pid::dbl, 0.3f } } },
        { "Hard Tune", { { pid::mode, 1.0f } } },
        { "Hard Tune A Minor", { { pid::mode, 1.0f }, { pid::key, 9.0f }, { pid::scale, 2.0f } } },
        { "Smooth Tune", { { pid::mode, 1.0f }, { pid::retune, 60.0f } } },
        { "Robot", { { pid::mode, 2.0f } } },
        { "Robot Drive", { { pid::mode, 2.0f }, { pid::drive, 0.55f }, { pid::dbl, 0.4f } } },
        { "Dark Robot", { { pid::mode, 2.0f }, { pid::pitch, -5.0f }, { pid::formant, -4.0f } } },
        { "Telephone Fuzz", { { pid::drive, 0.85f }, { pid::formant, 2.0f }, { pid::output, -4.0f } } },
    };
    return p;
}
} // namespace mascara
