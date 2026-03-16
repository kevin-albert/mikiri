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
                 const float bright,
                 float decay,
                 const float shimmer,
                 const int tailLength):
            color(col),
            shape(shapeId),
            brightness(bright),
            decayFac(decay),
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
        float brightness;
        float decayFac;
        float shimmerAmount;

        //                 x,    y
        float pos[2] = {0.0f, 0.0f};
        float vel[2] = {0.0f, 0.0f};
        static constexpr float jitter = 2.0f;
        
        int maxTail;
        int tailPos = 0;
        int tailLength = 0;
        float tail[20][2];

        static constexpr int pixelate = 4;

        inline void step(const float timestep) {
            for (size_t i = 0; i < 2; ++i) {
                pos[i] += vel[i] * timestep;
            }
            vel[1] += 2.0f;
            // brightness *= decayFac * timestep;
        }

        inline int quantize(int a) {
            return (a/pixelate) * pixelate;
        }

        inline void draw(juce::Image::BitmapData& bmp) {
            // add tail ringBuffer with jitter
            tail[tailPos][0] = pos[0] + Random::next(-jitter, jitter);
            tail[tailPos][1] = pos[1] + Random::next(-jitter, jitter);
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
                                sum += Shapes::get(shape, localY, localX);
                            }
                        }

                        if (sum * 2 < pixelate*pixelate) {
                            // only color if half or more pixels are active
                            continue;
                        }

                        const float alpha = brightness * static_cast<float>(maxTail-i)/static_cast<float>(maxTail);
                        
                        const bool pixelBlink = shimmer && Random::next() > 0.8f;
                        const int alphaInt = pixelBlink ? 256 : static_cast<int>(256.0f * alpha);
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

                                // if (shimmer && Random::next() > 0.9f) {
                                //     row[x_].blend(juce::PixelARGB(0xff, 0xff, 0xff, 0xff), alphaInt);
                                // } else {
                                //     row[x_].blend(color, alphaInt);
                                // }
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
    int dbg;

private:
    static constexpr float speed = 5.0f;
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
