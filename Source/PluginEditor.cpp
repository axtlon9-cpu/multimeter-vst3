#include "PluginEditor.h"

MultimeterAudioProcessorEditor::MultimeterAudioProcessorEditor(MultimeterAudioProcessor& p)
 : AudioProcessorEditor(&p), processor(p)
{
    setSize(440, 280);
    startTimerHz(30);
}

void MultimeterAudioProcessorEditor::timerCallback() { repaint(); }

void MultimeterAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff101820));
    g.setColour(juce::Colour(0xfff2f5f7));
    g.setFont(juce::Font(24.0f, juce::Font::bold));
    g.drawText("MULTIMETER", 20, 14, 400, 34, juce::Justification::centredLeft);
    g.setFont(13.0f);
    g.setColour(juce::Colour(0xff9fb3c8));
    g.drawText("STEREO LEVEL  /  PEAK  /  RMS  /  CORRELATION", 20, 48, 400, 22, juce::Justification::centredLeft);
    for (int ch = 0; ch < 2; ++ch)
    {
        const int y = 100 + ch * 72;
        const float pk = juce::jlimit(0.0f, 1.0f, processor.getPeak(ch));
        const float rv = juce::jlimit(0.0f, 1.0f, processor.getRms(ch));
        g.setColour(juce::Colour(0xffdce6ef));
        g.setFont(16.0f);
        g.drawText(ch == 0 ? "L" : "R", 22, y, 22, 24, juce::Justification::centred);
        g.setColour(juce::Colour(0xff263746));
        g.fillRoundedRectangle(54.0f, (float)y + 2.0f, 340.0f, 20.0f, 5.0f);
        g.setColour(juce::Colour(0xff31d0aa));
        g.fillRoundedRectangle(54.0f, (float)y + 2.0f, 340.0f * rv, 20.0f, 5.0f);
        g.setColour(juce::Colour(0xffffc857));
        g.fillRoundedRectangle(54.0f, (float)y + 27.0f, 340.0f * pk, 8.0f, 3.0f);
        const auto db = [](float v) { return v > 0.000001f ? 20.0f * std::log10(v) : -120.0f; };
        g.setColour(juce::Colour(0xffdce6ef));
        g.setFont(12.0f);
        g.drawText("RMS " + juce::String(db(rv), 1) + " dB", 54, y + 39, 150, 18, juce::Justification::centredLeft);
        g.drawText("PEAK " + juce::String(db(pk), 1) + " dB", 230, y + 39, 160, 18, juce::Justification::centredRight);
    }
    g.setColour(juce::Colour(0xff9fb3c8));
    g.setFont(14.0f);
    g.drawText("STEREO CORRELATION", 22, 246, 190, 20, juce::Justification::centredLeft);
    const float corr = processor.getCorrelation();
    g.setColour(juce::Colour(0xff263746));
    g.fillRoundedRectangle(220.0f, 248.0f, 174.0f, 12.0f, 5.0f);
    g.setColour(juce::Colour(0xff31d0aa));
    g.fillRoundedRectangle(220.0f + 87.0f * (1.0f + corr), 248.0f, 87.0f * (1.0f - corr), 12.0f, 5.0f);
    g.setColour(juce::Colour(0xffdce6ef));
    g.drawText(juce::String(corr, 2), 395, 242, 40, 22, juce::Justification::centredRight);
}

void MultimeterAudioProcessorEditor::resized() {}
