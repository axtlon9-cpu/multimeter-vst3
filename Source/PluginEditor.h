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
    void configureChoice(juce::ComboBox&, const juce::String&, const juce::StringArray&, const juce::String&);
    void attachChoice(juce::ComboBox&, const juce::String&);
    void setChoice(const juce::String&, int);
    void setBoolean(const juce::String&, bool);
    void drawAnalyzer(juce::Graphics&, juce::Rectangle<float>);
    void drawGoniometer(juce::Graphics&, juce::Rectangle<float>);
    void drawRightMeters(juce::Graphics&, juce::Rectangle<float>);

    MultimeterAudioProcessor& processor;
    juce::TextButton analyzerButton { "ANALYZER" };
    juce::TextButton goniometerButton { "GONIOMETER" };
    juce::ComboBox detectionBox, modeBox, bandsBox, topBox, rangeBox;
    juce::ComboBox holdTimeBox, returnRateBox, levelModeBox;
    juce::TextButton holdButton { "Hold" };
    juce::TextButton resetButton { "Reset" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MultimeterAudioProcessorEditor)
};
