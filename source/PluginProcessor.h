#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "Engine.h"
#include "Params.h"

class MascaraProcessor : public juce::AudioProcessor
{
public:
    MascaraProcessor();

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void reset() override { engine.reset(); }
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Mascara"; }
    bool acceptsMidi() const override { return true; }      // Robot mode follows MIDI notes
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.05; }

    int getNumPrograms() override;
    int getCurrentProgram() override { return program; }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;
    const mascara::Engine& getEngine() const { return engine; }
    int getMidiNote() const { return midiNote.load(); }

private:
    mascara::EngineParams readParams() const;

    mascara::Engine engine;
    int program = 0;
    std::vector<int> heldNotes;
    std::atomic<int> midiNote { -1 };

    std::atomic<float>* pPitch = nullptr, * pFormant = nullptr, * pLink = nullptr, * pMode = nullptr, * pKey = nullptr,
                      * pScale = nullptr, * pRetune = nullptr, * pTracking = nullptr, * pDrive = nullptr, * pMix = nullptr,
                      * pDouble = nullptr, * pWidth = nullptr, * pOutput = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MascaraProcessor)
};
