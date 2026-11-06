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
        const float normalized = juce::jlimit(0.0f, 1.0f, (top - db) / juce::jmax(0.001f, top - bottom));
        return y + normalized * height;
    }

    void drawLevelBar(juce::Graphics& g, juce::Rectangle<float> bounds,
                      float rms, float peak, float peakHold)
    {
        constexpr float dbBottom = -60.0f;
        constexpr float dbTop = 6.0f;
        const auto db = [](float v)
        {
            return v > 1.0e-7f ? 20.0f * std::log10(v) : -80.0f;
        };

        const auto meter = bounds.reduced(7.0f, 1.0f);
        g.setColour(juce::Colour(0xff263b4a));
        g.fillRoundedRectangle(meter, 2.5f);

        // Stable 600 ms RMS average is the blue body.
        const float rmsY = mapDbToY(db(rms), dbTop, dbBottom, meter.getY(), meter.getHeight());
        const float peakY = mapDbToY(db(peak), dbTop, dbBottom, meter.getY(), meter.getHeight());
        g.setColour(cyanColour);
        g.fillRoundedRectangle(meter.withTop(rmsY), 2.0f);

        // Peak is a distinct yellow 5 px marker, so its level remains readable
        // even when the crest factor is small or a clipper reduces transients.
        g.setColour(yellowColour);
        g.fillRect(meter.getX(), peakY - 2.5f, meter.getWidth(), 5.0f);

        // Bright horizontal marker indicates the separately-held maximum.
        const float holdY = mapDbToY(db(peakHold), dbTop, dbBottom, meter.getY(), meter.getHeight());
        if (db(peakHold) > dbBottom)
        {
            g.setColour(brightText.withAlpha(0.95f));
            g.fillRect(meter.getX(), holdY - 1.0f, meter.getWidth(), 1.5f);
        }
    }
    void styleLabel(juce::Label& label)
    {
        label.setColour(juce::Label::textColourId, textColour);
        label.setFont(juce::Font(12.0f));
        label.setJustificationType(juce::Justification::centred);
    }
}

MultimeterAudioProcessorEditor::MultimeterAudioProcessorEditor(MultimeterAudioProcessor& p)
    : AudioProcessorEditor(&p), processor(p)
{
    setSize(1280, 820);
    setResizable(true, true);
    setResizeLimits(1000, 700, 1900, 1200);

    for (auto* component : std::array<juce::Component*, 19> {
        &analyzerButton, &goniometerButton, &detectionBox, &modeBox, &bandsBox,
        &topBox, &rangeBox, &weightingBox, &holdTimeBox, &returnRateBox,
        &levelModeBox, &holdButton, &resetButton, &limiterButton, &ceilingSlider,
        &releaseSlider, &spectrumSmoothSlider, &correlationSmoothSlider, nullptr
    })
        if (component != nullptr) addAndMakeVisible(component);

    auto addLabel = [this](juce::Label& label, const juce::String& text)
    {
        label.setText(text, juce::dontSendNotification);
        styleLabel(label);
        addAndMakeVisible(label);
    };

    addLabel(detectionLabel, "Detection");
    addLabel(modeLabel, "Mode");
    addLabel(bandsLabel, "Analyzer Bands");
    addLabel(weightingLabel, "Analyzer Weighting");
    addLabel(holdLabel, "Peak Hold");
    addLabel(returnRateLabel, "Return Rate");
    addLabel(levelLabel, "Level Meter");
    addLabel(limiterLabel, "Output Peak Limiter");
    addLabel(ceilingLabel, "Out Ceiling");
    addLabel(releaseLabel, "Release");
    addLabel(spectrumSmoothLabel, "Spectrum Smooth");
    addLabel(correlationSmoothLabel, "Correlation Average");

    configureChoice(detectionBox, "detect", { "Left", "Right", "LRmax", "Mono" }, "Detection source");
    configureChoice(modeBox, "mode", { "RMS Slow", "RMS Fast", "Peak" }, "Analyzer response");
    configureChoice(bandsBox, "bands", { "31", "63" }, "Analyzer resolution");
    configureChoice(topBox, "top", { "-15 dB", "-10 dB", "-5 dB", "0 dB", "+5 dB", "+10 dB", "+15 dB", "+20 dB" }, "Spectrum top");
    configureChoice(rangeBox, "range", { "20 dB", "30 dB", "40 dB", "50 dB", "60 dB", "70 dB", "80 dB" }, "Spectrum range");
    configureChoice(weightingBox, "weighting", { "Flat", "A-weight", "C-weight" }, "Frequency weighting");
    configureChoice(holdTimeBox, "holdtime", { "2 s", "4 s", "6 s", "Infinite" }, "Peak hold duration");
    configureChoice(returnRateBox, "returnrate", { "4 dB/s", "6.3 dB/s", "8.6 dB/s", "11.8 dB/s", "20 dB/s", "30 dB/s", "50 dB/s" }, "Meter fall rate");
    configureChoice(levelModeBox, "levelmode", { "Peak + RMS" }, "Peak and RMS are shown together");

    analyzerButton.setClickingTogglesState(true);
    goniometerButton.setClickingTogglesState(true);
    analyzerButton.setColour(juce::TextButton::buttonColourId, panelColour);
    goniometerButton.setColour(juce::TextButton::buttonColourId, panelColour);
    analyzerButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff1c4050));
    goniometerButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff1c4050));
    analyzerButton.setColour(juce::TextButton::textColourOffId, textColour);
    goniometerButton.setColour(juce::TextButton::textColourOffId, textColour);
    analyzerButton.setColour(juce::TextButton::textColourOnId, greenColour);
    goniometerButton.setColour(juce::TextButton::textColourOnId, greenColour);
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
    holdButton.setColour(juce::TextButton::buttonColourId, panelColour);
    holdButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff1c4050));
    holdButton.onClick = [this] { setBoolean("hold", holdButton.getToggleState()); };
    resetButton.setColour(juce::TextButton::buttonColourId, panelColour);
    resetButton.onClick = [this] { processor.resetHold(); };

    limiterButton.setClickingTogglesState(true);
    limiterButton.setColour(juce::TextButton::buttonColourId, panelColour);
    limiterButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff5a3927));
    limiterButton.setColour(juce::TextButton::textColourOnId, yellowColour);
    limiterAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processor.parameters, "limiter", limiterButton);

    configureSlider(ceilingSlider, " dB", "Ceiling in dBFS");
    configureSlider(releaseSlider, " ms", "Limiter release time");
    configureSlider(spectrumSmoothSlider, " s", "Spectrum display smoothing");
    configureSlider(correlationSmoothSlider, " s", "Correlation averaging time");
    ceilingAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.parameters, "ceiling", ceilingSlider);
    releaseAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.parameters, "release", releaseSlider);
    spectrumSmoothAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.parameters, "spectrumSmooth", spectrumSmoothSlider);
    correlationSmoothAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.parameters, "correlationSmooth", correlationSmoothSlider);

    startTimerHz(30);
}

void MultimeterAudioProcessorEditor::configureChoice(juce::ComboBox& box,
    const juce::String& parameterID, const juce::StringArray& choices, const juce::String& tooltip)
{
    box.clear();
    for (int i = 0; i < choices.size(); ++i) box.addItem(choices[i], i + 1);
    box.setSelectedId((int) processor.getParameter(parameterID) + 1, juce::dontSendNotification);
    box.setTooltip(tooltip);
    box.setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff263b4b));
    box.setColour(juce::ComboBox::textColourId, brightText);
    box.setColour(juce::ComboBox::outlineColourId, juce::Colour(0xff344b5b));
    box.setColour(juce::ComboBox::arrowColourId, greenColour);
    box.onChange = [this, &box, parameterID]
    {
        if (box.getSelectedId() > 0) setChoice(parameterID, box.getSelectedId() - 1);
    };
}

void MultimeterAudioProcessorEditor::configureSlider(juce::Slider& slider,
    const juce::String& suffix, const juce::String& tooltip)
{
    slider.setSliderStyle(juce::Slider::LinearHorizontal);
    slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 64, 20);
    slider.setTextValueSuffix(suffix);
    slider.setTooltip(tooltip);
    slider.setColour(juce::Slider::trackColourId, cyanColour);
    slider.setColour(juce::Slider::thumbColourId, greenColour);
    slider.setColour(juce::Slider::textBoxTextColourId, brightText);
    slider.setColour(juce::Slider::textBoxBackgroundColourId, panelColour);
    slider.setColour(juce::Slider::textBoxOutlineColourId, gridColour);
}

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
    const int width = getWidth();
    const int height = getHeight();
    const int bottomY = height - 215;
    const int leftX = 28, leftW = 405;
    const int middleX = 458, middleW = 370;
    const int rightX = 852, rightW = width - rightX - 28;

    analyzerButton.setBounds(20, 18, (width - 40) / 2, 44);
    goniometerButton.setBounds(20 + (width - 40) / 2, 18, (width - 40) / 2, 44);

    topBox.setBounds(84, 82, 112, 28);
    rangeBox.setBounds(270, 82, 112, 28);

    detectionLabel.setBounds(leftX, bottomY + 8, leftW, 18);
    detectionBox.setBounds(leftX, bottomY + 28, leftW, 28);
    modeLabel.setBounds(leftX, bottomY + 63, 190, 18);
    bandsLabel.setBounds(leftX + 210, bottomY + 63, 195, 18);
    modeBox.setBounds(leftX, bottomY + 83, 190, 28);
    bandsBox.setBounds(leftX + 210, bottomY + 83, 195, 28);
    weightingLabel.setBounds(leftX, bottomY + 118, leftW, 18);
    weightingBox.setBounds(leftX, bottomY + 138, 195, 28);

    holdLabel.setBounds(middleX, bottomY + 8, middleW, 18);
    holdButton.setBounds(middleX, bottomY + 28, 94, 28);
    holdTimeBox.setBounds(middleX + 104, bottomY + 28, 120, 28);
    resetButton.setBounds(middleX + 234, bottomY + 28, 104, 28);
    returnRateLabel.setBounds(middleX, bottomY + 63, 170, 18);
    returnRateBox.setBounds(middleX, bottomY + 83, 170, 28);
    levelLabel.setBounds(middleX + 184, bottomY + 63, 154, 18);
    levelModeBox.setBounds(middleX + 184, bottomY + 83, 154, 28);
    spectrumSmoothLabel.setBounds(middleX, bottomY + 118, middleW, 18);
    spectrumSmoothSlider.setBounds(middleX, bottomY + 138, middleW, 28);

    limiterLabel.setBounds(rightX, bottomY + 5, rightW, 18);
    limiterButton.setBounds(rightX, bottomY + 26, 140, 28);
    ceilingLabel.setBounds(rightX, bottomY + 57, rightW, 16);
    ceilingSlider.setBounds(rightX, bottomY + 74, rightW, 25);
    releaseLabel.setBounds(rightX, bottomY + 105, rightW, 16);
    releaseSlider.setBounds(rightX, bottomY + 122, rightW, 25);
    correlationSmoothLabel.setBounds(rightX, bottomY + 153, rightW, 16);
    correlationSmoothSlider.setBounds(rightX, bottomY + 169, rightW, 25);
}

void MultimeterAudioProcessorEditor::paint(juce::Graphics& g)
{
    const int width = getWidth();
    const int height = getHeight();
    const int bottomY = height - 215;
    g.fillAll(backgroundColour);
    g.setColour(panelColour);
    g.fillRect(0, bottomY, width, height - bottomY);
    g.setColour(gridColour);
    g.drawLine(0.0f, (float) bottomY, (float) width, (float) bottomY, 1.0f);
    g.drawLine((float) width - 322.0f, 122.0f, (float) width - 322.0f, (float) bottomY - 18.0f, 1.0f);

    g.setColour(textColour);
    g.setFont(12.0f);
    g.drawText("Top:", 36, 85, 42, 20, juce::Justification::centredLeft);
    g.drawText("Range:", 212, 85, 55, 20, juce::Justification::centredLeft);

    juce::Rectangle<float> plot(48.0f, 132.0f, (float) width - 425.0f, (float) bottomY - 155.0f);
    if (processor.getParameter("view") < 0.5f) drawAnalyzer(g, plot);
    else drawGoniometer(g, plot);

    juce::Rectangle<float> meters((float) width - 310.0f, 130.0f, 278.0f, (float) bottomY - 145.0f);
    drawRightMeters(g, meters);
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

    const int n = processor.getParameter("bands") < 0.5f ? 31 : 63;
    const double firstFrequency = n == 31 ? 15.625 * std::pow(2.0, 2.0 / 6.0) : 15.625;
    const double lastFrequency = n == 31
        ? 15.625 * std::pow(2.0, 62.0 / 6.0)
        : 15.625 * std::pow(2.0, 62.0 / 6.0);
    const double ratio = lastFrequency / firstFrequency;
    const float slot = bounds.getWidth() / (float) n;
    for (int i = 0; i < n; ++i)
    {
        const int index = i;
        const float db = processor.getSpectrumDb(index);
        const float holdDb = processor.getSpectrumHoldDb(index);
        const float y = mapDbToY(db, topDb, bottomDb, bounds.getY(), bounds.getHeight());
        const float barHeight = juce::jmax(1.0f, bounds.getBottom() - y);
        const float barWidth = juce::jmax(1.0f, slot - 1.3f);
        const float x = bounds.getX() + (float) i * slot + 0.5f;
        g.setColour(cyanColour);
        g.fillRect(x, y, barWidth, barHeight);
        if (processor.getParameter("hold") > 0.5f)
        {
            const float hy = mapDbToY(holdDb, topDb, bottomDb, bounds.getY(), bounds.getHeight());
            g.setColour(yellowColour.withAlpha(0.85f));
            g.fillRect(x, hy, barWidth, 1.5f);
        }
    }

    const double frequencies[] = { 20.0, 31.5, 63.0, 125.0, 250.0, 500.0, 1000.0,
                                   2000.0, 4000.0, 8000.0, 16000.0, 20000.0 };
    const juce::String labels[] = { "20", "31", "63", "125", "250", "500", "1k",
                                    "2k", "4k", "8k", "16k", "20k" };
    g.setColour(textColour);
    g.setFont(10.0f);
    for (int i = 0; i < 12; ++i)
    {
        const float x = bounds.getX()
            + (float) (std::log(frequencies[i] / 20.0) / std::log(20000.0 / 20.0)) * bounds.getWidth();
        g.drawLine(x, bounds.getBottom(), x, bounds.getBottom() + 4.0f, 1.0f);
        const int labelWidth = 32;
        const int labelX = i == 0 ? (int) x : (i == 11 ? (int) x - labelWidth : (int) x - labelWidth / 2);
        const auto alignment = i == 0 ? juce::Justification::centredLeft
            : (i == 11 ? juce::Justification::centredRight : juce::Justification::centred);
        g.drawText(labels[i], labelX, (int) bounds.getBottom() + 6, labelWidth, 16, alignment);
    }
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
    g.drawText("M", (int) square.getCentreX() - 10, (int) square.getY() + 4, 20, 18, juce::Justification::centred);
    g.drawText("L", (int) square.getX() + 8, (int) square.getCentreY() - 22, 20, 18, juce::Justification::centred);
    g.drawText("R", (int) square.getRight() - 28, (int) square.getCentreY() - 22, 20, 18, juce::Justification::centred);

    juce::Path trace;
    bool started = false;
    for (int i = 0; i < MultimeterAudioProcessor::gonioPointCount; ++i)
    {
        float x = 0.0f, y = 0.0f;
        processor.getGonioPoint(i, x, y);
        const float sx = square.getCentreX() + juce::jlimit(-1.0f, 1.0f, x) * square.getWidth() * 0.48f;
        const float sy = square.getCentreY() - juce::jlimit(-1.0f, 1.0f, y) * square.getHeight() * 0.48f;
        if (!started) { trace.startNewSubPath(sx, sy); started = true; }
        else trace.lineTo(sx, sy);
    }
    g.setColour(juce::Colour(0xff69c8ec).withAlpha(0.86f));
    g.strokePath(trace, juce::PathStrokeType(1.25f));
}

void MultimeterAudioProcessorEditor::drawRightMeters(juce::Graphics& g, juce::Rectangle<float> bounds)
{
    const auto db = [](float value)
    {
        return value > 1.0e-7f ? 20.0f * std::log10(value) : -80.0f;
    };

    const float leftPeak = db(processor.getPeak(0));
    const float rightPeak = db(processor.getPeak(1));
    const float leftRms = db(processor.getRms(0));
    const float rightRms = db(processor.getRms(1));

    // LUFS is a separate loudness readout, not a substitute for RMS.
    g.setColour(textColour);
    g.setFont(12.0f);
    g.drawText("LUFS", bounds.getX(), bounds.getY(), 100.0f, 18.0f, juce::Justification::centredLeft);
    g.setFont(10.5f);
    g.drawText("LU-I " + juce::String(processor.getLufsIntegrated(), 1),
               bounds.getX(), bounds.getY() + 20.0f, 100.0f, 16.0f, juce::Justification::centredLeft);
    g.drawText("LU-S " + juce::String(processor.getLufsShortTerm(), 1),
               bounds.getX(), bounds.getY() + 38.0f, 100.0f, 16.0f, juce::Justification::centredLeft);

    const float firstColumnX = bounds.getX() + 112.0f;
    const float columnWidth = 70.0f;
    const float secondColumnX = firstColumnX + columnWidth;
    g.setFont(11.0f);
    g.setColour(brightText);
    g.drawText("L", (int) firstColumnX, (int) bounds.getY(), (int) columnWidth, 16, juce::Justification::centred);
    g.drawText("R", (int) secondColumnX, (int) bounds.getY(), (int) columnWidth, 16, juce::Justification::centred);
    g.setFont(9.5f);
    g.setColour(yellowColour);
    g.drawText("PK " + juce::String(leftPeak, 1), (int) firstColumnX, (int) bounds.getY() + 17,
               (int) columnWidth, 14, juce::Justification::centred);
    g.drawText("PK " + juce::String(rightPeak, 1), (int) secondColumnX, (int) bounds.getY() + 17,
               (int) columnWidth, 14, juce::Justification::centred);
    g.setColour(cyanColour);
    g.drawText("RMS " + juce::String(leftRms, 1), (int) firstColumnX, (int) bounds.getY() + 33,
               (int) columnWidth, 14, juce::Justification::centred);
    g.drawText("RMS " + juce::String(rightRms, 1), (int) secondColumnX, (int) bounds.getY() + 33,
               (int) columnWidth, 14, juce::Justification::centred);

    const float meterY = bounds.getY() + 56.0f;
    const float correlationY = bounds.getBottom() - 48.0f;
    const float meterHeight = juce::jmax(100.0f, correlationY - meterY - 30.0f);
    const float meterWidth = 48.0f;
    drawLevelBar(g, { firstColumnX + 11.0f, meterY, meterWidth, meterHeight },
                 processor.getRms(0), processor.getPeak(0), processor.getPeakHold(0));
    drawLevelBar(g, { secondColumnX + 11.0f, meterY, meterWidth, meterHeight },
                 processor.getRms(1), processor.getPeak(1), processor.getPeakHold(1));

    g.setColour(textColour);
    g.setFont(11.0f);
    g.drawText("CORRELATION", bounds.getX(), correlationY - 23.0f,
               bounds.getWidth(), 16.0f, juce::Justification::centred);
    const float lineX = bounds.getX() + 8.0f;
    const float lineW = bounds.getWidth() - 16.0f;
    g.setColour(juce::Colour(0xffbd4d4d));
    g.drawLine(lineX, correlationY, lineX + lineW * 0.5f, correlationY, 2.5f);
    g.setColour(juce::Colour(0xffd1a64b));
    g.drawLine(lineX + lineW * 0.5f, correlationY, lineX + lineW * 0.75f, correlationY, 2.5f);
    g.setColour(juce::Colour(0xff6ba66e));
    g.drawLine(lineX + lineW * 0.75f, correlationY, lineX + lineW, correlationY, 2.5f);
    const float markerX = lineX + (processor.getCorrelation() + 1.0f) * 0.5f * lineW;
    g.setColour(brightText);
    g.drawLine(markerX, correlationY - 10.0f, markerX, correlationY + 3.0f, 2.0f);
    g.drawText("-1", lineX, correlationY + 5.0f, 24.0f, 14.0f, juce::Justification::centredLeft);
    g.drawText("0", lineX + lineW * 0.5f - 8.0f, correlationY + 5.0f, 16.0f, 14.0f, juce::Justification::centred);
    g.drawText("+1", lineX + lineW - 24.0f, correlationY + 5.0f, 24.0f, 14.0f, juce::Justification::centredRight);
    g.setFont(12.0f);
    g.drawText(juce::String(processor.getCorrelation(), 2), bounds.getX(), correlationY + 19.0f,
               bounds.getWidth(), 16.0f, juce::Justification::centred);

    const float gr = processor.getLimiterGainReductionDb();
    g.setColour(gr > 0.05f ? yellowColour : textColour);
    g.setFont(11.0f);
    g.drawText("LIMITER GR  -" + juce::String(gr, 1) + " dB",
               bounds.getX(), bounds.getBottom() - 18.0f,
               bounds.getWidth(), 15.0f, juce::Justification::centred);
}
