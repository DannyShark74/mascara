// Loads the plugin processor like a host would: bus layouts, programs, state round trip,
// processing with MIDI, and a PNG snapshot of the editor (for checking the layout).
//   MascaraHostCheck [snapshot.png]
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <cstdio>

static int failures = 0;
static void check (bool ok, const juce::String& what)
{
    std::printf ("  [%s] %s\n", ok ? " ok " : "FAIL", what.toRawUTF8());
    if (! ok) ++failures;
}

static float runBlocks (MascaraProcessor& p, int numIn, int numOut, int blocks, bool withMidi)
{
    juce::AudioBuffer<float> buf (std::max (numIn, numOut), 512);
    juce::MidiBuffer midi;
    float peak = 0;
    double ph = 0;
    for (int b = 0; b < blocks; ++b)
    {
        buf.clear();
        for (int i = 0; i < 512; ++i)
        {
            ph += 180.0 / p.getSampleRate();
            const float x = (float) (0.2 * std::sin (2 * juce::MathConstants<double>::pi * ph) + 0.1 * std::sin (6 * juce::MathConstants<double>::pi * ph));
            for (int c = 0; c < numIn; ++c) buf.setSample (c, i, x);
        }
        midi.clear();
        if (withMidi && b == 10) midi.addEvent (juce::MidiMessage::noteOn (1, 55, (juce::uint8) 100), 0);
        if (withMidi && b == 40) midi.addEvent (juce::MidiMessage::noteOff (1, 55), 0);
        p.processBlock (buf, midi);
        for (int c = 0; c < numOut; ++c)
            for (int i = 0; i < 512; ++i)
            {
                const float v = buf.getSample (c, i);
                if (! std::isfinite (v)) return 1e9f;
                peak = std::max (peak, std::abs (v));
            }
    }
    return peak;
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    auto proc = std::make_unique<MascaraProcessor>();

    for (auto [in, out] : { std::pair { 1, 1 }, std::pair { 1, 2 }, std::pair { 2, 2 } })
    {
        juce::AudioProcessor::BusesLayout l;
        l.inputBuses.add (juce::AudioChannelSet::canonicalChannelSet (in));
        l.outputBuses.add (juce::AudioChannelSet::canonicalChannelSet (out));
        check (proc->setBusesLayout (l), "layout " + juce::String (in) + " in / " + juce::String (out) + " out accepted");
        proc->setRateAndBufferSizeDetails (48000.0, 512);
        proc->prepareToPlay (48000.0, 512);
        const float pk = runBlocks (*proc, in, out, 60, true);
        check (pk > 0.01f && pk < 2.0f, "  processes audio (peak " + juce::String (pk, 3) + ")");
    }
    {
        juce::AudioProcessor::BusesLayout l;
        l.inputBuses.add (juce::AudioChannelSet::stereo());
        l.outputBuses.add (juce::AudioChannelSet::mono());
        check (! proc->checkBusesLayoutSupported (l), "layout 2 in / 1 out rejected");
    }
    check (proc->getLatencySamples() > 0, "latency reported: " + juce::String (proc->getLatencySamples()) + " samples");

    // every factory preset processes cleanly
    bool allOk = true;
    for (int i = 0; i < proc->getNumPrograms(); ++i)
    {
        proc->setCurrentProgram (i);
        proc->reset();
        const float pk = runBlocks (*proc, 2, 2, 30, true);
        if (! (pk > 0.001f && pk < 2.5f)) { allOk = false; std::printf ("    preset %s peak %f\n", proc->getProgramName (i).toRawUTF8(), pk); }
    }
    check (allOk, juce::String (proc->getNumPrograms()) + " presets process cleanly");

    // state round trip
    proc->setCurrentProgram (5);
    proc->apvts.getParameter (mascara::pid::formant)->setValueNotifyingHost (0.8f);
    juce::MemoryBlock mb;
    proc->getStateInformation (mb);
    auto other = std::make_unique<MascaraProcessor>();
    other->setStateInformation (mb.getData(), (int) mb.getSize());
    bool same = other->getCurrentProgram() == 5;
    for (auto* prm : proc->getParameters())
    {
        auto* r = dynamic_cast<juce::RangedAudioParameter*> (prm);
        auto* o = dynamic_cast<juce::RangedAudioParameter*> (other->apvts.getParameter (r->getParameterID()));
        same = same && std::abs (r->getValue() - o->getValue()) < 1e-6f;
    }
    check (same, "state save / restore");

    // editor snapshot
    proc->setCurrentProgram (1);
    proc->apvts.getParameter (mascara::pid::pitch)->setValueNotifyingHost (proc->apvts.getParameter (mascara::pid::pitch)->convertTo0to1 (5.0f));
    proc->apvts.getParameter (mascara::pid::mode)->setValueNotifyingHost (0.5f);
    proc->reset();
    runBlocks (*proc, 2, 2, 40, false);
    std::unique_ptr<juce::AudioProcessorEditor> ed (proc->createEditor());
    if (auto* me = dynamic_cast<MascaraEditor*> (ed.get()))
        for (int i = 0; i < 20; ++i) me->refreshNow();
    check (ed != nullptr && ed->getWidth() > 0, "editor opens (" + juce::String (ed->getWidth()) + " x " + juce::String (ed->getHeight()) + ")");
    if (argc > 1)
    {
        auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 2.0f);
        juce::File f (juce::File::getCurrentWorkingDirectory().getChildFile (argv[1]));
        f.deleteFile();
        juce::FileOutputStream os (f);
        juce::PNGImageFormat().writeImageToStream (img, os);
        std::printf ("  snapshot written: %s\n", f.getFullPathName().toRawUTF8());
    }
    ed.reset();
    std::printf ("%s\n", failures ? "HOST CHECK FAILED" : "HOST CHECK PASSED");
    return failures ? 1 : 0;
}
