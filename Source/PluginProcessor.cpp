#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>
#include <algorithm>

namespace
{
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
    {
        std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

        params.push_back(std::make_unique<juce::AudioParameterChoice>(
            juce::ParameterID("shape", 1),
            "shape",
            juce::StringArray{"Sine", "Triangle", "Saw", "Square"},
            0));

        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID("mix", 1),
            "mix",
            juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f),
            0.0f));

        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID("tone", 1),
            "tone",
            juce::NormalisableRange<float>(200.0f, 16000.0f, 1.0f, 0.3f),
            4000.0f,
            juce::AudioParameterFloatAttributes().withLabel("Hz")));

        params.push_back(std::make_unique<juce::AudioParameterChoice>(
            juce::ParameterID("rate", 1),
            "rate",
            juce::StringArray{"16th", "16th Triplet", "Dotted 32nd", "32nd", "32nd Triplet", "Dotted 64th", "64th"},
            0));

        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID("input", 1),
            "input",
            juce::NormalisableRange<float>(0.0f, 40.0f, 0.1f),
            0.0f,
            juce::AudioParameterFloatAttributes().withLabel("dB")));

        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID("depth", 1),
            "depth",
            juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f),
            0.8f,
            juce::AudioParameterFloatAttributes().withLabel("Hz")));

        params.push_back(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID("blur", 1),
            "blur",
            juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f),
            0.0f));

        return { params.begin(), params.end() };
    }
}

PluginProcessor::PluginProcessor()
    : AudioProcessor(BusesProperties()
                     .withInput("Input", juce::AudioChannelSet::stereo(), true)
                     .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      parameters(*this, nullptr, juce::Identifier("Mikiri"), createParameterLayout()),
      fft(fftOrder)
{
    shapeParam = parameters.getRawParameterValue("shape");
    mixParam = parameters.getRawParameterValue("mix");
    toneParam = parameters.getRawParameterValue("tone");
    rateParam = parameters.getRawParameterValue("rate");
    driveParam = parameters.getRawParameterValue("input");
    depthParam = parameters.getRawParameterValue("depth");
    blurParam = parameters.getRawParameterValue("blur");

    for (int i = 0; i < fftSize; ++i)
        windowFunction[i] = 0.5f - 0.5f * std::cos(2.0f * juce::MathConstants<float>::pi * i / (fftSize - 1));
}

PluginProcessor::~PluginProcessor() = default;

const juce::String PluginProcessor::getName() const
{
    return JucePlugin_Name;
}

void PluginProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;

    arp1.setSampleRate(sampleRate);
    arp2.setSampleRate(sampleRate);

    juce::dsp::ProcessSpec spec;
    spec.sampleRate = sampleRate;
    spec.maximumBlockSize = static_cast<juce::uint32>(samplesPerBlock);
    spec.numChannels = 2;

    shiftHighpass.prepare(spec);
    shiftHighpass.setType(juce::dsp::StateVariableTPTFilterType::highpass);

    synthLowpass.prepare(spec);
    synthLowpass.setType(juce::dsp::StateVariableTPTFilterType::lowpass);

    synthHighpass.prepare(spec);
    synthHighpass.setType(juce::dsp::StateVariableTPTFilterType::highpass);

    synthPhaser.prepare(spec);
    synthPhaser.setRate(0.22f);
    synthPhaser.setDepth(1.0f);
    synthPhaser.setCentreFrequency(1200.0f);
    synthPhaser.setFeedback(0.7f);
    synthPhaser.setMix(0.6f);

    crossoverLow.prepare(spec);
    crossoverLow.setType(juce::dsp::StateVariableTPTFilterType::lowpass);
    crossoverLow.setCutoffFrequency(540.0f);

    crossoverHigh.prepare(spec);
    crossoverHigh.setType(juce::dsp::StateVariableTPTFilterType::highpass);
    crossoverHigh.setCutoffFrequency(540.0f);

    reverbLow.setSampleRate(sampleRate);
    reverbHigh.setSampleRate(sampleRate);

    synthPeakEQ.prepare(spec);

    for (int ch = 0; ch < maxChannels; ++ch)
    {
        inputBuffer[ch].fill(0.0f);
        inputWritePos[ch] = 0;
        outputReadPos[ch] = 0;
        samplesUntilNextHop[ch] = 0;
    }

    fftWorkBuffer.fill(0.0f);
    magnitudes.fill(0.0f);
    salience.fill(0.0f);
    accumulatedSalience.fill(0.0f);
    salienceFrameCount = 0;
    needsPitchUpdate = true;
}

void PluginProcessor::releaseResources()
{
}

void PluginProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ignoreUnused(midiMessages);
    juce::ScopedNoDenormals noDenormals;

    const int numChannels = juce::jmin(buffer.getNumChannels(), maxChannels);
    const int numSamples = buffer.getNumSamples();
    const float blendMix = mixParam->load();
    constexpr float synthGainLinear = 8.0f;
    const float driveDb = driveParam->load();
    const float driveGain = std::pow(10.0f, driveDb / 20.0f);
    const float dryCompensation = 1.0f / driveGain;

    for (int ch = 0; ch < juce::jmin(buffer.getNumChannels(), maxChannels); ++ch)
        buffer.applyGain(ch, 0, buffer.getNumSamples(), driveGain);

    double bpm = 120.0;
    double ppqPosition = 0.0;
    bool isPlaying = false;
    if (auto* playHead = getPlayHead())
    {
        auto posInfo = playHead->getPosition();
        if (posInfo.hasValue())
        {
            if (posInfo->getBpm().hasValue())
                bpm = *posInfo->getBpm();
            if (posInfo->getPpqPosition().hasValue())
                ppqPosition = *posInfo->getPpqPosition();
            if (posInfo->getIsPlaying())
                isPlaying = true;
        }
    }

    static constexpr double rateMultipliers[] = {
        4.0,          // 16th: 4 per beat
        6.0,          // 16th triplet: 6 per beat
        16.0 / 3.0,   // dotted 32nd: duration = 3/16 beat, rate = 16/3 per beat
        8.0,          // 32nd: 8 per beat
        12.0,         // 32nd triplet: 12 per beat
        32.0 / 3.0,   // dotted 64th: duration = 3/32 beat, rate = 32/3 per beat
        16.0          // 64th: 16 per beat
    };
    int rateIdx = static_cast<int>(rateParam->load());

    int numPitches;
    {
        juce::SpinLock::ScopedLockType lock(frequencyLock);
        numPitches = static_cast<int>(detectedPitches.size());
    }

    double stepsPerBeat = rateMultipliers[rateIdx];
    arp1.syncToHost(ppqPosition, stepsPerBeat, numPitches, isPlaying);
    arp2.syncToHost(ppqPosition, stepsPerBeat, numPitches, isPlaying);

    if (!isPlaying)
    {
        arp1.advanceFreeRunning(numSamples, currentSampleRate, bpm, stepsPerBeat);
        arp2.advanceFreeRunning(numSamples, currentSampleRate, bpm, stepsPerBeat);
    }

    bool stepped = arp1.consumeStepped();
    if (stepped)
    {
        needsPitchUpdate = true;

        if (rmsSampleCount > 0)
        {
            float rms = static_cast<float>(std::sqrt(rmsAccumulator / static_cast<double>(rmsSampleCount)));
            float dB = (rms > 1e-10f) ? 20.0f * std::log10(rms) : -100.0f;
            float normalized = juce::jlimit(0.0f, 1.0f, (dB + 40.0f) / 40.0f);
            envelope = std::max(envelope, normalized);
            rmsAccumulator = 0.0;
            rmsSampleCount = 0;
        }
    }

    constexpr float releaseMs = 200.0f;
    float releaseCoeff = std::exp(-1.0f / (releaseMs * 0.001f * static_cast<float>(currentSampleRate) / static_cast<float>(numSamples)));
    envelope *= releaseCoeff;

    arp2.consumeStepped();

    juce::AudioBuffer<float> synthBuffer(numChannels, numSamples);
    synthBuffer.clear();

    std::vector<DetectedPitch> pitches;
    {
        juce::SpinLock::ScopedLockType lock(frequencyLock);
        pitches = detectedPitches;
    }

    float magScale = 1.0f / static_cast<float>(fftSize);
    float glide = 0.95f - 0.6f * envelope;
    double msPerStep = 60000.0 / (bpm * stepsPerBeat);
    double attackMs = std::max(2.0, std::min(60.0, msPerStep) * (1.0 - envelope * 0.9));
    double decayMs = std::max(msPerStep - attackMs, 5.0);
    arp1.setSmoothing(glide);
    arp2.setSmoothing(glide);
    arp1.setAttackMs(attackMs);
    arp2.setAttackMs(attackMs);
    arp1.setDecayMs(decayMs);
    arp2.setDecayMs(decayMs);

    arp1.setTarget(pitches, magScale);
    arp2.setTarget(pitches, magScale, 1);
    const int waveType = static_cast<int>(shapeParam->load());

    float toneValue = toneParam->load();
    toneValue *= 1.0f + 4.0f * envelope;
    toneValue = std::min(toneValue, 16000.0f);

    if (stepped) {
        // add notes to the visualizer
        VizStep step;
        step.mix = mixParam->load();
        step.blur = blurParam->load();
        step.envelope = envelope;
        step.tone = toneValue;
        step.depth = depthParam->load();
        step.shape = waveType;
        step.stepsPerSecond = static_cast<float>((bpm * stepsPerBeat) / 60.0);

        auto pitch1 = arp1.getPitch();
        auto pitch2 = arp2.getPitch();
        if (pitch1.frequency > 0.0f) {
            step.tones.push_back(pitch1);
        }
        if (pitch2.frequency > 0.0f) {
            step.tones.push_back(pitch2);
        }

        constexpr size_t maxSteps = 32;
        {
            juce::SpinLock::ScopedLockType lock(vizLock);
            vizSteps.push_back(step);
            while (vizSteps.size() > maxSteps) {
                vizSteps.pop_front();
            }
        }
    }
    const double invSampleRate = 1.0 / currentSampleRate;

    for (int ch = 0; ch < numChannels; ++ch)
    {
        const float* channelData = buffer.getReadPointer(ch);
        for (int i = 0; i < numSamples; ++i)
        {
            inputBuffer[ch][inputWritePos[ch]] = channelData[i];

            rmsAccumulator += static_cast<double>(channelData[i]) * static_cast<double>(channelData[i]);
            rmsSampleCount++;

            inputWritePos[ch] = (inputWritePos[ch] + 1) % fftSize;
            outputReadPos[ch] = (outputReadPos[ch] + 1) % fftSize;

            if (--samplesUntilNextHop[ch] <= 0)
            {
                processChannel(ch);
                samplesUntilNextHop[ch] = hopSize;
            }
        }
    }

    float pan1 = arp1.getPan();
    float pan2 = arp2.getPan();
    float gainL1 = 0.5f * (1.0f - pan1), gainR1 = 0.5f * (1.0f + pan1);
    float gainL2 = 0.5f * (1.0f - pan2), gainR2 = 0.5f * (1.0f + pan2);

    float* synthL = synthBuffer.getWritePointer(0);
    float* synthR = numChannels > 1 ? synthBuffer.getWritePointer(1) : nullptr;

    for (int i = 0; i < numSamples; ++i)
    {
        float s1 = arp1.generateAndAdvance(invSampleRate, waveType);
        synthL[i] += s1 * gainL1;
        if (synthR) synthR[i] += s1 * gainR1;
    }

    for (int i = 0; i < numSamples; ++i)
    {
        float s2 = arp2.generateAndAdvance(invSampleRate, waveType);
        synthL[i] += s2 * gainL2;
        if (synthR) synthR[i] += s2 * gainR2;
    }

    for (int i = 0; i < numSamples; ++i) {
        synthL[i] = applyClipping(synthL[i]);
        if (synthR)
            synthR[i] = applyClipping(synthR[i]);
    }

    const float depth = depthParam->load();
    const float depthx3 = 3.0f * depth;
    float shift1Amount = 0.0f;
    float shift2Amount = 0.0f;
    float shift3Amount = 0.0f;
    if (depthx3 >= 2.0f)
        shift1Amount = std::sin((depthx3-2.0f) * juce::MathConstants<float>::halfPi);
    if (depthx3 >= 1.0f)
        shift2Amount = std::sin((depthx3-1.0f) * juce::MathConstants<float>::halfPi);
    if (depthx3 <= 1.5f)
        shift3Amount = std::sin((depthx3+0.5f) * juce::MathConstants<float>::halfPi);

    // pitch shifter
    juce::AudioBuffer<float> shiftBuffer(numChannels, numSamples);
    shiftBuffer.clear();

    shift1.update(currentSampleRate, bpm, stepsPerBeat);
    shift2.update(currentSampleRate, bpm, stepsPerBeat);
    shift3.update(currentSampleRate, bpm, stepsPerBeat);

    const float blur = blurParam->load();
    const float feedback = 0.1f + blur*0.5f;
    shift1.process(buffer, shiftBuffer, feedback, shift1Amount * envelope);
    shift2.process(buffer, shiftBuffer, feedback, shift2Amount * envelope);
    shift3.process(buffer, shiftBuffer, feedback, shift3Amount * envelope);

    // combine shift + synth buffer before further processing
    const float shiftMix = 0.02f + depth * 0.03f;
    for (int ch = 0; ch < numChannels; ++ch)
        synthBuffer.addFrom(ch, 0, shiftBuffer, ch, 0, numSamples, shiftMix);

    synthLowpass.setCutoffFrequency(toneValue);
    juce::dsp::AudioBlock<float> synthBlock(synthBuffer);
    juce::dsp::ProcessContextReplacing<float> synthContext(synthBlock);
    synthLowpass.process(synthContext);

    synthHighpass.setCutoffFrequency(500.0f - 480.0f * depth);
    synthHighpass.process(synthContext);

    synthPhaser.process(synthContext);

    juce::Reverb::Parameters lowRevParams;
    lowRevParams.roomSize = 0.3f + 0.3f * blur;
    lowRevParams.damping = 0.4f;
    lowRevParams.wetLevel = 0.05f + 0.25f * std::sqrt(blur);
    lowRevParams.dryLevel = 1.0f - 1.0f * std::sqrt(blur);
    lowRevParams.width = 0.5f;
    reverbLow.setParameters(lowRevParams);

    juce::Reverb::Parameters highRevParams;
    highRevParams.roomSize = 0.5f + 0.5f * blur;
    highRevParams.damping = 0.3f;
    highRevParams.wetLevel = 0.1f + 0.5f * std::sqrt(blur);
    highRevParams.dryLevel = 1.0f - 1.0f * std::sqrt(blur);
    highRevParams.width = 1.0f;
    reverbHigh.setParameters(highRevParams);

    juce::AudioBuffer<float> lowBand(numChannels, numSamples);
    juce::AudioBuffer<float> highBand(numChannels, numSamples);
    for (int ch = 0; ch < numChannels; ++ch)
    {
        lowBand.copyFrom(ch, 0, synthBuffer, ch, 0, numSamples);
        highBand.copyFrom(ch, 0, synthBuffer, ch, 0, numSamples);
    }

    juce::dsp::AudioBlock<float> lowBlock(lowBand);
    juce::dsp::AudioBlock<float> highBlock(highBand);
    crossoverLow.process(juce::dsp::ProcessContextReplacing<float>(lowBlock));
    crossoverHigh.process(juce::dsp::ProcessContextReplacing<float>(highBlock));

    if (numChannels >= 2)
    {
        reverbLow.processStereo(lowBand.getWritePointer(0), lowBand.getWritePointer(1), numSamples);
        reverbHigh.processStereo(highBand.getWritePointer(0), highBand.getWritePointer(1), numSamples);
    }
    else
    {
        reverbLow.processMono(lowBand.getWritePointer(0), numSamples);
        reverbHigh.processMono(highBand.getWritePointer(0), numSamples);
    }

    for (int ch = 0; ch < numChannels; ++ch)
    {
        float* dest = synthBuffer.getWritePointer(ch);
        const float* lo = lowBand.getReadPointer(ch);
        const float* hi = highBand.getReadPointer(ch);
        for (int i = 0; i < numSamples; ++i)
            dest[i] = lo[i] + hi[i];
    }

    juce::dsp::AudioBlock<float> eqBlock(synthBuffer);

    *synthPeakEQ.state = *juce::dsp::IIR::Coefficients<float>::makePeakFilter(currentSampleRate, 200.0f, 0.3f, juce::Decibels::decibelsToGain(4.0f * std::sqrt(blur)));
    synthPeakEQ.process(juce::dsp::ProcessContextReplacing<float>(eqBlock));

    for (int ch = 0; ch < numChannels; ++ch)
    {
        float* channelData = buffer.getWritePointer(ch);
        const float* synthData = synthBuffer.getReadPointer(ch);
        for (int i = 0; i < numSamples; ++i)
            channelData[i] = channelData[i] * (1.0f - blendMix) * dryCompensation + synthData[i] * blendMix * synthGainLinear;
    }

    for (int ch = numChannels; ch < buffer.getNumChannels(); ++ch)
        buffer.clear(ch, 0, numSamples);
}

void PluginProcessor::processChannel(int channel)
{
    for (int i = 0; i < fftSize; ++i)
    {
        int readIdx = (inputWritePos[channel] + i) % fftSize;
        fftWorkBuffer[i] = inputBuffer[channel][readIdx] * windowFunction[i];
        fftWorkBuffer[i + fftSize] = 0.0f;
    }

    fft.performRealOnlyForwardTransform(fftWorkBuffer.data(), true);

    const int numBins = fftSize / 2 + 1;
    for (int i = 0; i < numBins; ++i)
    {
        float real = fftWorkBuffer[i * 2];
        float imag = fftWorkBuffer[i * 2 + 1];
        magnitudes[i] = std::sqrt(real * real + imag * imag);
    }

    constexpr int numVoices = 16;

    const int numSalienceBins = fftSize / 2;
    const int minBin = static_cast<int>(10.0 * fftSize / currentSampleRate);
    const int maxBin = static_cast<int>(4400.0 * fftSize / currentSampleRate);
    constexpr int numHarmonics = 8;

    salience.fill(0.0f);
    for (int f0Bin = minBin; f0Bin < maxBin; ++f0Bin)
    {
        float score = 0.0f;
        for (int h = 1; h <= numHarmonics; ++h)
        {
            int harmonicBin = f0Bin * h;
            if (harmonicBin < numSalienceBins)
                score += magnitudes[harmonicBin] / static_cast<float>(h);
        }
        salience[f0Bin] = score;
    }

    for (int i = 0; i < numSalienceBins; ++i)
        accumulatedSalience[i] += salience[i];
    salienceFrameCount++;

    if (needsPitchUpdate && channel == 0)
    {
        std::array<float, fftSize / 2> avgSalience;
        float invCount = 1.0f / static_cast<float>(salienceFrameCount);
        for (int i = 0; i < numSalienceBins; ++i)
            avgSalience[i] = accumulatedSalience[i] * invCount;

        auto pitchPairs = detectPitches(numVoices, avgSalience);

        {
            juce::SpinLock::ScopedLockType lock(frequencyLock);
            detectedPitches.clear();
            for (auto& [bin, sal] : pitchPairs)
            {
                DetectedPitch dp;
                dp.frequency = bin * static_cast<float>(currentSampleRate) / fftSize;
                dp.magnitude = sal;
                detectedPitches.push_back(dp);
            }
        }

        accumulatedSalience.fill(0.0f);
        salienceFrameCount = 0;
        needsPitchUpdate = false;
    }
}

std::vector<std::pair<float, float>> PluginProcessor::detectPitches(int numVoices, const std::array<float, fftSize / 2>& salienceData)
{
    const int minBin = static_cast<int>(10.0 * fftSize / currentSampleRate);
    const int maxBin = static_cast<int>(4400.0 * fftSize / currentSampleRate);

    std::vector<int> peaks;
    const float absoluteThreshold = 0.01f;

    float maxSalience = 0.0f;
    for (int i = minBin; i < maxBin; ++i)
        maxSalience = std::max(maxSalience, salienceData[i]);

    constexpr float relativeThreshold = 0.15f;
    const float effectiveThreshold = std::max(absoluteThreshold, maxSalience * relativeThreshold);

    for (int i = minBin + 1; i < maxBin - 1; ++i)
    {
        if (salienceData[i] > salienceData[i - 1] && salienceData[i] > salienceData[i + 1] && salienceData[i] > effectiveThreshold)
            peaks.push_back(i);
    }

    std::sort(peaks.begin(), peaks.end(), [&salienceData](int a, int b) {
        return salienceData[a] > salienceData[b];
    });

    std::vector<std::pair<float, float>> result;
    for (int peak : peaks)
    {
        if (static_cast<int>(result.size()) >= numVoices)
            break;

        float interpolatedBin = static_cast<float>(peak);
        float alpha = salienceData[peak - 1];
        float beta  = salienceData[peak];
        float gamma = salienceData[peak + 1];
        float denom = alpha - 2.0f * beta + gamma;
        if (std::abs(denom) > 1e-10f)
            interpolatedBin += 0.5f * (alpha - gamma) / denom;

        bool reject = false;
        for (auto& existing : result)
        {
            float ratio = interpolatedBin / existing.first;
            if (ratio > 0.95f && ratio < 1.05f)
            {
                reject = true;
                break;
            }
        }

        if (!reject)
            result.push_back({ interpolatedBin, salienceData[peak] });
    }

    return result;
}

float PluginProcessor::applyClipping(const float sample) const {
    constexpr float maxThreshold = 0.2f;
    constexpr float minThreshold = 0.0f;
    
    // asymmetric
    const float upperThreshold = minThreshold + maxThreshold - maxThreshold * envelope;
    const float lowerThreshold = -maxThreshold;

    if (sample > upperThreshold) {
        return std::tanh(sample - upperThreshold) + upperThreshold;
    } else if (sample < lowerThreshold) {
        return std::tanh(sample + lowerThreshold) - lowerThreshold;
    } else {
        return sample;
    }
}

double PluginProcessor::getTailLengthSeconds() const {
    return 4.0;
}

juce::AudioProcessorEditor* PluginProcessor::createEditor()
{
    return new PluginEditor(*this);
}

void PluginProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = parameters.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void PluginProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml(getXmlFromBinary(data, sizeInBytes));
    if (xml != nullptr && xml->hasTagName(parameters.state.getType()))
        parameters.replaceState(juce::ValueTree::fromXml(*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PluginProcessor();
}

void PluginProcessor::getVizSteps(std::vector<PluginProcessor::VizStep>& steps) {
    juce::SpinLock::ScopedLockType lock(vizLock);
    std::copy(std::begin(vizSteps), std::end(vizSteps), std::back_inserter(steps));
    vizSteps.clear();
}