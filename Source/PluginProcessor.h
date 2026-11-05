#pragma once
#include <JuceHeader.h>

class MultimeterAudioProcessor : public juce::AudioProcessor
{
public:
    MultimeterAudioProcessor();
    ~MultimeterAudioProcessor() override = default;
    void prepareToPlay(double, int) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout&) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Multimeter"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double get tailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override {}
    void setStateInformation(const void*, int) override {}
    float getPeak(int channel) const { return peak[(size_t) channel].load(); }
    float getRms(int channel) const { return rms[(size_t) channel].load(); }
    float getCorrelation() const { return correlation.load(); }
private:
    std::array<std::atomic<float>, 2> peak {{0.0f, 0.0f}};
    std::array<std::atomic<float>, 2> rms {{0.0f, 0.0f}};
    std::atomic<float> correlation {0.0f};
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MultimeterAudioProcessor)
};
