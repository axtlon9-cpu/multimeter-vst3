#pragma once
#include <JuceHeader.h>
#include <array>
#include <atomic>
#include <vector>

class MultimeterAudioProcessor : public juce::AudioProcessor
{
public:
    static constexpr int spectrumBandCount = 63;
    static constexpr int gonioPointCount = 512;
    using ParameterState = juce::AudioProcessorValueTreeState;

    struct BiquadCoefficients { float gain = 1.0f, a1 = 0.0f, a2 = 0.0f; };
    using BandCoefficients = std::array<BiquadCoefficients, 3>;
    struct BiquadState { float s1 = 0.0f, s2 = 0.0f; };
    struct BandState
    {
        std::array<BiquadState, 3> left {}, right {};
        double powerSlow = 0.0, powerFast = 0.0;
        float peakEnvelope = 0.0f, heldPeak = 0.0f;
        uint64_t envelopeAge = 0, holdAge = 0;
    };

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
    float getPeakHold(int channel) const { return peakHold[(size_t) juce::jlimit(0, 1, channel)].load(); }
    float getRms(int channel) const { return rms[(size_t) juce::jlimit(0, 1, channel)].load(); }
    float getCorrelation() const { return correlation.load(); }
    float getSpectrumDb(int band) const
    {
        return spectrumDb[(size_t) juce::jlimit(0, spectrumBandCount - 1, band)].load();
    }
    float getSpectrumHoldDb(int band) const
    {
        return spectrumHoldDb[(size_t) juce::jlimit(0, spectrumBandCount - 1, band)].load();
    }
    float getLufsIntegrated() const { return lufsIntegrated.load(); }
    float getLufsShortTerm() const { return lufsShortTerm.load(); }
    float getLimiterGainReductionDb() const { return limiterGainReductionDb.load(); }
    void getGonioPoint(int index, float& x, float& y) const;
    void resetHold();

private:
    void buildFilterBanks();
    static float runBandFilter(float input, std::array<BiquadState, 3>& state,
                               const BandCoefficients& coefficients) noexcept;
    void resetBandHistories();
    static float weightingCorrectionDb(float frequency, int weightingMode) noexcept;

    std::array<std::array<BandCoefficients, spectrumBandCount>, 2> filterBanks {};
    std::array<BandState, spectrumBandCount> bands {};
    std::array<std::array<double, spectrumBandCount>, 2> centreFrequencies {};
    std::array<std::atomic<float>, spectrumBandCount> spectrumDb;\n    std::array<float, spectrumBandCount> spectrumDisplayState {};
    std::array<std::atomic<float>, spectrumBandCount> spectrumHoldDb;
    int currentBandMode = -1;
    int currentDetectionMode = -1;
    int publishCounter = 0;
    int publishInterval = 735;

    std::array<std::atomic<float>, 2> peak {{0.0f, 0.0f}};
    std::array<std::atomic<float>, 2> peakHold {{0.0f, 0.0f}};
    std::array<std::atomic<float>, 2> rms {{0.0f, 0.0f}};
    std::array<float, 2> meterPeakEnvelope {{0.0f, 0.0f}};
    std::array<float, 2> meterPeakHeld {{0.0f, 0.0f}};
    std::array<uint64_t, 2> meterPeakAge {{0, 0}};
    std::array<uint64_t, 2> meterHoldAge {{0, 0}};
    std::array<double, 2> meterRmsPower {{0.0, 0.0}};

    std::atomic<float> correlation {0.0f};
    double correlationLeftPower = 1.0e-12;
    double correlationRightPower = 1.0e-12;
    double correlationCrossPower = 0.0;

    std::array<std::atomic<float>, gonioPointCount> gonioX;
    std::array<std::atomic<float>, gonioPointCount> gonioY;
    std::atomic<int> gonioWriteIndex {0};
    int gonioDecimator = 0;

    std::atomic<float> lufsIntegrated {-70.0f};
    std::atomic<float> lufsShortTerm {-70.0f};
    std::array<juce::dsp::IIR::Filter<float>, 2> kShelf;
    std::array<juce::dsp::IIR::Filter<float>, 2> kHighPass;
    double integratedEnergySum = 0.0;
    uint64_t integratedSampleCount = 0;
    double shortTermMeanEnergy = 1.0e-10;

    // Stereo-linked look-ahead peak limiter.
    std::vector<float> limiterDelayLeft, limiterDelayRight;
    std::vector<float> limiterPeakDequeValues;
    std::vector<uint64_t> limiterPeakDequeIndices;
    int lookAheadSamples = 0;
    int delayLength = 1;
    int delayWrite = 0;
    uint64_t limiterSampleCounter = 0;
    uint64_t dequeHead = 0, dequeTail = 0;
    float limiterGain = 1.0f;
    float previousLimiterInputLeft = 0.0f, previousLimiterInputRight = 0.0f;\n    float previousLimiterInputLeft2 = 0.0f, previousLimiterInputRight2 = 0.0f;\n    float previousLimiterInputLeft3 = 0.0f, previousLimiterInputRight3 = 0.0f;
    bool limiterWasEnabled = false;
    std::atomic<float> limiterGainReductionDb {0.0f};
    std::atomic<bool> resetHoldRequested {false};

    double currentSampleRate = 44100.0;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MultimeterAudioProcessor)
};
