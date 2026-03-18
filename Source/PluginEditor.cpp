#include "PluginEditor.h"
#include "BinaryData.h"
#include "Visualizer.h"

PluginEditor::PluginEditor(PluginProcessor& processor)
    : AudioProcessorEditor(&processor),
      processorRef(processor),
      viz(refreshRate)
{
    setLookAndFeel(&lookAndFeel);

    shapeSlider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    shapeSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    addAndMakeVisible(shapeSlider);

    shapeLabel.setText("shape", juce::dontSendNotification);
    shapeLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(shapeLabel);

    shapeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processorRef.getParameters(), "shape", shapeSlider);

    mixSlider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    mixSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    addAndMakeVisible(mixSlider);

    mixLabel.setText("mix", juce::dontSendNotification);
    mixLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(mixLabel);

    mixAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processorRef.getParameters(), "mix", mixSlider);

    toneSlider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    toneSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    addAndMakeVisible(toneSlider);

    toneLabel.setText("tone", juce::dontSendNotification);
    toneLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(toneLabel);

    toneAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processorRef.getParameters(), "tone", toneSlider);

    rateSlider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    rateSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    addAndMakeVisible(rateSlider);

    rateLabel.setText("", juce::dontSendNotification);
    rateLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(rateLabel);

    rateAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processorRef.getParameters(), "rate", rateSlider);

    driveSlider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    driveSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    addAndMakeVisible(driveSlider);

    driveLabel.setText("input", juce::dontSendNotification);
    driveLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(driveLabel);

    driveAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processorRef.getParameters(), "input", driveSlider);

    depthSlider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    depthSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    addAndMakeVisible(depthSlider);

    depthLabel.setText("depth", juce::dontSendNotification);
    depthLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(depthLabel);

    depthAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processorRef.getParameters(), "depth", depthSlider);

    blurSlider.setSliderStyle(juce::Slider::RotaryVerticalDrag);
    blurSlider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    addAndMakeVisible(blurSlider);

    blurLabel.setText("blur", juce::dontSendNotification);
    blurLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(blurLabel);

    blurAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processorRef.getParameters(), "blur", blurSlider);

    bgImage = juce::ImageCache::getFromMemory(BinaryData::uibg_png, BinaryData::uibg_pngSize);

    rateImages[0] = juce::ImageCache::getFromMemory(BinaryData::_16th_png, BinaryData::_16th_pngSize);
    rateImages[1] = juce::ImageCache::getFromMemory(BinaryData::_16thtriplet_png, BinaryData::_16thtriplet_pngSize);
    rateImages[2] = juce::ImageCache::getFromMemory(BinaryData::dotted32nd_png, BinaryData::dotted32nd_pngSize);
    rateImages[3] = juce::ImageCache::getFromMemory(BinaryData::_32nd_png, BinaryData::_32nd_pngSize);
    rateImages[4] = juce::ImageCache::getFromMemory(BinaryData::_32ndtriplet_png, BinaryData::_32ndtriplet_pngSize);
    rateImages[5] = juce::ImageCache::getFromMemory(BinaryData::dotted64th_png, BinaryData::dotted64th_pngSize);
    rateImages[6] = juce::ImageCache::getFromMemory(BinaryData::_64th_png, BinaryData::_64th_pngSize);

    fgOverlay.setImage(juce::ImageCache::getFromMemory(BinaryData::uifg_png, BinaryData::uifg_pngSize));
    fgOverlay.setInterceptsMouseClicks(false, false);
    addAndMakeVisible(fgOverlay);

    debugLabel.setFont(juce::Font("Courier New", 12.0f, juce::Font::plain));
    debugLabel.setColour(juce::Label::textColourId, juce::Colours::white);
    debugLabel.setJustificationType(juce::Justification::bottomLeft);
    // addAndMakeVisible(debugLabel);

    setSize(680, 300);
    startTimerHz(refreshRate);
}

PluginEditor::~PluginEditor()
{
    setLookAndFeel(nullptr);
    stopTimer();
}

void PluginEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colours::black);
    if (bgImage.isValid())
        g.drawImage(bgImage, getLocalBounds().toFloat());

    if (currentRateImage.isValid())
    {
        float opacity = 0.5f + 0.5f * std::sin(rateImagePhase);
        auto rateBounds = rateSlider.getBounds();
        int imgSize = rateBounds.getWidth();
        auto centre = rateBounds.getCentre();
        g.setOpacity(opacity);
        g.drawImage(currentRateImage,
                    juce::Rectangle<float>(
                        static_cast<float>(centre.getX()) - imgSize * 0.5f,
                        static_cast<float>(centre.getY()) - imgSize * 0.5f,
                        static_cast<float>(imgSize),
                        static_cast<float>(imgSize)));
        g.setOpacity(1.0f);
    }

    auto vizBuffer = viz.getBuffer();
    if (vizBuffer.isValid())
        g.drawImageAt(vizBuffer, 0, 0);
}

void PluginEditor::resized()
{
    constexpr int knobSize = 30;
    constexpr int rateKnobSize = 76;
    constexpr int gap = 20;
    constexpr int numSmall = 6;
    constexpr int totalWidth = knobSize * numSmall + rateKnobSize + gap * 6;
    constexpr int offsetX = 260;
    const int outerWidth = getWidth() - offsetX;
    int startX = offsetX + (outerWidth - totalWidth) / 2;
    constexpr int topY = 20;
    int x = startX;

    auto placeKnob = [&](juce::Label& label, juce::Slider& slider, int size)
    {
        int centreX = x + size / 2;
        label.setBounds(centreX - 40, topY + size, 80, 15);
        slider.setBounds(x, topY, size, size);
        x += size + gap;
    };

    placeKnob(driveLabel, driveSlider, knobSize);
    placeKnob(shapeLabel, shapeSlider, knobSize);
    placeKnob(depthLabel, depthSlider, knobSize);
    placeKnob(toneLabel, toneSlider, knobSize);
    placeKnob(blurLabel, blurSlider, knobSize);
    placeKnob(mixLabel, mixSlider, knobSize);
    placeKnob(rateLabel, rateSlider, rateKnobSize);

    fgOverlay.setBounds(getLocalBounds());
    fgOverlay.toFront(false);
    debugLabel.setBounds(5, getHeight() - 20, 120, 20);
    debugLabel.toFront(false);
}

void PluginEditor::timerCallback()
{
    int rateIndex = static_cast<int>(processorRef.getParameters().getParameterAsValue("rate").getValue());
    if (rateIndex != lastRateIndex)
    {
        lastRateIndex = rateIndex;
        if (rateIndex >= 0 && rateIndex < static_cast<int>(rateImages.size()))
            currentRateImage = rateImages[static_cast<size_t>(rateIndex)];
    }

    float stepsPerSecond = processorRef.getStepsPerSecond();
    constexpr float twoPi = juce::MathConstants<float>::twoPi;
    rateImagePhase += twoPi * stepsPerSecond / static_cast<float>(refreshRate) / 2.0f;
    rateImagePhase = std::fmod(rateImagePhase, twoPi);

    processorRef.getVizSteps(steps);
    viz.step(steps);
    debugLabel.setText(juce::String(viz.dbg), juce::dontSendNotification);
    steps.clear();
    repaint();
}
