#include "PluginProcessor.h"
#include "PluginEditor.h"

MultimeterAudioProcessor::MultimeterAudioProcessor()
 : AudioProcessor(BusesProperties().withInput("Input", juce::AudioChannelSet::stereo(), true)
                                   .withOutput("Output", juce::AudioChannelSet::stereo(), true)) {}

void MultimeterAudioProcessor::prepareToPlay(double, int) {}

bool MultimeterAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();
    return (in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo())
        && in == out;
}

void MultimeterAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int channels = juce::jmin(2, buffer.getNumChannels());
    double sums[2] = {0.0, 0.0};
    float peaks[2] = {0.0f, 0.0f};
    for (int ch = 0; ch < channels; ++ch)
    {
        const auto* data = buffer.getReadPointer(ch);
        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            const float x = data[i];
            peaks[ch] = juce::jmax(peaks[ch], std::abs(x));
            sums[ch] += (double) x * (double) x;
        }
        const float blockRms = buffer.getNumSamples() > 0
            ? (float) std::sqrt(sums[ch] / (double) buffer.getNumSamples()) : 0.0f;
        const float oldPeak = peak[(size_t) ch].load();
        peak[(size_t) ch].store(juce::jmax(peaks[ch], oldPeak * 0.92f));
        const float oldRms = rms[(size_t) ch].load();
        rms[(size_t) ch].store(0.85f * oldRms + 0.15f * blockRms);
    }
    if (channels == 2)
    {
        const auto* l = buffer.getReadPointer(0);
        const auto* r = buffer.getReadPointer(1);
        double cross = 0.0, ll = 0.0, rr = 0.0;
        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            cross += (double) l[i] * r[i];
            ll += (double) l[i] * l[i];
            rr += (double) r[i] * r[i];
        }
        const double denom = std::sqrt(ll * rr);
        correlation.store(denom > 1.0e-12 ? (float) juce::jlimit(-1.0, 1.0, cross / denom) : 0.0f);
    }
    // Meter only: audio passes through unchanged.
}

juce::AudioProcessorEditor* MultimeterAudioProcessor::createEditor()
{
    return new MultimeterAudioProcessorEditor(*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MultimeterAudioProcessor();
}
