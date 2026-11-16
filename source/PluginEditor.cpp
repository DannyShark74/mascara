#include "PluginEditor.h"

using namespace mascara;
using namespace mascara::ui;

//==============================================================================
LookAndFeel::LookAndFeel()
{
    setColour (juce::ComboBox::backgroundColourId, Colours::panel);
    setColour (juce::ComboBox::outlineColourId, Colours::border);
    setColour (juce::ComboBox::textColourId, Colours::text);
    setColour (juce::ComboBox::arrowColourId, Colours::violet);
    setColour (juce::PopupMenu::backgroundColourId, Colours::panel);
    setColour (juce::PopupMenu::textColourId, Colours::text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, Colours::violet.withAlpha (0.35f));
    setColour (juce::PopupMenu::highlightedTextColourId, Colours::text);
    setColour (juce::TextButton::buttonColourId, Colours::panel);
    setColour (juce::TextButton::buttonOnColourId, Colours::violet);
    setColour (juce::TextButton::textColourOffId, Colours::dim);
    setColour (juce::TextButton::textColourOnId, Colours::bg);
    setColour (juce::Label::textColourId, Colours::text);
}

void LookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float start, float end, juce::Slider& s)
{
    const auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (4.0f);
    const float r = std::min (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto c = bounds.getCentre();
    const bool big = r > 34.0f;
    const float thick = big ? 5.0f : 3.5f;
    const float angle = start + pos * (end - start);
    const auto accent = s.isEnabled() ? s.findColour (juce::Slider::rotarySliderFillColourId) : Colours::dim.withAlpha (0.5f);

    // body with a soft gradient
    const float br = r * 0.70f;
    g.setGradientFill (juce::ColourGradient (Colours::panel.brighter (0.16f), c.x, c.y - br,
                                             Colours::panel.darker (0.25f), c.x, c.y + br, false));
    g.fillEllipse (c.x - br, c.y - br, br * 2.0f, br * 2.0f);
    g.setColour (Colours::border.brighter (0.25f));
    g.drawEllipse (c.x - br, c.y - br, br * 2.0f, br * 2.0f, 1.0f);

    juce::Path track;
    track.addCentredArc (c.x, c.y, r - thick, r - thick, 0.0f, start, end, true);
    g.setColour (Colours::track);
    g.strokePath (track, juce::PathStrokeType (thick, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    const auto range = s.getRange();
    const bool bipolar = range.getStart() < 0.0 && range.getEnd() > 0.0;
    const float from = bipolar ? start + (float) s.valueToProportionOfLength (0.0) * (end - start) : start;
    if (std::abs (angle - from) > 0.001f)
    {
        juce::Path val;
        val.addCentredArc (c.x, c.y, r - thick, r - thick, 0.0f, std::min (from, angle), std::max (from, angle), true);
        g.setColour (accent);
        g.strokePath (val, juce::PathStrokeType (thick, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    const juce::Point<float> tip (c.x + br * 0.78f * std::sin (angle), c.y - br * 0.78f * std::cos (angle));
    g.setColour (Colours::text);
    g.drawLine (juce::Line<float> (c.getPointOnCircumference (br * 0.25f, angle), tip), big ? 3.0f : 2.0f);
}

void LookAndFeel::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box)
{
    const auto b = juce::Rectangle<float> (0, 0, (float) w, (float) h).reduced (0.5f);
    g.setColour (Colours::panel.brighter (box.isMouseOver() ? 0.08f : 0.03f));
    g.fillRoundedRectangle (b, 6.0f);
    g.setColour (Colours::border.brighter (0.2f));
    g.drawRoundedRectangle (b, 6.0f, 1.0f);
    juce::Path arrow;
    const float ax = (float) w - 14.0f, ay = (float) h * 0.5f;
    arrow.addTriangle (ax - 4.0f, ay - 2.0f, ax + 4.0f, ay - 2.0f, ax, ay + 3.0f);
    g.setColour (box.isEnabled() ? Colours::violet : Colours::dim.withAlpha (0.4f));
    g.fillPath (arrow);
}

void LookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down)
{
    const auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    const auto on = b.findColour (juce::TextButton::buttonOnColourId);
    if (b.getToggleState())
    {
        g.setColour (on.withAlpha (b.isEnabled() ? 1.0f : 0.4f));
        g.fillRoundedRectangle (r, 6.0f);
        return;
    }
    g.setColour (Colours::panel.brighter (down ? 0.14f : over ? 0.08f : 0.03f));
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (Colours::border.brighter (0.2f));
    g.drawRoundedRectangle (r, 6.0f, 1.0f);
}

void LookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool)
{
    g.setFont (juce::FontOptions (11.5f, juce::Font::bold));
    g.setColour (b.getToggleState() ? Colours::bg : (b.isEnabled() ? Colours::text.withAlpha (0.85f) : Colours::dim.withAlpha (0.5f)));
    g.drawFittedText (b.getButtonText(), b.getLocalBounds().reduced (4, 2), juce::Justification::centred, 1);
}

//==============================================================================
Knob::Knob (juce::AudioProcessorValueTreeState& s, const juce::String& id, const juce::String& title, juce::Colour accent, bool big)
    : param (s.getParameter (id)), isBig (big)
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    slider.setScrollWheelEnabled (false);
    slider.setMouseDragSensitivity (big ? 300 : 220);
    slider.setColour (juce::Slider::rotarySliderFillColourId, accent);
    addAndMakeVisible (slider);
    attach = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (s, id, slider);
    slider.setDoubleClickReturnValue (true, (double) param->convertFrom0to1 (param->getDefaultValue()));

    name.setText (title, juce::dontSendNotification);
    name.setJustificationType (juce::Justification::centred);
    name.setFont (juce::FontOptions (big ? 13.5f : 11.5f, juce::Font::bold));
    name.setColour (juce::Label::textColourId, Colours::dim);
    name.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (name);

    value.setJustificationType (juce::Justification::centred);
    value.setFont (juce::FontOptions (big ? 15.0f : 12.0f, big ? juce::Font::bold : juce::Font::plain));
    value.setColour (juce::Label::textColourId, Colours::text);
    value.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (value);

    slider.onValueChange = [this] { refresh(); };
    refresh();
}

void Knob::refresh()
{
    value.setText (customText ? customText() : param->getCurrentValueAsText(), juce::dontSendNotification);
}

void Knob::resized()
{
    auto r = getLocalBounds();
    name.setBounds (r.removeFromTop (isBig ? 20 : 16));
    value.setBounds (r.removeFromBottom (isBig ? 22 : 16));
    slider.setBounds (r);
}

void Panel::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    g.setColour (Colours::panel);
    g.fillRoundedRectangle (b, 10.0f);
    g.setColour (Colours::border);
    g.drawRoundedRectangle (b.reduced (0.5f), 10.0f, 1.0f);
    auto t = b.reduced (14.0f, 9.0f).removeFromTop (14.0f);
    g.setColour (colour);
    g.fillEllipse (t.getX(), t.getCentreY() - 3.0f, 6.0f, 6.0f);
    g.setColour (Colours::dim);
    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    g.drawText (title, t.withTrimmedLeft (12.0f).toNearestInt(), juce::Justification::left);
}

//==============================================================================
void NoteDisplay::update (float inMidi, float outMidi, bool robotMidi)
{
    midi = robotMidi;
    if (inMidi > 0.0f)
    {
        in = inMidi; out = outMidi; holdFrames = 12;
        const float cents = (inMidi - std::round (inMidi)) * 100.0f;
        shownCents += (cents - shownCents) * 0.35f;
    }
    else if (holdFrames > 0 && --holdFrames == 0)
        in = out = -1.0f;
    repaint();
}

void NoteDisplay::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    g.setColour (Colours::panel);
    g.fillRoundedRectangle (b, 10.0f);
    g.setColour (Colours::border);
    g.drawRoundedRectangle (b.reduced (0.5f), 10.0f, 1.0f);

    auto r = b.reduced (16.0f, 12.0f);
    auto head = r.removeFromTop (16.0f);
    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    g.setColour (Colours::dim);
    g.drawText ("VOICE  IN  /  OUT", head.toNearestInt(), juce::Justification::left);
    const bool active = in > 0.0f;
    g.setColour (active ? Colours::teal : Colours::track);
    g.fillEllipse (head.getRight() - 9.0f, head.getCentreY() - 4.5f, 9.0f, 9.0f);
    if (midi)
    {
        g.setColour (Colours::violet);
        g.drawText ("MIDI", head.withTrimmedRight (16.0f).toNearestInt(), juce::Justification::right);
    }

    // big output note
    auto big = r.removeFromTop (78.0f);
    g.setColour (active ? Colours::text : Colours::dim.withAlpha (0.5f));
    g.setFont (juce::FontOptions (58.0f, juce::Font::bold));
    g.drawText (active ? noteName (out) : juce::String ("--"), big.toNearestInt(), juce::Justification::centred);

    // input note with cents
    auto line = r.removeFromTop (24.0f);
    g.setFont (juce::FontOptions (14.0f));
    g.setColour (Colours::dim);
    juce::String inText = "in  --";
    if (active)
    {
        const int c = juce::roundToInt ((in - std::round (in)) * 100.0f);
        inText = "in  " + noteName (in) + "  " + (c >= 0 ? "+" : "") + juce::String (c) + " ct";
    }
    g.drawText (inText, line.toNearestInt(), juce::Justification::centred);

    // tuner bar: input deviation from the nearest semitone
    r.removeFromTop (12.0f);
    auto bar = r.removeFromTop (8.0f).reduced (8.0f, 0.0f);
    g.setColour (Colours::track);
    g.fillRoundedRectangle (bar, 4.0f);
    g.setColour (Colours::dim.withAlpha (0.6f));
    g.fillRect (bar.getCentreX() - 0.5f, bar.getY() - 3.0f, 1.0f, bar.getHeight() + 6.0f);
    if (active)
    {
        const float x = bar.getCentreX() + juce::jlimit (-50.0f, 50.0f, shownCents) / 50.0f * bar.getWidth() * 0.5f;
        g.setColour (std::abs (shownCents) < 10.0f ? Colours::teal : Colours::coral);
        g.fillEllipse (x - 6.0f, bar.getCentreY() - 6.0f, 12.0f, 12.0f);
    }
    auto labels = r.removeFromTop (16.0f).reduced (4.0f, 0.0f);
    g.setFont (juce::FontOptions (10.0f));
    g.setColour (Colours::dim);
    g.drawText ("-50", labels.toNearestInt(), juce::Justification::left);
    g.drawText ("+50", labels.toNearestInt(), juce::Justification::right);
}

//==============================================================================
namespace
{
juce::Path maskPath (juce::Rectangle<float> b)
{
    // a simple carnival mask: wide band with two almond eye holes
    juce::Path p;
    const float x = b.getX(), y = b.getY(), w = b.getWidth(), h = b.getHeight();
    p.startNewSubPath (x, y + h * 0.25f);
    p.cubicTo (x + w * 0.25f, y - h * 0.05f, x + w * 0.42f, y + h * 0.10f, x + w * 0.5f, y + h * 0.22f);
    p.cubicTo (x + w * 0.58f, y + h * 0.10f, x + w * 0.75f, y - h * 0.05f, x + w, y + h * 0.25f);
    p.cubicTo (x + w * 0.98f, y + h * 0.75f, x + w * 0.70f, y + h * 1.0f, x + w * 0.58f, y + h * 0.80f);
    p.cubicTo (x + w * 0.54f, y + h * 0.72f, x + w * 0.46f, y + h * 0.72f, x + w * 0.42f, y + h * 0.80f);
    p.cubicTo (x + w * 0.30f, y + h * 1.0f, x + w * 0.02f, y + h * 0.75f, x, y + h * 0.25f);
    p.closeSubPath();
    auto eye = [&] (float cx)
    {
        p.startNewSubPath (cx - w * 0.15f, y + h * 0.48f);
        p.quadraticTo (cx, y + h * 0.26f, cx + w * 0.15f, y + h * 0.46f);
        p.quadraticTo (cx, y + h * 0.66f, cx - w * 0.15f, y + h * 0.48f);
        p.closeSubPath();
    };
    eye (x + w * 0.29f);
    eye (x + w * 0.71f);
    p.setUsingNonZeroWinding (false);
    return p;
}
} // namespace

MascaraEditor::MascaraEditor (MascaraProcessor& p) : AudioProcessorEditor (p), proc (p)
{
    setLookAndFeel (&lnf);
    auto& s = proc.apvts;

    for (auto* c : { (juce::Component*) &voicePanel, (juce::Component*) &modePanel, (juce::Component*) &doublePanel,
                     (juce::Component*) &outPanel, (juce::Component*) &trackPanel, (juce::Component*) &display })
        addAndMakeVisible (c);

    pitch = std::make_unique<Knob> (s, pid::pitch, "PITCH", Colours::coral, true);
    formant = std::make_unique<Knob> (s, pid::formant, "FORMANT", Colours::teal, true);
    retune = std::make_unique<Knob> (s, pid::retune, "RETUNE", Colours::violet, false);
    tracking = std::make_unique<Knob> (s, pid::tracking, "SENS", Colours::violet, false);
    dbl = std::make_unique<Knob> (s, pid::dbl, "AMOUNT", Colours::gold, false);
    width = std::make_unique<Knob> (s, pid::width, "WIDTH", Colours::gold, false);
    drive = std::make_unique<Knob> (s, pid::drive, "DRIVE", Colours::coral, false);
    mix = std::make_unique<Knob> (s, pid::mix, "MIX", Colours::gold, false);
    output = std::make_unique<Knob> (s, pid::output, "OUTPUT", Colours::gold, false);
    voicePanel.addAndMakeVisible (*pitch);
    voicePanel.addAndMakeVisible (*formant);
    modePanel.addAndMakeVisible (*retune);
    trackPanel.addAndMakeVisible (*tracking);
    doublePanel.addAndMakeVisible (*dbl);
    doublePanel.addAndMakeVisible (*width);
    outPanel.addAndMakeVisible (*drive);
    outPanel.addAndMakeVisible (*mix);
    outPanel.addAndMakeVisible (*output);

    linkBtn.setClickingTogglesState (true);
    linkBtn.setColour (juce::TextButton::buttonOnColourId, Colours::teal);
    linkBtn.setTooltip ("Formant follows the pitch knob (classic chipmunk / slowed-down voice)");
    linkAttach = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (s, pid::link, linkBtn);
    linkBtn.onClick = [this] { formant->refresh(); };
    voicePanel.addAndMakeVisible (linkBtn);

    const char* modeNames[3] = { "TRANSPOSE", "QUANTIZE", "ROBOT" };
    for (int i = 0; i < 3; ++i)
    {
        auto& b = modeBtns[(size_t) i];
        b.setButtonText (modeNames[i]);
        b.setColour (juce::TextButton::buttonOnColourId, Colours::violet);
        b.onClick = [this, i]
        {
            if (auto* prm = proc.apvts.getParameter (pid::mode))
            {
                prm->beginChangeGesture();
                prm->setValueNotifyingHost (prm->convertTo0to1 ((float) i));
                prm->endChangeGesture();
            }
        };
        modePanel.addAndMakeVisible (b);
    }

    keyBox.addItemList (keyNames(), 1);
    for (int i = 0; i < Scales::count; ++i) scaleBox.addItem (Scales::name (i), i + 1);
    keyAttach = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (s, pid::key, keyBox);
    scaleAttach = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (s, pid::scale, scaleBox);
    keyBox.onChange = [this] { pitch->refresh(); };
    for (auto* l : { &keyLabel, &scaleLabel })
    {
        l->setFont (juce::FontOptions (11.0f, juce::Font::bold));
        l->setColour (juce::Label::textColourId, Colours::dim);
        modePanel.addAndMakeVisible (*l);
    }
    keyLabel.setText ("KEY", juce::dontSendNotification);
    scaleLabel.setText ("SCALE", juce::dontSendNotification);
    modePanel.addAndMakeVisible (keyBox);
    modePanel.addAndMakeVisible (scaleBox);

    latencyLabel.setFont (juce::FontOptions (11.0f));
    latencyLabel.setColour (juce::Label::textColourId, Colours::dim);
    latencyLabel.setJustificationType (juce::Justification::topLeft);
    trackPanel.addAndMakeVisible (latencyLabel);

    for (int i = 0; i < proc.getNumPrograms(); ++i) presetBox.addItem (proc.getProgramName (i), i + 1);
    presetBox.onChange = [this]
    {
        const int idx = presetBox.getSelectedItemIndex();
        if (idx >= 0 && idx != proc.getCurrentProgram()) proc.setCurrentProgram (idx);
        shownProgram = idx;
    };
    prevBtn.onClick = [this] { proc.setCurrentProgram ((proc.getCurrentProgram() + proc.getNumPrograms() - 1) % proc.getNumPrograms()); };
    nextBtn.onClick = [this] { proc.setCurrentProgram ((proc.getCurrentProgram() + 1) % proc.getNumPrograms()); };
    addAndMakeVisible (presetBox);
    addAndMakeVisible (prevBtn);
    addAndMakeVisible (nextBtn);

    pitch->customText = [this]
    {
        const auto& ps = proc.apvts;
        const int m = juce::roundToInt (ps.getRawParameterValue (pid::mode)->load());
        const float v = ps.getRawParameterValue (pid::pitch)->load();
        if (m == 2)
        {
            if (proc.getMidiNote() >= 0) return noteName ((float) proc.getMidiNote()) + " (MIDI)";
            return noteName (48.0f + ps.getRawParameterValue (pid::key)->load() + std::round (v));
        }
        if (m == 1) { const int r = juce::roundToInt (v); return r == 0 ? juce::String ("0 st") : (r > 0 ? "+" : "") + juce::String (r) + " st"; }
        return ps.getParameter (pid::pitch)->getCurrentValueAsText();
    };
    formant->customText = [this]
    {
        const auto& ps = proc.apvts;
        auto text = ps.getParameter (pid::formant)->getCurrentValueAsText();
        if (ps.getRawParameterValue (pid::link)->load() > 0.5f) text << "  +link";
        return text;
    };

    setSize (860, 446);
    refreshMode();
    timerCallback();
    startTimerHz (30);
}

MascaraEditor::~MascaraEditor() { setLookAndFeel (nullptr); }

void MascaraEditor::paint (juce::Graphics& g)
{
    g.fillAll (Colours::bg);
    auto head = getLocalBounds().removeFromTop (60).reduced (18, 10);
    auto icon = head.removeFromLeft (52).toFloat().withSizeKeepingCentre (48.0f, 30.0f);
    g.setGradientFill (juce::ColourGradient (Colours::coral, icon.getX(), icon.getY(), Colours::violet, icon.getRight(), icon.getBottom(), false));
    g.fillPath (maskPath (icon));
    head.removeFromLeft (8);
    g.setColour (Colours::text);
    g.setFont (juce::FontOptions (26.0f, juce::Font::bold));
    g.drawText (juce::String::fromUTF8 ("MÁSCARA"), head.removeFromLeft (150), juce::Justification::centredLeft);
    g.setColour (Colours::dim);
    g.setFont (juce::FontOptions (12.5f));
    g.drawText ("pitch  /  formant  /  voice", head.removeFromLeft (200), juce::Justification::centredLeft);
}

void MascaraEditor::resized()
{
    auto head = getLocalBounds().removeFromTop (60).reduced (18, 14);
    nextBtn.setBounds (head.removeFromRight (32));
    head.removeFromRight (6);
    presetBox.setBounds (head.removeFromRight (220));
    head.removeFromRight (6);
    prevBtn.setBounds (head.removeFromRight (32));

    const int top = 64, h1 = 230, gap = 10, top2 = top + h1 + gap, h2 = 132;
    display.setBounds (16, top, 250, h1);
    voicePanel.setBounds (276, top, 330, h1);
    modePanel.setBounds (616, top, 228, h1);
    doublePanel.setBounds (16, top2, 250, h2);
    outPanel.setBounds (276, top2, 330, h2);
    trackPanel.setBounds (616, top2, 228, h2);

    {   // voice: two big knobs and the link switch between them
        auto r = voicePanel.getLocalBounds().reduced (12, 0).withTrimmedTop (32).withTrimmedBottom (12);
        pitch->setBounds (r.removeFromLeft (128));
        formant->setBounds (r.removeFromRight (128));
        linkBtn.setBounds (r.withSizeKeepingCentre (54, 26));
    }
    {   // mode
        auto r = modePanel.getLocalBounds().reduced (12, 0).withTrimmedTop (32).withTrimmedBottom (10);
        auto row = r.removeFromTop (28);
        const int bw = (row.getWidth() - 8) / 3;
        for (int i = 0; i < 3; ++i)
        {
            modeBtns[(size_t) i].setBounds (row.removeFromLeft (bw));
            row.removeFromLeft (4);
        }
        r.removeFromTop (10);
        auto labels = r.removeFromTop (14);
        keyLabel.setBounds (labels.removeFromLeft (70));
        labels.removeFromLeft (8);
        scaleLabel.setBounds (labels);
        auto boxes = r.removeFromTop (28);
        keyBox.setBounds (boxes.removeFromLeft (70));
        boxes.removeFromLeft (8);
        scaleBox.setBounds (boxes);
        r.removeFromTop (6);
        retune->setBounds (r.withSizeKeepingCentre (100, r.getHeight()));
    }
    {
        auto r = doublePanel.getLocalBounds().reduced (12, 0).withTrimmedTop (28).withTrimmedBottom (6);
        dbl->setBounds (r.removeFromLeft (r.getWidth() / 2));
        width->setBounds (r);
    }
    {
        auto r = outPanel.getLocalBounds().reduced (12, 0).withTrimmedTop (28).withTrimmedBottom (6);
        const int w = r.getWidth() / 3;
        drive->setBounds (r.removeFromLeft (w));
        mix->setBounds (r.removeFromLeft (w));
        output->setBounds (r);
    }
    {
        auto r = trackPanel.getLocalBounds().reduced (12, 0).withTrimmedTop (28).withTrimmedBottom (6);
        tracking->setBounds (r.removeFromLeft (90));
        latencyLabel.setBounds (r.reduced (4, 8));
    }
}

void MascaraEditor::refreshMode()
{
    const int m = juce::roundToInt (proc.apvts.getRawParameterValue (pid::mode)->load());
    shownMode = m;
    for (int i = 0; i < 3; ++i) modeBtns[(size_t) i].setToggleState (i == m, juce::dontSendNotification);
    pitch->setTitle (m == 2 ? "NOTE" : "PITCH");
    retune->setEnabled (m == 1);
    keyBox.setEnabled (m != 0);
    scaleBox.setEnabled (m == 1);
    keyLabel.setAlpha (m != 0 ? 1.0f : 0.4f);
    scaleLabel.setAlpha (m == 1 ? 1.0f : 0.4f);
    pitch->refresh();
}

void MascaraEditor::timerCallback()
{
    const int m = juce::roundToInt (proc.apvts.getRawParameterValue (pid::mode)->load());
    if (m != shownMode) refreshMode();
    if (proc.getCurrentProgram() != shownProgram)
    {
        shownProgram = proc.getCurrentProgram();
        presetBox.setSelectedItemIndex (shownProgram, juce::dontSendNotification);
    }
    const auto& e = proc.getEngine();
    display.update (e.shownInput.load(), e.shownTarget.load(), m == 2 && proc.getMidiNote() >= 0);
    if (m == 2) pitch->refresh();
    formant->refresh();

    const double sr = proc.getSampleRate() > 0 ? proc.getSampleRate() : 48000.0;
    latencyLabel.setText ("Higher catches quiet\nand breathy notes.\n\nLatency " + juce::String (juce::roundToInt (1000.0 * proc.getLatencySamples() / sr))
                              + " ms,\ncompensated by the DAW.", juce::dontSendNotification);
}
