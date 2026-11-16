#pragma once
#include "PluginProcessor.h"

namespace mascara::ui
{
struct Colours
{
    static inline const juce::Colour bg      { 0xff121015 };
    static inline const juce::Colour panel   { 0xff1b1820 };
    static inline const juce::Colour border  { 0xff2d2934 };
    static inline const juce::Colour text    { 0xfff1ebe6 };
    static inline const juce::Colour dim     { 0xff978e9c };
    static inline const juce::Colour track   { 0xff2e2a35 };
    static inline const juce::Colour coral   { 0xffff6f5e };   // pitch
    static inline const juce::Colour teal    { 0xff3fd0c0 };   // formant
    static inline const juce::Colour violet  { 0xffb48cff };   // mode
    static inline const juce::Colour gold    { 0xffe8b85c };   // double / output
};

class LookAndFeel : public juce::LookAndFeel_V4
{
public:
    LookAndFeel();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end, juce::Slider&) override;
    void drawComboBox (juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool over, bool down) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override { return juce::FontOptions (14.0f); }
    juce::Font getPopupMenuFont() override { return juce::FontOptions (14.0f); }
};

class Knob : public juce::Component
{
public:
    Knob (juce::AudioProcessorValueTreeState& s, const juce::String& paramId, const juce::String& title, juce::Colour accent, bool big);
    void resized() override;
    void refresh();
    void setTitle (const juce::String& t) { name.setText (t, juce::dontSendNotification); }
    std::function<juce::String()> customText;   // overrides the parameter's own text when set
    juce::Slider slider;

private:
    juce::Label name, value;
    juce::RangedAudioParameter* param;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attach;
    bool isBig;
};

class Panel : public juce::Component
{
public:
    Panel (juce::String t, juce::Colour c) : title (std::move (t)), colour (c) {}
    void paint (juce::Graphics&) override;
    juce::String title;
    juce::Colour colour;
};

// shows the sung note, the note that comes out and how far the input is from the nearest semitone
class NoteDisplay : public juce::Component
{
public:
    void paint (juce::Graphics&) override;
    void update (float inMidi, float outMidi, bool robotMidi);
private:
    float in = -1, out = -1, shownCents = 0;
    bool midi = false;
    int holdFrames = 0;
};
} // namespace mascara::ui

class MascaraEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit MascaraEditor (MascaraProcessor&);
    ~MascaraEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void refreshNow() { timerCallback(); }   // used by the host check

private:
    void timerCallback() override;
    void refreshMode();

    MascaraProcessor& proc;
    mascara::ui::LookAndFeel lnf;

    juce::ComboBox presetBox;
    juce::TextButton prevBtn { "<" }, nextBtn { ">" };

    mascara::ui::Panel voicePanel { "VOICE", mascara::ui::Colours::coral },
                       modePanel { "MODE", mascara::ui::Colours::violet },
                       doublePanel { "DOUBLE", mascara::ui::Colours::gold },
                       outPanel { "DRIVE & OUTPUT", mascara::ui::Colours::gold },
                       trackPanel { "TRACKING", mascara::ui::Colours::violet };
    mascara::ui::NoteDisplay display;

    std::unique_ptr<mascara::ui::Knob> pitch, formant, retune, tracking, dbl, width, drive, mix, output;
    juce::TextButton linkBtn { "LINK" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> linkAttach;
    std::array<juce::TextButton, 3> modeBtns;
    juce::ComboBox keyBox, scaleBox;
    juce::Label keyLabel, scaleLabel, latencyLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> keyAttach, scaleAttach;
    int shownMode = -1, shownProgram = -1;
};
