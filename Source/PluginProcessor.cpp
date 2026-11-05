#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>

namespace
{
    constexpr double pi = 3.14159265358979323846;
    constexpr float dbFloor = -80.0f;

    float toDb(float value)
    {
        return value > 1.0e-7f ? 20.0f * std::log10(value) : dbFloor;
    }

    juce::StringArray returnRateChoices()
    {
        return { "4 dB/s", "6.3 dB/s", "8.6 dB/s", "11.8 dB/s", "20 dB/s", "30 dB/s", "50 dB/s" };
    }
}

MultimeterAudioProcessor::ParameterState::ParameterLayout MultimeterAudioProcessor::createParameterLayout()
{
    ParameterState::ParameterLayout layout;
    layout.add(std::make_unique<juce::AudioParameterChoice>("view", "View",
        juce::StringArray { "Analyzer", "Goniometer" }, 0));
    layout.add(std::make_unique<juce::AudioParameterChoice>("detect", "Detection",
        juce::StringArray { "Left", "Right", "LRmax", "Mono" }, 2));
    layout.add(std::make_unique<juce::AudioParameterChoice>("mode", "Mode",
        juce::StringArray { "RMS Slow", "RMS Fast", "Peak" }, 2));
    layout.add(std::make_unique<juce::AudioParameterChoice>("bands", "Analyzer Bands",
        juce::StringArray { "31", "63" }, 1));
    layout.add(std::make_unique<juce::AudioParameterChoice>("top", "Top",
        juce::StringArray { "-15 dB", "-10 dB", "-5 dB", "0 dB", "+5 dB", "+10 dB", "+15 dB", "+20 dB" }, 4));
    layout.add(std::make_unique<juce::AudioParameterChoice>("range", "Range",
        juce::StringArray { "20 dB", "30 dB", "40 dB", "50 dB", "60 dB", "70 dB", "80 dB" }, 4));
    layout.add(std::make_unique<juce::AudioParameterBool>("hold", "Hold", false));
    layout.add(std::make_unique<juce::AudioParameterChoice>("holdtime", "Peak Hold",
        juce::StringArray { "2 s", "4 s", "6 s", "Infinite" }, 0));
    layout.add(std::make_unique<juce::AudioParameterChoice>("returnrate", "Return Rate",
        returnRateChoices(), 3));
    layout.add(std::make_unique<juce::AudioParameterChoice>("levelmode", "Level",
        juce::StringArray { "Peak", "Peak & RMS" }, 1));
    return layout;
}

MultimeterAudioProcessor::MultimeterAudioProcessor()
    : AudioProcessor(BusesProperties()
        .withInput("Input", juce::AudioChannelSet::stereo(), true)
        .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      parameters(*this, nullptr, "PARAMETERS", createParameterLayout())
{
    for (auto& value : spectrumDb) value.store(dbFloor);
    for (auto& value : gonioX) value.store(0.0f);
    for (auto& value : gonioY) value.store(0.0f);
}

float MultimeterAudioProcessor::getParameter(const juce::String& id) const
{
    if (auto* value = parameters.getRawParameterValue(id))
        return value->load();
    return 0.0f;
}

void MultimeterAudioProcessor::getGonioPoint(int index, float& x, float& y) const
{
    const int write = gonioWriteIndex.load();
    const int safe = juce::jlimit(0, gonioPointCount - 1, index);
    const int slot = (write + safe) % gonioPointCount;
    x = gonioX[(size_t) slot].load();
    y = gonioY[(size_t) slot].load();
}

void MultimeterAudioProcessor::resetHold()
{
    for (auto& value : spectrumDb) value.store(dbFloor);
    for (int channel = 0; channel < 2; ++channel)
        peak[(size_t) channel].store(0.0f);
}

void MultimeterAudioProcessor::prepareToPlay(double sampleRate, int)
{
    currentSampleRate = juce::jmax(8000.0, sampleRate);
    fftFifo.fill(0.0f);
    fftData.fill(0.0f);
    fftFifoIndex = 0;
    gonioDecimator = 0;
    integratedEnergySum = 0.0;
    integratedSampleCount = 0;
    shortTermMeanEnergy = 1.0e-10;
    lufsIntegrated.store(-70.0f);
    lufsShortTerm.store(-70.0f);

    const auto shelf = juce::dsp::IIR::Coefficients<float>::makeHighShelf(
        currentSampleRate, 1681.974450955533, 0.7071752369554196,
        juce::Decibels::decibelsToGain(3.999843853973347));
    const auto highPass = juce::dsp::IIR::Coefficients<float>::makeHighPass(
        currentSampleRate, 38.13547087602444, 0.5003270373238773);

    for (int ch = 0; ch < 2; ++ch)
    {
        kShelf[(size_t) ch].reset();
        kHighPass[(size_t) ch].reset();
        *kShelf[(size_t) ch].coefficients = *shelf;
        *kHighPass[(size_t) ch].coefficients = *highPass;
    }
}

bool MultimeterAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto input = layouts.getMainInputChannelSet();
    const auto output = layouts.getMainOutputChannelSet();
    return (input == juce::AudioChannelSet::mono() || input == juce::AudioChannelSet::stereo())
        && input == output;
}

void MultimeterAudioProcessor::updateSpectrum(float returnRateDbPerSecond,
                                               bool holdEnabled,
                                               int detectionMode,
                                               int meteringMode)
{
    fftData.fill(0.0f);
    std::copy(fftFifo.begin(), fftFifo.end(), fftData.begin());
    window.multiplyWithWindowingTable(fftData.data(), (size_t) fftSize);
    fft.performFrequencyOnlyForwardTransform(fftData.data());

    const double nyquist = currentSampleRate * 0.5;
    const double lowFrequency = 20.0;
    const double highFrequency = juce::jmin(20000.0, nyquist);
    const double hopSeconds = (double) fftSize / currentSampleRate;
    const int binCount = fftSize / 2;

    for (int band = 0; band < spectrumBandCount; ++band)
    {
        const double edge0 = lowFrequency * std::pow(highFrequency / lowFrequency,
            (double) band / (double) spectrumBandCount);
        const double edge1 = lowFrequency * std::pow(highFrequency / lowFrequency,
            (double) (band + 1) / (double) spectrumBandCount);
        int firstBin = juce::jlimit(1, binCount - 1,
            (int) std::floor(edge0 * (double) fftSize / currentSampleRate));
        int lastBin = juce::jlimit(firstBin, binCount - 1,
            (int) std::ceil(edge1 * (double) fftSize / currentSampleRate));

        double sumSquares = 0.0;
        float maximum = 0.0f;
        int count = 0;
        for (int bin = firstBin; bin <= lastBin; ++bin)
        {
            const float magnitude = fftData[(size_t) bin] * (4.0f / (float) fftSize);
            sumSquares += (double) magnitude * (double) magnitude;
            maximum = juce::jmax(maximum, magnitude);
            ++count;
        }

        const float magnitude = (meteringMode == 2)
            ? maximum
            : (float) std::sqrt(sumSquares / (double) juce::jmax(1, count));
        const float targetDb = juce::jlimit(dbFloor, 12.0f, toDb(magnitude));
        const float previous = spectrumDb[(size_t) band].load();
        float next = targetDb;

        if (targetDb < previous)
        {
            if (holdEnabled)
                next = previous;
            else
                next = juce::jmax(targetDb,
                    previous - returnRateDbPerSecond * (float) hopSeconds);
        }
        spectrumDb[(size_t) band].store(next);
    }

    juce::ignoreUnused(detectionMode);
}

void MultimeterAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int channels = juce::jmin(2, getTotalNumInputChannels(), buffer.getNumChannels());
    const int samples = buffer.getNumSamples();
    if (channels <= 0 || samples <= 0) return;

    const auto* left = buffer.getReadPointer(0);
    const auto* right = channels > 1 ? buffer.getReadPointer(1) : left;

    const int detectionMode = (int) getParameter("detect");
    const int meteringMode = (int) getParameter("mode");
    const float returnRates[] = { 4.0f, 6.3f, 8.6f, 11.8f, 20.0f, 30.0f, 50.0f };
    const float returnRate = returnRates[juce::jlimit(0, 6, (int) getParameter("returnrate"))];
    const bool holdEnabled = getParameter("hold") > 0.5f;

    double sumSquares[2] = { 0.0, 0.0 };
    double cross = 0.0, leftEnergy = 0.0, rightEnergy = 0.0;
    float blockPeak[2] = { 0.0f, 0.0f };
    double loudnessEnergy = 0.0;

    for (int i = 0; i < samples; ++i)
    {
        const float l = left[i];
        const float r = right[i];
        blockPeak[0] = juce::jmax(blockPeak[0], std::abs(l));
        blockPeak[1] = juce::jmax(blockPeak[1], std::abs(r));
        sumSquares[0] += (double) l * l;
        sumSquares[1] += (double) r * r;
        cross += (double) l * r;
        leftEnergy += (double) l * l;
        rightEnergy += (double) r * r;

        float analyzerSample = 0.5f * (l + r);
        if (detectionMode == 0) analyzerSample = l;
        else if (detectionMode == 1) analyzerSample = r;
        else if (detectionMode == 2) analyzerSample = std::abs(l) >= std::abs(r) ? l : r;

        fftFifo[(size_t) fftFifoIndex++] = analyzerSample;
        if (fftFifoIndex >= fftSize)
        {
            fftFifoIndex = 0;
            updateSpectrum(returnRate, holdEnabled, detectionMode, meteringMode);
        }

        if (++gonioDecimator >= 8)
        {
            gonioDecimator = 0;
            const int slot = gonioWriteIndex.load();
            gonioX[(size_t) slot].store((l + r) * 0.70710678f);
            gonioY[(size_t) slot].store((l - r) * 0.70710678f);
            gonioWriteIndex.store((slot + 1) % gonioPointCount);
        }

        const float weightedL = kHighPass[0].processSample(kShelf[0].processSample(l));
        const float weightedR = kHighPass[1].processSample(kShelf[1].processSample(r));
        loudnessEnergy += 0.5 * ((double) weightedL * weightedL + (double) weightedR * weightedR);
    }

    const double blockSeconds = (double) samples / currentSampleRate;
    for (int ch = 0; ch < 2; ++ch)
    {
        const float blockRms = (float) std::sqrt(sumSquares[ch] / (double) samples);
        const float oldPeak = peak[(size_t) ch].load();
        const float decay = (float) std::pow(10.0, -(double) returnRate * blockSeconds / 20.0);
        peak[(size_t) ch].store(juce::jmax(blockPeak[ch], oldPeak * decay));

        const float oldRms = rms[(size_t) ch].load();
        const float rmsAlpha = (float) std::exp(-blockSeconds / (meteringMode == 0 ? 0.5 : 0.12));
        rms[(size_t) ch].store(rmsAlpha * oldRms + (1.0f - rmsAlpha) * blockRms);
    }

    const double denominator = std::sqrt(leftEnergy * rightEnergy);
    correlation.store(denominator > 1.0e-15
        ? (float) juce::jlimit(-1.0, 1.0, cross / denominator) : 0.0f);

    const double blockLoudnessMean = loudnessEnergy / (double) samples;
    integratedEnergySum += loudnessEnergy;
    integratedSampleCount += (uint64_t) samples;
    const double integratedMean = integratedSampleCount > 0
        ? integratedEnergySum / (double) integratedSampleCount : 1.0e-10;
    const double shortAlpha = std::exp(-blockSeconds / 3.0);
    shortTermMeanEnergy = shortAlpha * shortTermMeanEnergy
        + (1.0 - shortAlpha) * blockLoudnessMean;

    const float integratedLufs = (float) (-0.691 + 10.0 * std::log10(juce::jmax(1.0e-10, integratedMean)));
    const float shortLufs = (float) (-0.691 + 10.0 * std::log10(juce::jmax(1.0e-10, shortTermMeanEnergy)));
    lufsIntegrated.store(juce::jlimit(-70.0f, 12.0f, integratedLufs));
    lufsShortTerm.store(juce::jlimit(-70.0f, 12.0f, shortLufs));
}

void MultimeterAudioProcessor::getStateInformation(juce::MemoryBlock& destination)
{
    auto state = parameters.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destination);
}

void MultimeterAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml(getXmlFromBinary(data, sizeInBytes));
    if (xml != nullptr && xml->hasTagName(parameters.state.getType()))
        parameters.replaceState(juce::ValueTree::fromXml(*xml));
}

juce::AudioProcessorEditor* MultimeterAudioProcessor::createEditor()
{
    return new MultimeterAudioProcessorEditor(*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MultimeterAudioProcessor();
}
