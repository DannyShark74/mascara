#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace mascara;

MascaraProcessor::MascaraProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "MASCARA", createLayout())
{
    pPitch = apvts.getRawParameterValue (pid::pitch);
    pFormant = apvts.getRawParameterValue (pid::formant);
    pLink = apvts.getRawParameterValue (pid::link);
    pMode = apvts.getRawParameterValue (pid::mode);
    pKey = apvts.getRawParameterValue (pid::key);
    pScale = apvts.getRawParameterValue (pid::scale);
    pRetune = apvts.getRawParameterValue (pid::retune);
    pTracking = apvts.getRawParameterValue (pid::tracking);
    pDrive = apvts.getRawParameterValue (pid::drive);
    pMix = apvts.getRawParameterValue (pid::mix);
    pDouble = apvts.getRawParameterValue (pid::dbl);
    pWidth = apvts.getRawParameterValue (pid::width);
    pOutput = apvts.getRawParameterValue (pid::output);
    heldNotes.reserve (128);
}

bool MascaraProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet(), out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo()) return false;
    if (in != juce::AudioChannelSet::mono() && in != juce::AudioChannelSet::stereo()) return false;
    return in.size() <= out.size();
}

void MascaraProcessor::prepareToPlay (double sampleRate, int)
{
    engine.setParams (readParams());
    engine.prepare (sampleRate);
    setLatencySamples (engine.getLatency());
    heldNotes.clear();
    midiNote = -1;
}

EngineParams MascaraProcessor::readParams() const
{
    EngineParams p;
    p.pitch = pPitch->load();
    p.formant = pFormant->load();
    p.link = pLink->load() > 0.5f;
    p.mode = (Mode) juce::jlimit (0, 2, juce::roundToInt (pMode->load()));
    p.key = juce::jlimit (0, 11, juce::roundToInt (pKey->load()));
    p.scale = juce::jlimit (0, Scales::count - 1, juce::roundToInt (pScale->load()));
    p.retuneMs = pRetune->load();
    p.tracking = pTracking->load();
    p.drive = pDrive->load();
    p.mix = pMix->load();
    p.doubleAmt = pDouble->load();
    p.doubleWidth = pWidth->load();
    p.outputDb = pOutput->load();
    p.midiNote = midiNote.load();
    return p;
}

void MascaraProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        if (m.isNoteOn())
        {
            heldNotes.erase (std::remove (heldNotes.begin(), heldNotes.end(), m.getNoteNumber()), heldNotes.end());
            heldNotes.push_back (m.getNoteNumber());
        }
        else if (m.isNoteOff())
            heldNotes.erase (std::remove (heldNotes.begin(), heldNotes.end(), m.getNoteNumber()), heldNotes.end());
        else if (m.isAllNotesOff() || m.isAllSoundOff())
            heldNotes.clear();
    }
    midiNote = heldNotes.empty() ? -1 : heldNotes.back();

    const int numIn = std::max (1, getTotalNumInputChannels());
    const int numOut = std::max (1, std::min (2, getTotalNumOutputChannels()));
    const int n = buffer.getNumSamples();
    if (buffer.getNumChannels() < std::max (numIn, numOut) || n == 0) return;

    engine.setParams (readParams());
    const float* in[2] = { buffer.getReadPointer (0), buffer.getReadPointer (std::min (1, numIn - 1)) };
    float* out[2] = { buffer.getWritePointer (0), buffer.getWritePointer (std::min (1, numOut - 1)) };
    engine.process (in, std::min (numIn, 2), out, numOut, n);
}

juce::AudioProcessorEditor* MascaraProcessor::createEditor() { return new MascaraEditor (*this); }

//==============================================================================
int MascaraProcessor::getNumPrograms() { return (int) factoryPresets().size(); }

const juce::String MascaraProcessor::getProgramName (int index)
{
    const auto& p = factoryPresets();
    return juce::isPositiveAndBelow (index, (int) p.size()) ? juce::String (p[(size_t) index].name) : juce::String();
}

void MascaraProcessor::setCurrentProgram (int index)
{
    const auto& presets = factoryPresets();
    if (! juce::isPositiveAndBelow (index, (int) presets.size())) return;
    program = index;
    for (auto* p : getParameters())
        if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (p))
        {
            float plain = r->convertFrom0to1 (r->getDefaultValue());
            for (const auto& [id, v] : presets[(size_t) index].values)
                if (r->getParameterID() == id) plain = v;
            r->setValueNotifyingHost (r->convertTo0to1 (plain));
        }
}

void MascaraProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("program", program, nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, destData);
}

void MascaraProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto tree = juce::ValueTree::fromXml (*xml);
            program = juce::jlimit (0, getNumPrograms() - 1, (int) tree.getProperty ("program", 0));
            apvts.replaceState (tree);
        }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new MascaraProcessor(); }
