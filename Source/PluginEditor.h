#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

class MultimeterAudioProcessorEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit MultimeterAudioProcessorEditor(MultimeterAudioProcessor&);
    void paint(juce::Graphics&) override;
    void resized() override;
private:
    void timerCallback() override;
    MultimeterAudioProcessor& processor;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MultimeterAudioProcessorEditor)
};
