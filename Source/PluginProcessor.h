#pragma once
#include <JuceHeader.h>
#include <array>
#include <atomic>

class MultimeterAudioProcessor : public juce::AudioProcessor
{
public:
    static constexpr int spectrumBandCount = 63;
    static constexpr int gonioPointCount = 512;
    using ParameterState = juce::AudioProcessorValueTreeState;

    MultimeterAudioProcessor();
    ~MultimeterAudioProcessor() override = default;

    static ParameterState::ParameterLayout createParameterLayout();
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout&) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Multimeter"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    ParameterState parameters;
    float getParameter(const juce::String& id) const;
    float getPeak(int channel) const { return peak[(size_t) juce::jlimit(0, 1, channel)].load(); }
    float getRms(int channel) const { return rms[(size_t) juce::jlimit(0, 1, channel)].load(); }
    float getCorrelation() const { return correlation.load(); }
    float getSpectrumDb(int band) const
    {
        return spectrumDb[(size_t) juce::jlimit(0, spectrumBandCount - 1, band)].load();
    }
    float getLufsIntegrated() const { return lufsIntegrated.load(); }
    float getLufsShortTerm() const { return lufsShortTerm.load(); }
    void getGonioPoint(int index, float& x, float& y) const;
    void resetHold();

private:
    static constexpr int fftOrder = 13;
    static constexpr int fftSize = 1 << fftOrder;
    void updateSpectrum(float returnRateDbPerSecond, bool holdEnabled, int detectionMode, int meteringMode);

    std::array<std::atomic<float>, 2> peak {{0.0f, 0.0f}};
    std::array<std::atomic<float>, 2> rms {{0.0f, 0.0f}};
    std::atomic<float> correlation {0.0f};
    std::array<std::atomic<float>, spectrumBandCount> spectrumDb;
    std::array<std::atomic<float>, gonioPointCount> gonioX;
    std::array<std::atomic<float>, gonioPointCount> gonioY;
    std::atomic<int> gonioWriteIndex {0};
    std::atomic<float> lufsIntegrated {-70.0f};
    std::atomic<float> lufsShortTerm {-70.0f};

    juce::dsp::FFT fft { fftOrder };
    juce::dsp::WindowingFunction<float> window {
        fftSize, juce::dsp::WindowingFunction<float>::hann, false
    };
    std::array<float, fftSize> fftFifo {};
    std::array<float, fftSize * 2> fftData {};
    int fftFifoIndex = 0;
    double currentSampleRate = 44100.0;
    int gonioDecimator = 0;

    std::array<juce::dsp::IIR::Filter<float>, 2> kShelf;
    std::array<juce::dsp::IIR::Filter<float>, 2> kHighPass;
    double integratedEnergySum = 0.0;
    uint64_t integratedSampleCount = 0;
    double shortTermMeanEnergy = 1.0e-10;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MultimeterAudioProcessor)
};
