#pragma once

#include "PluginProcessor.h"
#include <juce_graphics/juce_graphics.h>
#include <vector>
#include <random>


// needs to be odd
constexpr int shapeSize = 7;
const int SHAPES[][shapeSize][shapeSize] = {
{   // sine

    {0,0,1,1,1,0,0},
    {0,1,1,1,1,1,0},
    {1,1,1,1,1,1,1},
    {1,1,1,1,1,1,1},
    {1,1,1,1,1,1,1},
    {0,1,1,1,1,1,0},
    {0,0,1,1,1,0,0},

},
{   // tri

    {0,0,0,1,0,0,0},
    {0,0,1,1,1,0,0},
    {0,1,1,1,1,1,0},
    {1,1,1,1,1,1,1},
    {1,1,1,1,1,1,1},
    {1,1,1,1,1,1,1},
    {0,0,0,0,0,0,0},

},
{   // saw

    {1,1,1,1,1,1,1},
    {0,1,1,1,1,1,1},
    {0,0,1,1,1,1,1},
    {0,0,0,1,1,1,1},
    {0,0,0,0,1,1,1},
    {0,0,0,0,0,1,1},
    {0,0,0,0,0,0,1},

},
{   // square

    {1,1,1,1,1,1,1},
    {1,1,0,0,0,1,1},
    {1,1,0,0,0,1,1},
    {1,1,0,0,0,1,1},
    {1,1,0,0,0,1,1},
    {1,1,1,1,1,1,1},
    {1,1,1,1,1,1,1},
},
};

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

class Particle
{
    public:
        Particle(juce::PixelARGB col, const int shapeId, float bright, float decay):
            color(col),
            shape(shapeId),
            brightness(bright),
            decayFac(decay)
        {}

        inline bool visible(const int width, const int height) const {
            return pos[0] >= 0.0f && pos[0] < static_cast<float>(width) && 
                   pos[1] >= 0.0f && pos[1] < static_cast<float>(height);
        }

        juce::PixelARGB color;
        int shape;
        float brightness;
        float decayFac;

        //                 x,    y
        float pos[2] = {0.0f, 0.0f};
        float vel[2] = {0.0f, 0.0f};
        
        static constexpr int maxTail = 3;
        int tailPos = 0;
        int tailLength = 0;
        float tail[maxTail][2] = {{0}};

        static constexpr int pixelate = 3;

        inline void step(const float timestep) {
            for (size_t i = 0; i < 2; ++i) {
                pos[i] += vel[i] * timestep;
            }
            vel[1] -= 0.05f;
            brightness *= decayFac * timestep;
        }

        inline int quantize(int a) {
            return (a/pixelate) * pixelate;
        }

        inline void draw(juce::Image::BitmapData& bmp, const float xJitter, const float yJitter) {
            // add tail ringBuffer with jitter
            tail[tailPos][0] = pos[0] + xJitter;
            tail[tailPos][1] = pos[1] + yJitter;
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
                constexpr int shapeOffset = shapeSize/2;
                const int startX = quantize(centerX - shapeOffset), 
                          startY = quantize(centerY - shapeOffset);
                auto& shapeData = SHAPES[shape];

                // paint over the quantized canvas area in chunks with stride=pixelate
                for (int y = startY; y < startY + shapeSize+pixelate-1; y += pixelate) {
                    for (int x = startX; x < startX+shapeSize; x += pixelate) {

                        // within each chunk, count matching pixels within our shape
                        int sum = 0;
                        for (int y_ = y; y_ < y+pixelate; ++y_) {
                            // current global position = x_, y_
                            // current local position is global position - center + shapeOffset + shift
                            int localY = y_ - centerY + shapeOffset;
                            if (localY < 0)
                                continue;
                            else if (localY >= shapeSize)
                                break;
            
                            for (int x_ = x; x_ < x+pixelate; ++x_) {
                                int localX = x_ - centerX + shapeOffset;
                                if (localX < 0)
                                    continue;
                                else if (localX >= shapeSize)
                                    break;
                                sum += shapeData[localY][localX];
                            }
                        }

                        if (sum * 2 < pixelate*pixelate) {
                            // only color if half or more pixels are active
                            continue;
                        }

                        const int alpha = 0x100;
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
                                
                                row[x_].blend(color, alpha);
                            }
                        }
                    }
                }
                

                /*
                auto& shapeData = SHAPES[shape];

                constexpr int shapeOffset = shapeSize/2;
                for (int shapeY = -shapeOffset; shapeY <= shapeOffset; ++shapeY) {
                    int y = centerY + shapeY;
                    if (y < 0 || y >= bmp.height)
                        continue;

                    auto* row = reinterpret_cast<juce::PixelARGB*>(bmp.getLinePointer(y));
                    for (int shapeX = -shapeOffset; shapeX <= shapeOffset; ++shapeX) {
                        int x = centerX + shapeX;
                        if (x < 0 || x >= bmp.width)
                            continue;

                        // tail particles linear decay
                        int alpha = (0x100 * (maxTail-i)) / maxTail;

                        // apply fadeIn
                        // if (tailLength == 1)
                        //     alpha /= 2;

                        if (shapeData[quantize(shapeY+shapeOffset)][quantize(shapeX+shapeOffset)]) {
                            // row[x].set(color);
                            row[x].blend(color, alpha);
                            // uint8_t alpha = static_cast<uint8_t>(255.0f * particle->brightness); 
                            // row[x].set(juce::PixelARGB(0xff, 0xff, 0xff, 0xff));
                        }
                    }
                }*/
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
    static constexpr float speed = 1.0f;
    const int frameRate;
    const float timestep;

    static constexpr int width = 680;
    static constexpr int height = 300;
    static constexpr int pixelate = 2;
    juce::Image imageBuffer;

    std::mt19937 eng;
    std::uniform_real_distribution<float> distr;
    inline float nextRandom(const float min = 0.0f, const float max = 1.0f) {
        return distr(eng) * (max-min) + min;
    }

    static constexpr float particleJitter = 4.0f;

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
    
    static constexpr size_t maxRecentPitches = 20;
    std::deque<float> recentPitches;
    inline void addRecentPitch(const float frequency) {
        recentPitches.push_back(frequencyToPitch(frequency));
        while (recentPitches.size() > maxRecentPitches) {
            recentPitches.pop_front();
        }
    }

};
