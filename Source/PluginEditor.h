#pragma once

#include "PluginProcessor.h"
#include "Visualizer.h"

class MikiriLookAndFeel : public juce::LookAndFeel_V4
{
public:
    MikiriLookAndFeel()
    {
        setColour(juce::Slider::textBoxTextColourId, juce::Colours::white);
        setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour(juce::Label::textColourId, juce::Colours::white);
    }

    juce::Font getLabelFont(juce::Label&) override
    {
        return juce::Font("Courier New", 10.0f, juce::Font::plain);
    }

    void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                          float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                          juce::Slider&) override
    {
        auto centreX = static_cast<float>(x) + static_cast<float>(width) * 0.5f;
        auto centreY = static_cast<float>(y) + static_cast<float>(height) * 0.5f;
        auto knobSize = static_cast<float>(std::min(width, height)) - 2.0f;
        auto radius = knobSize * 0.5f;

        g.setColour(juce::Colours::white);
        g.drawEllipse(centreX - radius, centreY - radius, knobSize, knobSize, 1.0f);

        auto angle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);
        constexpr float dotRadius = 2.5f;
        auto dotDist = radius * 0.7f;
        auto dotX = centreX + dotDist * std::cos(angle - juce::MathConstants<float>::halfPi);
        auto dotY = centreY + dotDist * std::sin(angle - juce::MathConstants<float>::halfPi);

        g.fillEllipse(dotX - dotRadius, dotY - dotRadius, dotRadius * 2.0f, dotRadius * 2.0f);
    }
};

class ForegroundOverlay : public juce::Component
{
public:
    void setImage(const juce::Image& img) { image = img; }
    void paint(juce::Graphics& g) override
    {
        if (image.isValid())
            g.drawImage(image, getLocalBounds().toFloat());
    }
private:
    juce::Image image;
};

class PluginEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit PluginEditor(PluginProcessor& processor);
    ~PluginEditor() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void timerCallback() override;


private:
    PluginProcessor& processorRef;
    MikiriLookAndFeel lookAndFeel;

    juce::Slider shapeSlider;
    juce::Label shapeLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> shapeAttachment;

    juce::Slider mixSlider;
    juce::Label mixLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> mixAttachment;

    juce::Slider toneSlider;
    juce::Label toneLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> toneAttachment;

    juce::Slider rateSlider;
    juce::Label rateLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> rateAttachment;

    juce::Slider driveSlider;
    juce::Label driveLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> driveAttachment;

    juce::Slider depthSlider;
    juce::Label depthLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> depthAttachment;

    juce::Slider blurSlider;
    juce::Label blurLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> blurAttachment;

    juce::Image bgImage;
    juce::Image vizBuffer;
    ForegroundOverlay fgOverlay;

    static constexpr int refreshRate = 20;

    std::vector<PluginProcessor::VizStep> steps;
    Visualizer viz;
    juce::Label debugLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginEditor)
};
