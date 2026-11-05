#include "PluginEditor.h"
#include <cmath>

namespace
{
    const juce::Colour backgroundColour { 0xff111e28 };
    const juce::Colour panelColour { 0xff1b2d3d };
    const juce::Colour gridColour { 0xff293b49 };
    const juce::Colour textColour { 0xffa9bdca };
    const juce::Colour brightText { 0xffdce8ef };
    const juce::Colour cyanColour { 0xff4d9fbd };
    const juce::Colour greenColour { 0xff75e49b };
    const juce::Colour yellowColour { 0xffffd323 };

    float mapDbToY(float db, float top, float bottom, float y, float height)
    {
        const float normalized = juce::jlimit(0.0f, 1.0f, (top - db) / (top - bottom));
        return y + normalized * height;
    }

    void drawLevelBar(juce::Graphics& g, juce::Rectangle<float> bounds,
                      float rms, float peak, const juce::String& label)
    {
        const float dbBottom = -60.0f;
        const float dbTop = 6.0f;
        const auto db = [](float v) { return v > 1.0e-7f ? 20.0f * std::log10(v) : -80.0f; };
        g.setColour(textColour);
        g.setFont(12.0f);
        g.drawText(label, bounds.removeFromTop(18.0f).toNearestInt(), juce::Justification::centred);
        auto meter = bounds.reduced(8.0f, 2.0f);
        g.setColour(juce::Colour(0xff263b4a));
        g.fillRoundedRectangle(meter, 3.0f);

        const float rmsDb = juce::jlimit(dbBottom, dbTop, db(rms));
        const float peakDb = juce::jlimit(dbBottom, dbTop, db(peak));
        const float rmsY = mapDbToY(rmsDb, dbTop, dbBottom, meter.getY(), meter.getHeight());
        auto fill = meter.withTop(rmsY);
        g.setColour(cyanColour);
        g.fillRoundedRectangle(fill, 2.0f);
        const float peakY = mapDbToY(peakDb, dbTop, dbBottom, meter.getY(), meter.getHeight());
        g.setColour(yellowColour);
        g.fillRect(meter.getX(), peakY - 1.5f, meter.getWidth(), 3.0f);

        g.setColour(brightText);
        g.setFont(11.0f);
        g.drawText(juce::String(db(peak), 1), bounds.getX(), bounds.getBottom() + 2.0f,
                   bounds.getWidth(), 16.0f, juce::Justification::centred);
    }
}

MultimeterAudioProcessorEditor::MultimeterAudioProcessorEditor(MultimeterAudioProcessor& p)
    : AudioProcessorEditor(&p), processor(p)
{
    setSize(1120, 720);
    setResizable(true, true);
    setResizeLimits(900, 590, 1800, 1100);

    addAndMakeVisible(analyzerButton);
    addAndMakeVisible(goniometerButton);
    addAndMakeVisible(detectionBox);
    addAndMakeVisible(modeBox);
    addAndMakeVisible(bandsBox);
    addAndMakeVisible(topBox);
    addAndMakeVisible(rangeBox);
    addAndMakeVisible(holdTimeBox);
    addAndMakeVisible(returnRateBox);
    addAndMakeVisible(levelModeBox);
    addAndMakeVisible(holdButton);
    addAndMakeVisible(resetButton);

    configureChoice(detectionBox, "detect", { "Left", "Right", "LRmax", "Mono" }, "Detection");
    configureChoice(modeBox, "mode", { "RMS Slow", "RMS Fast", "Peak" }, "Mode");
    configureChoice(bandsBox, "bands", { "31", "63" }, "Analyzer Bands");
    configureChoice(topBox, "top", { "-15 dB", "-10 dB", "-5 dB", "0 dB", "+5 dB", "+10 dB", "+15 dB", "+20 dB" }, "Top");
    configureChoice(rangeBox, "range", { "20 dB", "30 dB", "40 dB", "50 dB", "60 dB", "70 dB", "80 dB" }, "Range");
    configureChoice(holdTimeBox, "holdtime", { "2 s", "4 s", "6 s", "Infinite" }, "Peak hold");
    configureChoice(returnRateBox, "returnrate", { "4 dB/s", "6.3 dB/s", "8.6 dB/s", "11.8 dB/s", "20 dB/s", "30 dB/s", "50 dB/s" }, "Return rate");
    configureChoice(levelModeBox, "levelmode", { "Peak", "Peak & RMS" }, "Level");

    analyzerButton.setClickingTogglesState(true);
    goniometerButton.setClickingTogglesState(true);
    analyzerButton.onClick = [this]
    {
        setChoice("view", 0);
        analyzerButton.setToggleState(true, juce::dontSendNotification);
        goniometerButton.setToggleState(false, juce::dontSendNotification);
        repaint();
    };
    goniometerButton.onClick = [this]
    {
        setChoice("view", 1);
        analyzerButton.setToggleState(false, juce::dontSendNotification);
        goniometerButton.setToggleState(true, juce::dontSendNotification);
        repaint();
    };
    analyzerButton.setToggleState(processor.getParameter("view") < 0.5f, juce::dontSendNotification);
    goniometerButton.setToggleState(processor.getParameter("view") >= 0.5f, juce::dontSendNotification);

    holdButton.setClickingTogglesState(true);
    holdButton.setToggleState(processor.getParameter("hold") > 0.5f, juce::dontSendNotification);
    holdButton.onClick = [this] { setBoolean("hold", holdButton.getToggleState()); };

    resetButton.onClick = [this] { processor.resetHold(); };
    startTimerHz(30);
}

void MultimeterAudioProcessorEditor::configureChoice(juce::ComboBox& box,
    const juce::String& parameterID, const juce::StringArray& choices, const juce::String& tooltip)
{
    box.clear();
    for (int i = 0; i < choices.size(); ++i)
        box.addItem(choices[i], i + 1);
    box.setSelectedId((int) processor.getParameter(parameterID) + 1, juce::dontSendNotification);
    box.setTooltip(tooltip);
    box.setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff263b4b));
    box.setColour(juce::ComboBox::textColourId, brightText);
    box.setColour(juce::ComboBox::outlineColourId, juce::Colour(0xff344b5b));
    box.setColour(juce::ComboBox::arrowColourId, greenColour);
    box.onChange = [this, &box, parameterID]
    {
        if (box.getSelectedId() > 0)
            setChoice(parameterID, box.getSelectedId() - 1);
    };
}

void MultimeterAudioProcessorEditor::attachChoice(juce::ComboBox&, const juce::String&) {}

void MultimeterAudioProcessorEditor::setChoice(const juce::String& id, int index)
{
    if (auto* parameter = dynamic_cast<juce::RangedAudioParameter*>(processor.parameters.getParameter(id)))
        parameter->setValueNotifyingHost(parameter->convertTo0to1((float) index));
}

void MultimeterAudioProcessorEditor::setBoolean(const juce::String& id, bool state)
{
    if (auto* parameter = processor.parameters.getParameter(id))
        parameter->setValueNotifyingHost(state ? 1.0f : 0.0f);
}

void MultimeterAudioProcessorEditor::timerCallback()
{
    const bool analyzerSelected = processor.getParameter("view") < 0.5f;
    if (analyzerButton.getToggleState() != analyzerSelected)
        analyzerButton.setToggleState(analyzerSelected, juce::dontSendNotification);
    if (goniometerButton.getToggleState() == analyzerSelected)
        goniometerButton.setToggleState(!analyzerSelected, juce::dontSendNotification);
    if (holdButton.getToggleState() != (processor.getParameter("hold") > 0.5f))
        holdButton.setToggleState(processor.getParameter("hold") > 0.5f, juce::dontSendNotification);
    repaint();
}

void MultimeterAudioProcessorEditor::resized()
{
    auto area = getLocalBounds();
    const int width = area.getWidth();
    const int height = area.getHeight();
    const int margin = 20;
    const int half = (width - margin * 2) / 2;

    analyzerButton.setBounds(margin, 18, half, 42);
    goniometerButton.setBounds(margin + half, 18, half, 42);

    topBox.setBounds(90, 83, 112, 28);
    rangeBox.setBounds(275, 83, 112, 28);
    levelModeBox.setBounds(width - 195, 83, 165, 28);

    const int bottomTop = height - 150;
    detectionBox.setBounds(34, bottomTop + 30, 315, 30);
    modeBox.setBounds(34, bottomTop + 88, 230, 30);
    bandsBox.setBounds(282, bottomTop + 88, 170, 30);

    holdButton.setBounds(width - 350, bottomTop + 30, 92, 30);
    holdTimeBox.setBounds(width - 250, bottomTop + 30, 100, 30);
    resetButton.setBounds(width - 142, bottomTop + 30, 112, 30);
    returnRateBox.setBounds(width - 250, bottomTop + 88, 220, 30);
}

void MultimeterAudioProcessorEditor::paint(juce::Graphics& g)
{
    const int width = getWidth();
    const int height = getHeight();
    g.fillAll(backgroundColour);

    // Main display frame and separator.
    g.setColour(panelColour);
    g.fillRect(0, height - 150, width, 150);
    g.setColour(gridColour);
    g.drawLine(0.0f, (float) height - 150.0f, (float) width, (float) height - 150.0f, 1.0f);
    g.drawLine((float) width - 310.0f, 120.0f, (float) width - 310.0f, (float) height - 170.0f, 1.0f);

    g.setColour(textColour);
    g.setFont(13.0f);
    g.drawText("Top:", 42, 87, 42, 20, juce::Justification::centredLeft);
    g.drawText("Range:", 220, 87, 55, 20, juce::Justification::centredLeft);
    g.drawText("Level:", width - 250, 87, 58, 20, juce::Justification::centredLeft);

    juce::Rectangle<float> plot(48.0f, 132.0f,
        (float) width - 395.0f, (float) height - 300.0f);

    if (processor.getParameter("view") < 0.5f)
        drawAnalyzer(g, plot);
    else
        drawGoniometer(g, plot);

    juce::Rectangle<float> meters((float) width - 305.0f, 130.0f, 275.0f, (float) height - 310.0f);
    drawRightMeters(g, meters);

    const float bottomY = (float) height - 140.0f;
    g.setColour(textColour);
    g.setFont(14.0f);
    g.drawText("Detection", 34, (int) bottomY - 2, 315, 20, juce::Justification::centred);
    g.drawText("Mode", 34, (int) bottomY + 55, 230, 20, juce::Justification::centred);
    g.drawText("Analyzer Bands", 282, (int) bottomY + 55, 170, 20, juce::Justification::centred);
    g.drawText("Peak", width - 350, (int) bottomY - 2, 220, 20, juce::Justification::centred);
    g.drawText("Return Rate", width - 250, (int) bottomY + 55, 220, 20, juce::Justification::centred);
}

void MultimeterAudioProcessorEditor::drawAnalyzer(juce::Graphics& g, juce::Rectangle<float> bounds)
{
    const int topChoices[] = { -15, -10, -5, 0, 5, 10, 15, 20 };
    const int rangeChoices[] = { 20, 30, 40, 50, 60, 70, 80 };
    const float topDb = (float) topChoices[juce::jlimit(0, 7, (int) processor.getParameter("top"))];
    const float rangeDb = (float) rangeChoices[juce::jlimit(0, 6, (int) processor.getParameter("range"))];
    const float bottomDb = topDb - rangeDb;

    g.setFont(11.0f);
    for (float db = topDb; db >= bottomDb - 0.01f; db -= 10.0f)
    {
        const float y = mapDbToY(db, topDb, bottomDb, bounds.getY(), bounds.getHeight());
        g.setColour(gridColour);
        g.drawLine(bounds.getX(), y, bounds.getRight(), y, 1.0f);
        g.setColour(textColour);
        g.drawText(juce::String((int) db), 0, (int) y - 8, 42, 16, juce::Justification::centredRight);
    }

    const int numberOfBands = processor.getParameter("bands") < 0.5f ? 31 : 63;
    const float slot = bounds.getWidth() / (float) numberOfBands;
    for (int i = 0; i < numberOfBands; ++i)
    {
        const int spectrumIndex = numberOfBands == 31 ? i * 2 : i;
        const float db = processor.getSpectrumDb(spectrumIndex);
        const float y = mapDbToY(db, topDb, bottomDb, bounds.getY(), bounds.getHeight());
        const float barHeight = juce::jmax(1.0f, bounds.getBottom() - y);
        const float barWidth = juce::jmax(1.0f, slot - 1.4f);
        g.setColour(cyanColour);
        g.fillRect(bounds.getX() + (float) i * slot + 0.5f, y, barWidth, barHeight);
    }

    const double frequencies[] = { 31.5, 63.0, 125.0, 250.0, 500.0, 1000.0,
                                   2000.0, 4000.0, 8000.0, 16000.0 };
    const juce::String labels[] = { "31", "62", "125", "250", "500", "1 k", "2 k", "4 k", "8 k", "16 k" };
    g.setColour(textColour);
    g.setFont(11.0f);
    for (int i = 0; i < 10; ++i)
    {
        const float x = bounds.getX() + (float) (std::log(frequencies[i] / 20.0) / std::log(20000.0 / 20.0)) * bounds.getWidth();
        g.drawLine(x, bounds.getBottom(), x, bounds.getBottom() + 4.0f, 1.0f);
        g.drawText(labels[i], (int) x - 22, (int) bounds.getBottom() + 6, 44, 16, juce::Justification::centred);
    }
    g.drawText("Hz", (int) bounds.getRight() - 10, (int) bounds.getBottom() + 6, 28, 16, juce::Justification::centredLeft);
}

void MultimeterAudioProcessorEditor::drawGoniometer(juce::Graphics& g, juce::Rectangle<float> bounds)
{
    const float side = juce::jmin(bounds.getWidth(), bounds.getHeight());
    juce::Rectangle<float> square(bounds.getCentreX() - side * 0.5f,
                                  bounds.getCentreY() - side * 0.5f, side, side);
    g.setColour(gridColour);
    g.drawRect(square, 1.0f);
    g.drawLine(square.getCentreX(), square.getY(), square.getCentreX(), square.getBottom(), 1.0f);
    g.drawLine(square.getX(), square.getCentreY(), square.getRight(), square.getCentreY(), 1.0f);
    g.drawLine(square.getX(), square.getY(), square.getRight(), square.getBottom(), 1.0f);
    g.drawLine(square.getRight(), square.getY(), square.getX(), square.getBottom(), 1.0f);

    g.setColour(textColour);
    g.setFont(12.0f);
    g.drawText("L", (int) square.getX() + 8, (int) square.getCentreY() - 22, 20, 18, juce::Justification::centred);
    g.drawText("R", (int) square.getRight() - 28, (int) square.getCentreY() - 22, 20, 18, juce::Justification::centred);
    g.drawText("M", (int) square.getCentreX() - 10, (int) square.getY() + 4, 20, 18, juce::Justification::centred);

    const float peak = juce::jmax(processor.getPeak(0), processor.getPeak(1));
    const float autoScale = 0.80f / juce::jmax(0.08f, peak);
    juce::Path trace;
    bool started = false;
    for (int i = 0; i < MultimeterAudioProcessor::gonioPointCount; ++i)
    {
        float x = 0.0f, y = 0.0f;
        processor.getGonioPoint(i, x, y);
        const float sx = square.getCentreX() + juce::jlimit(-0.95f, 0.95f, x * autoScale) * square.getWidth() * 0.5f;
        const float sy = square.getCentreY() - juce::jlimit(-0.95f, 0.95f, y * autoScale) * square.getHeight() * 0.5f;
        if (!started) { trace.startNewSubPath(sx, sy); started = true; }
        else trace.lineTo(sx, sy);
    }
    g.setColour(juce::Colour(0xff69c8ec).withAlpha(0.82f));
    g.strokePath(trace, juce::PathStrokeType(1.2f));
}

void MultimeterAudioProcessorEditor::drawRightMeters(juce::Graphics& g, juce::Rectangle<float> bounds)
{
    g.setColour(textColour);
    g.setFont(12.0f);
    const float luIntegrated = processor.getLufsIntegrated();
    const float luShort = processor.getLufsShortTerm();
    g.drawText("LUFS", bounds.getX(), bounds.getY(), 78.0f, 18.0f, juce::Justification::centred);
    g.drawText("LU-I " + juce::String(luIntegrated, 1), bounds.getX(), bounds.getY() + 20.0f,
               82.0f, 17.0f, juce::Justification::centredLeft);
    g.drawText("LU-S " + juce::String(luShort, 1), bounds.getX(), bounds.getY() + 38.0f,
               82.0f, 17.0f, juce::Justification::centredLeft);

    const float meterY = bounds.getY() + 75.0f;
    const float meterHeight = juce::jmax(150.0f, bounds.getHeight() - 145.0f);
    const float meterWidth = 48.0f;
    const float meterGap = 12.0f;
    const float startX = bounds.getX() + 95.0f;
    drawLevelBar(g, { startX, meterY, meterWidth, meterHeight }, processor.getRms(0),
                 processor.getPeak(0), "L");
    drawLevelBar(g, { startX + meterWidth + meterGap, meterY, meterWidth, meterHeight },
                 processor.getRms(1), processor.getPeak(1), "R");

    const float correlationY = bounds.getBottom() - 35.0f;
    g.setColour(textColour);
    g.setFont(11.0f);
    g.drawText("CORRELATION", bounds.getX(), correlationY - 22.0f,
               bounds.getWidth(), 16.0f, juce::Justification::centred);
    const float lineX = bounds.getX() + 8.0f;
    const float lineW = bounds.getWidth() - 16.0f;
    g.setColour(juce::Colour(0xffbd4d4d));
    g.drawLine(lineX, correlationY, lineX + lineW * 0.5f, correlationY, 2.0f);
    g.setColour(juce::Colour(0xffd1a64b));
    g.drawLine(lineX + lineW * 0.5f, correlationY, lineX + lineW * 0.75f, correlationY, 2.0f);
    g.setColour(juce::Colour(0xff6ba66e));
    g.drawLine(lineX + lineW * 0.75f, correlationY, lineX + lineW, correlationY, 2.0f);
    const float markerX = lineX + (processor.getCorrelation() + 1.0f) * 0.5f * lineW;
    g.setColour(brightText);
    g.drawLine(markerX, correlationY - 10.0f, markerX, correlationY + 2.0f, 2.0f);
    g.drawText(juce::String(processor.getCorrelation(), 2), bounds.getX(), correlationY + 5.0f,
               bounds.getWidth(), 16.0f, juce::Justification::centred);
}
