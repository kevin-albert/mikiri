#pragma once

#include "Shapes.h"
#include "PluginProcessor.h"
#include <juce_graphics/juce_graphics.h>
#include <vector>
#include <random>


constexpr int numColors = 7;
const juce::PixelARGB COLORS[numColors] = {
    juce::PixelARGB(0xff, 0xff, 0x00, 0x00), // red
    juce::PixelARGB(0xff, 0xff, 0xf0, 0x00), // orange
    juce::PixelARGB(0xff, 0xff, 0xff, 0x00), // yellow
    juce::PixelARGB(0xff, 0x00, 0xff, 0x00), // green
    juce::PixelARGB(0xff, 0x00, 0xff, 0xff), // turquoise
    juce::PixelARGB(0xff, 0x00, 0x00, 0xff), // blue
    juce::PixelARGB(0xff, 0xff, 0x00, 0xff)  // violet
};

class Random {
    public:
        static inline float next(const float min = 0.0f, const float max = 1.0f) {
            return distr(eng) * (max-min) + min;
        }
    private:
        static std::mt19937 eng;
        static std::uniform_real_distribution<float> distr;
};

class Particle
{
    public:
        Particle(juce::PixelARGB col, 
                 const int shapeId,
                 const float stepsPerSecond,
                 const float oscillationAmount,
                 const float shimmer,
                 const int tailLength):
            color(col),
            shape(shapeId),
            stepRate(stepsPerSecond),
            oscillation(oscillationAmount),
            shimmerAmount(shimmer),
            maxTail(tailLength)
        {
        }

        inline bool visible(const int width, const int height) const {
            return pos[0] >= 0.0f && pos[0] < static_cast<float>(width) && 
                   pos[1] >= 0.0f && pos[1] < static_cast<float>(height);
        }

        juce::PixelARGB color;
        int shape;
        float stepRate;
        float stepPeriod = 0.0f;
        float yOffset;

        float shimmerAmount;

        //                 x,    y
        float pos[2] = {0.0f, 0.0f};
        float vel[2] = {0.0f, 0.0f};
        static constexpr float drag = 0.03f;
        static constexpr float gravity = 17.0f;
        static constexpr float jitter = 0.0f;
        
        float oscillation = 0.0f;
        int maxTail;
        int tailPos = 0;
        int tailLength = 0;
        float tail[20][2];

        static constexpr int pixelate = 4;

        inline void step(const float timestep) {
            for (size_t i = 0; i < 2; ++i) {
                pos[i] += vel[i] * timestep;
            }

            // compute yOffset based on waveform & arp rate
            if (shape == 1) // triangle - double rate
                stepPeriod += 2.0f * stepRate * timestep;
            else
                stepPeriod += stepRate * timestep;

            while (stepPeriod > 4.0f)
                stepPeriod -= 4.0f;

            switch (shape) {
                case 0: // sine
                    yOffset = std::tanh(std::sin(stepPeriod * juce::MathConstants<float>::halfPi));
                    break;
                case 1: // triangle
                    if (stepPeriod < 1.0f)
                        yOffset = stepPeriod;
                    else if (stepPeriod < 3.0f)
                        yOffset = 2.0f - stepPeriod;
                    else
                        yOffset = stepPeriod - 4.0f;
                    break;
                case 2: // saw
                    if (stepPeriod < 2.0f)
                        yOffset = 1.0f - stepPeriod;
                    else
                        yOffset = 3.0f - stepPeriod;
                    break;
                case 3: // square
                    if (stepPeriod < 2.0f)
                        yOffset = 1.0f;
                    else
                        yOffset = -1.0f;
                    break;
                default:
                    yOffset = 0.0f;
            }

            // apply additional physics outside of the mouth only
            if (pos[0] > 220.0f) {
                vel[0] *= (1.0f-drag);
                vel[1] += gravity;
            }
        }

        inline int quantize(int a) {
            return (a/pixelate) * pixelate;
        }

        inline void draw(juce::Image::BitmapData& bmp) {

            const float spd = std::sqrt(vel[0]*vel[0] + vel[1]+vel[1]);
            float dir[] = {vel[0]/spd, vel[1]/spd};

            // add tail ringBuffer with oscillation
            // oscillate along the tangent (+/- normal)
            tail[tailPos][0] = pos[0] + 15.0f * dir[1] * oscillation * -yOffset;
            tail[tailPos][1] = pos[1] + 15.0f * dir[0] * oscillation * yOffset;
            if (tailLength < maxTail)
                ++tailLength;
            tailPos = (tailPos+1) % maxTail;

            // draw all recent frames including this one
            int tailIdx = tailPos;
            for (int i = 0; i < tailLength; ++i) {
                if (--tailIdx < 0)
                    tailIdx = maxTail-1;
                auto& p = tail[tailIdx];

                // center in real pixels
                const int centerX = static_cast<int>(p[0]);
                const int centerY = static_cast<int>(p[1]);

                // paint over the quantized canvas area
                constexpr int shapeOffset = Shapes::size/2;
                const int startX = quantize(centerX - shapeOffset), 
                          startY = quantize(centerY - shapeOffset);

                const bool shimmer = shimmerAmount > 0.02f && Random::next() < shimmerAmount;

                // paint over the quantized canvas area in chunks with stride=pixelate
                for (int y = startY; y < startY + Shapes::size+pixelate-1; y += pixelate) {
                    for (int x = startX; x < startX+Shapes::size; x += pixelate) {

                        // within each chunk, count matching pixels within our shape
                        int sum = 0;
                        for (int y_ = y; y_ < y+pixelate; ++y_) {
                            // current global position = x_, y_
                            // current local position is global position - center + shapeOffset + shift
                            int localY = y_ - centerY + shapeOffset;
                            if (localY < 0)
                                continue;
                            else if (localY >= Shapes::size)
                                break;
            
                            for (int x_ = x; x_ < x+pixelate; ++x_) {
                                int localX = x_ - centerX + shapeOffset;
                                if (localX < 0)
                                    continue;
                                else if (localX >= Shapes::size)
                                    break;
                                sum += Shapes::get(0, localY, localX);
                            }
                        }

                        if (sum * 2 < pixelate*pixelate) {
                            // only color if half or more pixels are active
                            continue;
                        }

                        const float alpha = static_cast<float>(maxTail-i)/static_cast<float>(maxTail);
                        
                        const bool pixelBlink = shimmer && Random::next() > 0.8f;
                        int alphaInt = pixelBlink ? 256 : static_cast<int>(256.0f * alpha);
                        if ((pos[0] > 650 && pos[1] > 225) ||
                            (pos[0] > 505 && pos[1] > 260))
                                alphaInt /= 8;
                        const juce::PixelARGB pixelColor = pixelBlink ? juce::PixelARGB(0xff, 0xff, 0xff, 0xff) : color; 

                        // apply the color
                        for (int y_ = y; y_ < y+pixelate; ++y_) {
                            if (y_ < 0)
                                continue;
                            if (y_ >= bmp.height)
                                break;
                            auto* row = reinterpret_cast<juce::PixelARGB*>(bmp.getLinePointer(y_));
                            for (int x_ = x; x_ < x+pixelate; ++x_) {
                                if (x_ < 0)
                                    continue;
                                if (x_ >= bmp.width)
                                    break;

                                row[x_].blend(pixelColor, alphaInt);
                            }
                        }
                    }
                }
            }
        }

    private:
        // nope
};

class Visualizer
{
public:
    Visualizer(const int rate);
    void step(const std::vector<PluginProcessor::VizStep>& steps);
    juce::Image getBuffer() { return imageBuffer; }
    float dbg;

private:
    static constexpr float speed = 0.7f;
    const int frameRate;
    const float timestep;

    static constexpr int width = 680;
    static constexpr int height = 300;
    juce::Image imageBuffer;

    std::deque<float> recentFrequencies;

    static constexpr size_t maxParticles = 500;
    std::deque<Particle*> particles;
    inline void addParticle(Particle* particle) {
        particles.push_back(particle);
        while (particles.size() > maxParticles) {
            delete particles.front();
            particles.pop_front();
        }
    }

    inline float frequencyToPitch(float frequency) {
        return std::log2(frequency);
    }

    static constexpr size_t maxRecentPitches = 40;
    std::deque<float> recentPitches;
    inline void addRecentPitch(const float frequency) {
        recentPitches.push_back(frequencyToPitch(frequency));
        while (recentPitches.size() > maxRecentPitches) {
            recentPitches.pop_front();
        }
    }

};
