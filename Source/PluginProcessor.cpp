#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>
#include <limits>
#include <algorithm>

namespace
{
    constexpr double pi = 3.14159265358979323846;
    constexpr float dbFloor = -80.0f;
    constexpr int lookAheadMilliseconds = 5;

    float toDb(float value)
    {
        return value > 1.0e-7f ? 20.0f * std::log10(value) : dbFloor;
    }

    float dbToGain(float db) { return juce::Decibels::decibelsToGain(db); }
    juce::StringArray rateChoices()
    {
        return { "4 dB/s", "6.3 dB/s", "8.6 dB/s", "11.8 dB/s", "20 dB/s", "30 dB/s", "50 dB/s" };
    }
}

MultimeterAudioProcessor::ParameterState::ParameterLayout MultimeterAudioProcessor::createParameterLayout()
{
    ParameterState::ParameterLayout p;
    p.add(std::make_unique<juce::AudioParameterChoice>("view", "View",
        juce::StringArray { "Analyzer", "Goniometer" }, 0));
    p.add(std::make_unique<juce::AudioParameterChoice>("detect", "Detection",
        juce::StringArray { "Left", "Right", "LRmax", "Mono" }, 2));
    p.add(std::make_unique<juce::AudioParameterChoice>("mode", "Mode",
        juce::StringArray { "RMS Slow", "RMS Fast", "Peak" }, 2));
    p.add(std::make_unique<juce::AudioParameterChoice>("bands", "Analyzer Bands",
        juce::StringArray { "31", "63" }, 1));
    p.add(std::make_unique<juce::AudioParameterChoice>("top", "Top",
        juce::StringArray { "-15 dB", "-10 dB", "-5 dB", "0 dB", "+5 dB", "+10 dB", "+15 dB", "+20 dB" }, 4));
    p.add(std::make_unique<juce::AudioParameterChoice>("range", "Range",
        juce::StringArray { "20 dB", "30 dB", "40 dB", "50 dB", "60 dB", "70 dB", "80 dB" }, 4));
    p.add(std::make_unique<juce::AudioParameterBool>("hold", "Analyzer Hold", false));
    p.add(std::make_unique<juce::AudioParameterChoice>("holdtime", "Peak Hold",
        juce::StringArray { "2 s", "4 s", "6 s", "Infinite" }, 0));
    p.add(std::make_unique<juce::AudioParameterChoice>("returnrate", "Return Rate",
        rateChoices(), 3));
    p.add(std::make_unique<juce::AudioParameterChoice>("levelmode", "Level",
        juce::StringArray { "Peak", "Peak & RMS" }, 1));
    p.add(std::make_unique<juce::AudioParameterChoice>("weighting", "Weighting",
        juce::StringArray { "Flat", "A-weight", "C-weight" }, 0));
    p.add(std::make_unique<juce::AudioParameterBool>("limiter", "Peak Limiter", false));
    p.add(std::make_unique<juce::AudioParameterFloat>("ceiling", "Out Ceiling",
        juce::NormalisableRange<float>(-12.0f, 0.0f, 0.1f), -0.1f));
    p.add(std::make_unique<juce::AudioParameterFloat>("release", "Limiter Release",
        juce::NormalisableRange<float>(10.0f, 500.0f, 1.0f, 0.5f), 100.0f));
    p.add(std::make_unique<juce::AudioParameterFloat>("spectrumSmooth", "Spectrum Smoothing",
        juce::NormalisableRange<float>(0.03f, 1.0f, 0.01f), 0.18f));
    p.add(std::make_unique<juce::AudioParameterFloat>("correlationSmooth", "Correlation Averaging",
        juce::NormalisableRange<float>(0.10f, 2.0f, 0.01f), 0.55f));
    return p;
}

MultimeterAudioProcessor::MultimeterAudioProcessor()
    : AudioProcessor(BusesProperties()
        .withInput("Input", juce::AudioChannelSet::stereo(), true)
        .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      parameters(*this, nullptr, "PARAMETERS", createParameterLayout())
{
    for (auto& v : spectrumDb) v.store(dbFloor);
    for (auto& v : spectrumHoldDb) v.store(dbFloor);
    spectrumDisplayState.fill(dbFloor);
    for (auto& v : gonioX) v.store(0.0f);
    for (auto& v : gonioY) v.store(0.0f);
}

float MultimeterAudioProcessor::getParameter(const juce::String& id) const
{
    if (auto* v = parameters.getRawParameterValue(id)) return v->load();
    return 0.0f;
}

float MultimeterAudioProcessor::weightingCorrectionDb(float frequency, int weightingMode) noexcept
{
    const double f = juce::jmax(10.0f, frequency);
    if (weightingMode == 1)
    {
        const double f2 = f * f;
        const double numerator = 12200.0 * 12200.0 * f2 * f2;
        const double denominator = (f2 + 20.6 * 20.6)
            * std::sqrt((f2 + 107.7 * 107.7) * (f2 + 737.9 * 737.9))
            * (f2 + 12200.0 * 12200.0);
        return (float) (20.0 * std::log10(numerator / juce::jmax(1.0e-30, denominator)) + 2.0);
    }
    if (weightingMode == 2)
    {
        const double f2 = f * f;
        const double numerator = 12200.0 * 12200.0 * f2;
        const double denominator = (f2 + 20.6 * 20.6) * (f2 + 12200.0 * 12200.0);
        return (float) (20.0 * std::log10(numerator / juce::jmax(1.0e-30, denominator)) + 0.06);
    }
    return 0.0f;
}

void MultimeterAudioProcessor::buildFilterBanks()
{
    const double qValues[2] = { 4.318473047, 8.651198653 };
    for (int mode = 0; mode < 2; ++mode)
    {
        for (int band = 0; band < spectrumBandCount; ++band)
        {
            const double exponent = mode == 1 ? (double) band : (double) (2 * band + 2);
            const double fc = 15.625 * std::pow(2.0, exponent / 6.0);
            centreFrequencies[(size_t) mode][(size_t) band] = fc;
            const double w = std::tan(pi * juce::jmin(fc, currentSampleRate * 0.47) / currentSampleRate);
            const double bw = w * w / (juce::jmax(1.0e-12, std::atan(w)) * qValues[mode]);
            const double dr = -0.5 * bw * bw - 4.0 * w * w;
            const double di = -std::sqrt(3.0) * 0.5 * bw * bw;
            const double da = std::sqrt(dr * dr + di * di);
            const double u = std::sqrt(juce::jmax(0.0, (da + dr) * 0.5));
            const double v = -std::sqrt(juce::jmax(0.0, (da - dr) * 0.5));
            const double theta = 2.0 * std::atan(w);

            for (int section = 0; section < 3; ++section)
            {
                const double sigma = section == 0 ? -bw * 0.5
                    : (section == 1 ? (-bw * 0.5 + u) * 0.5 : (-bw * 0.5 - u) * 0.5);
                const double omega = section == 0
                    ? std::sqrt(juce::jmax(0.0, w * w - bw * bw * 0.25))
                    : (section == 1 ? std::abs((std::sqrt(3.0) * 0.5 * bw + v) * 0.5)
                                    : std::abs((std::sqrt(3.0) * 0.5 * bw - v) * 0.5));
                const double den = (1.0 - sigma) * (1.0 - sigma) + omega * omega;
                const double pre = (1.0 - sigma * sigma - omega * omega) / den;
                const double pim = 2.0 * omega / den;
                const double a1 = -2.0 * pre;
                const double a2 = pre * pre + pim * pim;
                const double re = 1.0 + a1 * std::cos(theta) + a2 * std::cos(2.0 * theta);
                const double im = -a1 * std::sin(theta) - a2 * std::sin(2.0 * theta);
                const double gain = std::sqrt(re * re + im * im)
                    / juce::jmax(1.0e-12, 2.0 * std::sin(theta));
                filterBanks[(size_t) mode][(size_t) band][(size_t) section] = {
                    (float) gain, (float) a1, (float) a2
                };
            }
        }
    }
}

float MultimeterAudioProcessor::runBandFilter(float input, std::array<BiquadState, 3>& state,
                                               const BandCoefficients& coefficients) noexcept
{
    float value = input;
    for (int s = 0; s < 3; ++s)
    {
        const auto& c = coefficients[(size_t) s];
        auto& z = state[(size_t) s];
        const float y = c.gain * value + z.s1;
        const float next1 = z.s2 - c.a1 * y;
        const float next2 = -c.gain * value - c.a2 * y;
        z.s1 = next1;
        z.s2 = next2;
        value = y;
    }
    return value;
}

void MultimeterAudioProcessor::resetBandHistories()
{
    for (auto& b : bands) b = BandState {};
    for (auto& v : spectrumDb) v.store(dbFloor);
    for (auto& v : spectrumHoldDb) v.store(dbFloor);
    spectrumDisplayState.fill(dbFloor);
}

void MultimeterAudioProcessor::getGonioPoint(int index, float& x, float& y) const
{
    const int write = gonioWriteIndex.load(std::memory_order_relaxed);
    const int slot = (write + juce::jlimit(0, gonioPointCount - 1, index)) % gonioPointCount;
    x = gonioX[(size_t) slot].load(std::memory_order_relaxed);
    y = gonioY[(size_t) slot].load(std::memory_order_relaxed);
}

void MultimeterAudioProcessor::resetHold()
{
    resetHoldRequested.store(true, std::memory_order_release);
}

void MultimeterAudioProcessor::prepareToPlay(double sampleRate, int)
{
    currentSampleRate = juce::jmax(8000.0, sampleRate);
    buildFilterBanks();
    resetBandHistories();
    currentBandMode = -1;
    currentDetectionMode = -1;
    publishCounter = 0;
    publishInterval = juce::jmax(1, (int) std::round(currentSampleRate / 50.0));
    gonioDecimator = 0;
    gonioWriteIndex.store(0);
    correlationLeftPower = correlationRightPower = 1.0e-12;
    correlationCrossPower = 0.0;
    correlation.store(0.0f);
    integratedEnergySum = 0.0;
    integratedSampleCount = 0;
    shortTermMeanEnergy = 1.0e-10;
    lufsIntegrated.store(-70.0f);
    lufsShortTerm.store(-70.0f);
    for (int ch = 0; ch < 2; ++ch)
    {
        peak[(size_t) ch].store(0.0f);
        peakHold[(size_t) ch].store(0.0f);
        rms[(size_t) ch].store(0.0f);
        meterPeakEnvelope[(size_t) ch] = meterPeakHeld[(size_t) ch] = 0.0f;
        meterPeakAge[(size_t) ch] = meterHoldAge[(size_t) ch] = 0;
        meterRmsPower[(size_t) ch] = 0.0;
    }

    const auto shelf = juce::dsp::IIR::Coefficients<float>::makeHighShelf(
        currentSampleRate, 1681.974450955533, 0.7071752369554196,
        juce::Decibels::decibelsToGain(3.999843853973347));
    const auto highPass = juce::dsp::IIR::Coefficients<float>::makeHighPass(
        currentSampleRate, 38.13547087602444, 0.5003270373238773);
    for (int ch = 0; ch < 2; ++ch)
    {
        kShelf[(size_t) ch].reset();
        kHighPass[(size_t) ch].reset();
        kShelf[(size_t) ch].coefficients = shelf;
        kHighPass[(size_t) ch].coefficients = highPass;
    }

    lookAheadSamples = juce::jmax(1, (int) std::round(currentSampleRate * lookAheadMilliseconds / 1000.0));
    delayLength = lookAheadSamples + 1;
    limiterDelayLeft.assign((size_t) delayLength, 0.0f);
    limiterDelayRight.assign((size_t) delayLength, 0.0f);
    limiterPeakDequeValues.assign((size_t) delayLength + 2, 0.0f);
    limiterPeakDequeIndices.assign((size_t) delayLength + 2, 0);
    delayWrite = 0;
    limiterSampleCounter = 0;
    dequeHead = dequeTail = 0;
    limiterGain = 1.0f;
    previousLimiterInputLeft = previousLimiterInputRight = 0.0f;
    previousLimiterInputLeft2 = previousLimiterInputRight2 = 0.0f;
    previousLimiterInputLeft3 = previousLimiterInputRight3 = 0.0f;
    limiterWasEnabled = false;
    limiterGainReductionDb.store(0.0f);
    setLatencySamples(lookAheadSamples);
}

bool MultimeterAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto input = layouts.getMainInputChannelSet();
    const auto output = layouts.getMainOutputChannelSet();
    return (input == juce::AudioChannelSet::mono() || input == juce::AudioChannelSet::stereo())
        && input == output;
}

void MultimeterAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int inputChannels = juce::jmin(2, getTotalNumInputChannels(), buffer.getNumChannels());
    const int outputChannels = juce::jmin(2, getTotalNumOutputChannels(), buffer.getNumChannels());
    const int numSamples = buffer.getNumSamples();
    if (inputChannels <= 0 || outputChannels <= 0 || numSamples <= 0) return;

    const int detectMode = juce::jlimit(0, 3, (int) getParameter("detect"));
    const int meterMode = juce::jlimit(0, 2, (int) getParameter("mode"));
    const int bandMode = getParameter("bands") > 0.5f ? 1 : 0;
    const int bandCount = bandMode == 1 ? 63 : 31;
    const int weightingMode = juce::jlimit(0, 2, (int) getParameter("weighting"));
    const float returnRates[] = { 4.0f, 6.3f, 8.6f, 11.8f, 20.0f, 30.0f, 50.0f };
    const float returnRate = returnRates[juce::jlimit(0, 6, (int) getParameter("returnrate"))];
    const bool holdEnabled = getParameter("hold") > 0.5f;
    const int holdTimeIndex = juce::jlimit(0, 3, (int) getParameter("holdtime"));
    const double holdTimes[] = { 2.0, 4.0, 6.0, std::numeric_limits<double>::infinity() };
    const double holdSamples = holdTimes[holdTimeIndex] * currentSampleRate;
    const double slowAlpha = std::exp(-1.0 / (currentSampleRate * 0.300));
    const double fastAlpha = std::exp(-1.0 / (currentSampleRate * 0.050));
    const double rmsAlpha = meterMode == 0 ? slowAlpha : fastAlpha;
    const double specTau = getParameter("spectrumSmooth");
    const double correlationTau = getParameter("correlationSmooth");
    const double corrAlpha = std::exp(-1.0 / (currentSampleRate * correlationTau));
    const float peakDecay = std::exp(-returnRate * 0.11512925465f / (float) currentSampleRate);
    const bool limiterEnabled = getParameter("limiter") > 0.5f;
    const float ceilingGain = dbToGain(getParameter("ceiling"));
    const float limiterReleaseSeconds = juce::jmax(0.010f, getParameter("release") * 0.001f);
    const float limiterReleaseCoeff = std::exp(-1.0f / (limiterReleaseSeconds * (float) currentSampleRate));

    if (bandMode != currentBandMode || detectMode != currentDetectionMode)
    {
        resetBandHistories();
        currentBandMode = bandMode;
        currentDetectionMode = detectMode;
    }
    if (resetHoldRequested.exchange(false, std::memory_order_acq_rel))
    {
        for (auto& b : bands) { b.heldPeak = dbFloor; b.holdAge = 0; }
        for (auto& v : spectrumHoldDb) v.store(dbFloor, std::memory_order_relaxed);
        for (int ch = 0; ch < 2; ++ch)
        {
            meterPeakHeld[(size_t) ch] = 0.0f;
            meterHoldAge[(size_t) ch] = 0;
            peakHold[(size_t) ch].store(0.0f, std::memory_order_relaxed);
        }
    }

    auto* outL = buffer.getWritePointer(0);
    auto* outR = outputChannels > 1 ? buffer.getWritePointer(1) : nullptr;
    const auto* inL = buffer.getReadPointer(0);
    const auto* inR = inputChannels > 1 ? buffer.getReadPointer(1) : inL;
    const bool monoOutput = outputChannels == 1;

    double blockLoudnessEnergy = 0.0;
    float blockCorrelationValue = correlation.load(std::memory_order_relaxed);

    for (int sample = 0; sample < numSamples; ++sample)
    {
        const float inputL = inL[sample];
        const float inputR = inputChannels > 1 ? inR[sample] : inputL;

        // One-sixth-octave filter bank: three cascaded sections per band,
        // following the center-frequency and coefficient model in the supplied AMXD GenExpr.
        for (int band = 0; band < bandCount; ++band)
        {
            auto& state = bands[(size_t) band];
            const auto& coefficients = filterBanks[(size_t) bandMode][(size_t) band];
            const float leftBand = runBandFilter(inputL, state.left, coefficients);
            const float rightBand = runBandFilter(inputR, state.right, coefficients);
            const double leftPower = (double) leftBand * leftBand;
            const double rightPower = (double) rightBand * rightBand;

            double selectedPower = 0.0;
            float selectedPeak = 0.0f;
            if (detectMode == 0) { selectedPower = leftPower; selectedPeak = std::abs(leftBand); }
            else if (detectMode == 1) { selectedPower = rightPower; selectedPeak = std::abs(rightBand); }
            else if (detectMode == 2)
            {
                selectedPower = juce::jmax(leftPower, rightPower);
                selectedPeak = juce::jmax(std::abs(leftBand), std::abs(rightBand));
            }
            else
            {
                selectedPower = 0.5 * (leftPower + rightPower);
                selectedPeak = std::sqrt((float) selectedPower);
            }

            state.powerSlow = slowAlpha * state.powerSlow + (1.0 - slowAlpha) * selectedPower;
            state.powerFast = fastAlpha * state.powerFast + (1.0 - fastAlpha) * selectedPower;
            state.peakEnvelope = juce::jmax(selectedPeak, state.peakEnvelope * peakDecay);

        }

        // Stereo-linked look-ahead limiter. The max deque is updated without allocation.
        const uint64_t currentIndex = limiterSampleCounter;
        const int writeSlot = (int) (currentIndex % (uint64_t) delayLength);
        limiterDelayLeft[(size_t) writeSlot] = inputL;
        limiterDelayRight[(size_t) writeSlot] = inputR;

        const float prevL = previousLimiterInputLeft;
        const float prevR = previousLimiterInputRight;
        const float prev2L = previousLimiterInputLeft2;
        const float prev2R = previousLimiterInputRight2;
        const float prev3L = previousLimiterInputLeft3;
        const float prev3R = previousLimiterInputRight3;
        previousLimiterInputLeft3 = previousLimiterInputLeft2;
        previousLimiterInputRight3 = previousLimiterInputRight2;
        previousLimiterInputLeft2 = previousLimiterInputLeft;
        previousLimiterInputRight2 = previousLimiterInputRight;
        previousLimiterInputLeft = inputL;
        previousLimiterInputRight = inputR;

        float detectorPeak = juce::jmax(std::abs(inputL), std::abs(inputR));
        // Four-point Lagrange interpolation checks three inter-sample positions
        // across the most recent interval without adding another signal path.
        for (int os = 1; os < 4; ++os)
        {
            const double t = -1.0 + (double) os * 0.25;
            const double w3 = (t + 2.0) * (t + 1.0) * t / -6.0;
            const double w2 = (t + 3.0) * (t + 1.0) * t / 2.0;
            const double w1 = (t + 3.0) * (t + 2.0) * t / -2.0;
            const double w0 = (t + 3.0) * (t + 2.0) * (t + 1.0) / 6.0;
            const float interpolatedL = (float) (w3 * prev3L + w2 * prev2L + w1 * prevL + w0 * inputL);
            const float interpolatedR = (float) (w3 * prev3R + w2 * prev2R + w1 * prevR + w0 * inputR);
            detectorPeak = juce::jmax(detectorPeak,
                juce::jmax(std::abs(interpolatedL), std::abs(interpolatedR)));
        }

        const size_t dequeCapacity = limiterPeakDequeValues.size();
        while (dequeHead != dequeTail)
        {
            const size_t last = (dequeTail + dequeCapacity - 1) % dequeCapacity;
            if (limiterPeakDequeValues[last] > detectorPeak) break;
            dequeTail = last;
        }
        limiterPeakDequeValues[dequeTail] = detectorPeak;
        limiterPeakDequeIndices[dequeTail] = currentIndex;
        dequeTail = (dequeTail + 1) % dequeCapacity;

        const uint64_t oldestIndex = currentIndex >= (uint64_t) lookAheadSamples
            ? currentIndex - (uint64_t) lookAheadSamples : 0;
        while (dequeHead != dequeTail && limiterPeakDequeIndices[dequeHead] < oldestIndex)
            dequeHead = (dequeHead + 1) % dequeCapacity;

        float delayedL = 0.0f, delayedR = 0.0f;
        if (currentIndex >= (uint64_t) lookAheadSamples)
        {
            const size_t delayedSlot = (size_t) (oldestIndex % (uint64_t) delayLength);
            delayedL = limiterDelayLeft[delayedSlot];
            delayedR = limiterDelayRight[delayedSlot];
        }

        float targetGain = 1.0f;
        if (limiterEnabled && dequeHead != dequeTail)
        {
            const float lookAheadPeak = limiterPeakDequeValues[dequeHead];
            if (lookAheadPeak > ceilingGain && lookAheadPeak > 1.0e-9f)
                targetGain = ceilingGain / lookAheadPeak;
        }

        if (limiterEnabled)
        {
            if (targetGain < limiterGain) limiterGain = targetGain;
            else limiterGain = 1.0f - (1.0f - limiterGain) * limiterReleaseCoeff;
        }
        else limiterGain = 1.0f;

        float outputL = delayedL * limiterGain;
        float outputR = delayedR * limiterGain;
        if (limiterEnabled)
        {
            outputL = juce::jlimit(-ceilingGain, ceilingGain, outputL);
            outputR = juce::jlimit(-ceilingGain, ceilingGain, outputR);
        }

        if (monoOutput) outL[sample] = 0.5f * (outputL + outputR);
        else
        {
            outL[sample] = outputL;
            outR[sample] = outputR;
        }

        const float outputPeak[2] = { std::abs(outputL), std::abs(outputR) };
        const float outputValue[2] = { outputL, outputR };
        for (int ch = 0; ch < 2; ++ch)
        {
            const size_t channel = (size_t) ch;
            meterPeakEnvelope[channel] = juce::jmax(outputPeak[ch], meterPeakEnvelope[channel] * peakDecay);
            meterRmsPower[channel] = rmsAlpha * meterRmsPower[channel]
                + (1.0 - rmsAlpha) * (double) outputValue[ch] * outputValue[ch];
            if (outputPeak[ch] >= meterPeakHeld[channel])
            {
                meterPeakHeld[channel] = outputPeak[ch];
                meterHoldAge[channel] = 0;
            }
            else
            {
                meterHoldAge[channel]++;
                if (!holdEnabled || (double) meterHoldAge[channel] > holdSamples)
                    meterPeakHeld[channel] = juce::jmax(outputPeak[ch], meterPeakHeld[channel] * peakDecay);
            }
            meterPeakAge[channel]++;
        }

        // Averaged phase correlation: cross and channel powers share the same time constant.
        correlationLeftPower = corrAlpha * correlationLeftPower
            + (1.0 - corrAlpha) * (double) outputL * outputL;
        correlationRightPower = corrAlpha * correlationRightPower
            + (1.0 - corrAlpha) * (double) outputR * outputR;
        correlationCrossPower = corrAlpha * correlationCrossPower
            + (1.0 - corrAlpha) * (double) outputL * outputR;
        const double correlationDenominator = std::sqrt(correlationLeftPower * correlationRightPower);
        const float corrValue = correlationDenominator > 1.0e-12
            ? (float) juce::jlimit(-1.0, 1.0, correlationCrossPower / correlationDenominator) : 0.0f;
        blockCorrelationValue = corrValue;

        // Goniometer: M/S (sum/difference) with a fixed decimation cadence.
        if (++gonioDecimator >= 8)
        {
            gonioDecimator = 0;
            const int slot = gonioWriteIndex.load(std::memory_order_relaxed);
            gonioX[(size_t) slot].store((outputL + outputR) * 0.70710678f, std::memory_order_relaxed);
            gonioY[(size_t) slot].store((outputL - outputR) * 0.70710678f, std::memory_order_relaxed);
            gonioWriteIndex.store((slot + 1) % gonioPointCount, std::memory_order_relaxed);
        }

        const float weightedL = kHighPass[0].processSample(kShelf[0].processSample(outputL));
        const float weightedR = kHighPass[1].processSample(kShelf[1].processSample(outputR));
        blockLoudnessEnergy += 0.5 * ((double) weightedL * weightedL + (double) weightedR * weightedR);

        ++limiterSampleCounter;
        ++publishCounter;
        if (publishCounter >= publishInterval)
        {
            publishCounter = 0;
            const double displayStepSeconds = (double) publishInterval / currentSampleRate;
            for (int band = 0; band < bandCount; ++band)
            {
                auto& state = bands[(size_t) band];
                const double rawPower = meterMode == 0 ? state.powerSlow : state.powerFast;
                const float rawMagnitude = meterMode == 2
                    ? state.peakEnvelope
                    : (float) std::sqrt(juce::jmax(0.0, rawPower));
                const float weightedDb = toDb(rawMagnitude) + weightingCorrectionDb(
                    (float) centreFrequencies[(size_t) bandMode][(size_t) band], weightingMode);
                const float targetDb = juce::jlimit(dbFloor, 12.0f, weightedDb);

                const float priorDb = spectrumDisplayState[(size_t) band];
                const double displayTau = targetDb > priorDb ? juce::jmax(0.020, specTau * 0.35) : specTau;
                const float smoothing = (float) (1.0 - std::exp(-displayStepSeconds / displayTau));
                const float displayDb = priorDb + smoothing * (targetDb - priorDb);
                spectrumDisplayState[(size_t) band] = displayDb;

                if (holdEnabled)
                {
                    if (targetDb >= state.heldPeak)
                    {
                        state.heldPeak = targetDb;
                        state.holdAge = 0;
                    }
                    else
                    {
                        state.holdAge += (uint64_t) publishInterval;
                        if ((double) state.holdAge > holdSamples)
                        {
                            const float released = state.heldPeak
                                - returnRate * (float) displayStepSeconds * 0.11512925465f;
                            state.heldPeak = juce::jmax(targetDb, released);
                        }
                    }
                }
                else
                {
                    state.heldPeak = targetDb;
                    state.holdAge = 0;
                }

                spectrumDb[(size_t) band].store(displayDb, std::memory_order_relaxed);
                spectrumHoldDb[(size_t) band].store(state.heldPeak, std::memory_order_relaxed);
            }
        }
    }

    for (int ch = 0; ch < 2; ++ch)
    {
        peak[(size_t) ch].store(meterPeakEnvelope[(size_t) ch], std::memory_order_relaxed);
        peakHold[(size_t) ch].store(meterPeakHeld[(size_t) ch], std::memory_order_relaxed);
        rms[(size_t) ch].store((float) std::sqrt(juce::jmax(0.0, meterRmsPower[(size_t) ch])),
                               std::memory_order_relaxed);
    }
    correlation.store(blockCorrelationValue, std::memory_order_relaxed);

    const double blockMean = blockLoudnessEnergy / (double) numSamples;
    integratedEnergySum += blockLoudnessEnergy;
    integratedSampleCount += (uint64_t) numSamples;
    const double integratedMean = integratedSampleCount > 0
        ? integratedEnergySum / (double) integratedSampleCount : 1.0e-10;
    const double shortAlpha = std::exp(-(double) numSamples / (currentSampleRate * 3.0));
    shortTermMeanEnergy = shortAlpha * shortTermMeanEnergy + (1.0 - shortAlpha) * blockMean;
    lufsIntegrated.store((float) juce::jlimit(-70.0, 12.0,
        -0.691 + 10.0 * std::log10(juce::jmax(1.0e-10, integratedMean))), std::memory_order_relaxed);
    lufsShortTerm.store((float) juce::jlimit(-70.0, 12.0,
        -0.691 + 10.0 * std::log10(juce::jmax(1.0e-10, shortTermMeanEnergy))), std::memory_order_relaxed);
    limiterGainReductionDb.store(limiterEnabled ? -20.0f * std::log10(juce::jmax(1.0e-6f, limiterGain)) : 0.0f,
                                 std::memory_order_relaxed);
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
