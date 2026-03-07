#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <vector>

class PluginProcessor : public juce::AudioProcessor
{
public:
    struct DetectedPitch
    {
        float frequency = 0.0f;
        float magnitude = 0.0f;
    };

    struct VizStep {
        std::vector<DetectedPitch> tones;
        float mix;
        float blur;
        float envelope;
        float tone;
        float depth;
        int shape;
    };

    class Arpeggiator
    {
    public:
        Arpeggiator(int startIndex = 0) : index(startIndex), defaultIndex(startIndex) {}

        void setSampleRate(double sr) { sampleRate = sr; }

        void syncToHost(double ppqPosition, double stepsPerBeat, int numPitches, bool isPlaying)
        {
            int64_t currentStep;
            if (isPlaying)
            {
                currentStep = static_cast<int64_t>(std::floor(ppqPosition * stepsPerBeat));
                freeRunningStep = currentStep;
            }
            else
            {
                currentStep = freeRunningStep;
            }

            if (currentStep != lastStep)
            {
                int64_t diff = currentStep - lastStep;
                if (diff < 0 || diff > 16)
                    diff = 1;

                pendingNumPitches = numPitches;
                pendingSteps = diff;
                pendingPan = random.nextFloat() * 2.0f - 1.0f;
                lastStep = currentStep;

                if (fadeOutSamplesRemaining <= 0)
                {
                    fadeOutSamplesRemaining = static_cast<int>(sampleRate * releaseMs * 0.001);
                    fadeOutSamplesTotal = fadeOutSamplesRemaining;
                }
            }
        }

        bool consumeStepped()
        {
            bool s = stepped;
            stepped = false;
            return s;
        }

        float getPan() const { return currentPan; }

        void setAttackMs(double ms) { attackMs = ms; }

        void setDecayMs(double ms) { decayMs = ms; }

        void setSmoothing(float s) { smoothing = s; }

        void setTarget(const std::vector<DetectedPitch>& pitches, float magScale, int octave=0)
        {
            if (pitches.empty()) { targetFreq = 0.0; targetAmp = 0.0f; return; }
            int idx = juce::jlimit(0, static_cast<int>(pitches.size()) - 1, index);
            targetFreq = static_cast<double>(pitches[static_cast<size_t>(idx)].frequency);
            targetAmp = pitches[static_cast<size_t>(idx)].magnitude * magScale;
            if (octave != 0) {
                targetFreq *= std::pow(2.0f, static_cast<float>(octave));
            }

            if (smoothedFreq <= 0.0)
            {
                smoothedFreq = targetFreq;
                smoothedAmp = targetAmp;
            }
        }

        float generateAndAdvance(double invSampleRate, int waveType)
        {
            if (fadeOutSamplesRemaining > 0)
            {
                fadeOutSamplesRemaining--;
                if (fadeOutSamplesRemaining <= 0)
                    applyPendingStep();
            }

            double coeff = static_cast<double>(smoothing);
            smoothedFreq = smoothedFreq * coeff + targetFreq * (1.0 - coeff);
            smoothedAmp = smoothedAmp * smoothing + targetAmp * (1.0f - smoothing);

            float env = attack;
            if (attack >= 1.0f) {
                env = std::sqrt(decay);
            }
            if (fadeOutSamplesRemaining > 0 && fadeOutSamplesTotal > 0)
                env *= static_cast<float>(fadeOutSamplesRemaining) / static_cast<float>(fadeOutSamplesTotal);

            float sample = generateWave(phase, waveType) * smoothedAmp * env;
            attack = std::min(attack + static_cast<float>((1000.0 / attackMs) * invSampleRate), 1.0f);
            decay = std::max(decay - static_cast<float>((1000.0 / decayMs) * invSampleRate), 0.0f);
            phase += smoothedFreq * invSampleRate;
            phase -= std::floor(phase);
            return sample;
        }

        static float generateWave(double phase, int waveType)
        {
            constexpr double twoPi = juce::MathConstants<double>::twoPi;
            switch (waveType)
            {
                case 0: return static_cast<float>(std::sin(phase * twoPi));
                case 1: return static_cast<float>(2.0 * std::abs(2.0 * (phase - std::floor(phase + 0.5))) - 1.0);
                case 2: return static_cast<float>(2.0 * (phase - std::floor(phase + 0.5)));
                case 3: return (phase - std::floor(phase)) < 0.5 ? 1.0f : -1.0f;
                default: return 0.0f;
            }
        }

        int getIndex() const { return index; }

        void advanceFreeRunning(int numSamples, double sampleRate, double bpm, double stepsPerBeat)
        {
            double beatsPerSample = bpm / (60.0 * sampleRate);
            double stepsAdvanced = numSamples * beatsPerSample * stepsPerBeat;
            freeRunningStep += static_cast<int64_t>(std::floor(freeRunningAccum + stepsAdvanced));
            freeRunningAccum = std::fmod(freeRunningAccum + stepsAdvanced, 1.0);
        }

        DetectedPitch getPitch() const {
            DetectedPitch dp;
            dp.frequency = targetFreq;
            dp.magnitude = targetAmp;
            return dp;
        }

    private:
        void applyPendingStep()
        {
            for (int64_t s = 0; s < pendingSteps; ++s)
            {
                if (pendingNumPitches > 1)
                {
                    if (goingUp)
                    {
                        index++;
                        if (index >= pendingNumPitches - 1) { index = pendingNumPitches - 1; goingUp = false; }
                    }
                    else
                    {
                        index--;
                        if (index <= 0) { index = 0; goingUp = true; }
                    }
                }
                else
                {
                    index = defaultIndex;
                    goingUp = true;
                }
            }

            pendingSteps = 0;
            stepped = true;
            attack = 0.0f;
            decay = 1.0f;
            currentPan = pendingPan;
        }

        int index;
        int defaultIndex;
        bool goingUp = true;
        int64_t lastStep = -1;
        double phase = 0.0;
        bool stepped = false;
        float smoothing = 0.0f;
        double targetFreq = 0.0;
        float targetAmp = 0.0f;
        double smoothedFreq = 0.0;
        float smoothedAmp = 0.0f;
        float currentPan = 0.0f;
        int64_t freeRunningStep = 0;
        double freeRunningAccum = 0.0;
        double attackMs = 2.0;
        double decayMs = 50.0;
        float attack = 0.0f;
        float decay = 1.0f;
        double sampleRate = 44100.0;
        static constexpr double releaseMs = 2.0;
        int fadeOutSamplesRemaining = 0;
        int fadeOutSamplesTotal = 0;
        int64_t pendingSteps = 0;
        int pendingNumPitches = 0;
        float pendingPan = 0.0f;
        juce::Random random;
    };
public:
    PluginProcessor();
    ~PluginProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override;
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override;

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int index) override {}
    const juce::String getProgramName(int index) override { return {}; }
    void changeProgramName(int index, const juce::String& newName) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState& getParameters() { return parameters; }

    void getVizSteps(std::vector<VizStep>& steps); 

    static constexpr int fftOrder = 12;
    static constexpr int fftSize = 1 << fftOrder;
    static constexpr int hopSize = fftSize / 128;

private:
    void processChannel(int channel);
    std::vector<std::pair<float, float>> detectPitches(int numVoices, const std::array<float, fftSize / 2>& salienceData);
    float applyClipping(const float sample) const;

    juce::AudioProcessorValueTreeState parameters;
    std::atomic<float>* shapeParam = nullptr;
    std::atomic<float>* mixParam = nullptr;
    std::atomic<float>* toneParam = nullptr;
    std::atomic<float>* rateParam = nullptr;
    std::atomic<float>* driveParam = nullptr;
    std::atomic<float>* depthParam = nullptr;
    std::atomic<float>* blurParam = nullptr;

    juce::dsp::StateVariableTPTFilter<float> synthLowpass;
    juce::dsp::StateVariableTPTFilter<float> synthHighpass;
    juce::dsp::Phaser<float> synthPhaser;

    juce::dsp::StateVariableTPTFilter<float> crossoverLow;
    juce::dsp::StateVariableTPTFilter<float> crossoverHigh;
    juce::Reverb reverbLow;
    juce::Reverb reverbHigh;

    juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>, juce::dsp::IIR::Coefficients<float>> synthPeakEQ;

    juce::dsp::FFT fft;
    std::array<float, fftSize> windowFunction;

    static constexpr int maxChannels = 2;
    std::array<std::array<float, fftSize>, maxChannels> inputBuffer;
    std::array<int, maxChannels> inputWritePos;
    std::array<int, maxChannels> outputReadPos;
    std::array<int, maxChannels> samplesUntilNextHop;

    std::array<float, fftSize * 2> fftWorkBuffer;
    std::array<float, fftSize> magnitudes;
    std::array<float, fftSize / 2> salience;
    std::array<float, fftSize / 2> accumulatedSalience;
    int salienceFrameCount = 0;
    bool needsPitchUpdate = true;

    double currentSampleRate = 44100.0;

    Arpeggiator arp1{0};
    Arpeggiator arp2{2};

    float envelope = 0.0f;
    double rmsAccumulator = 0.0;
    int rmsSampleCount = 0;

    std::vector<DetectedPitch> detectedPitches;
    juce::SpinLock frequencyLock;

    std::deque<VizStep> vizSteps;
    juce::SpinLock vizLock;

public:
    float getEnvelope() const { return envelope; }

    std::vector<float> getDetectedFrequencies() const
    {
        juce::SpinLock::ScopedLockType lock(frequencyLock);
        std::vector<float> freqs;
        for (auto& p : detectedPitches)
            freqs.push_back(p.frequency);
        return freqs;
    }

    std::vector<DetectedPitch> getDetectedPitches() const
    {
        juce::SpinLock::ScopedLockType lock(frequencyLock);
        return detectedPitches;
    }

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginProcessor)
};
