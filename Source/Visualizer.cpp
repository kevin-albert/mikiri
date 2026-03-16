#include "Visualizer.h"
#include <algorithm>

std::mt19937 Random::eng;
std::uniform_real_distribution<float> Random::distr(0.0f, 1.0f);

Visualizer::Visualizer(const int rate):
    frameRate(rate),
    timestep(speed / static_cast<float>(frameRate)),
    imageBuffer(juce::Image::ARGB, 680, 300, true)
{
    Shapes::init();
}

void Visualizer::step(const std::vector<PluginProcessor::VizStep>& steps)
{

    // update particles
    for (Particle* particle: particles) {
        particle->step(timestep);
    }

    // enqueue new notes to recentPitches
    for (const auto& step: steps) {
        for (const auto& tone: step.tones) {
            addRecentPitch(tone.frequency);
        }
    }

    dbg = 0.0f;

    if (!recentPitches.empty()) {
        const float minPitch = *(std::min_element(recentPitches.begin(), recentPitches.end()));
        const float maxPitch = *(std::max_element(recentPitches.begin(), recentPitches.end()));
        const float pitchDelta = maxPitch - minPitch;

        for (const auto& step: steps) {
            for (const auto& tone: step.tones) {
                // only add particle if above a certain loudness
                if (tone.magnitude < 0.001)
                    continue;

                float yFactor;
                if (pitchDelta > 0.1f) {
                    float pitch = frequencyToPitch(tone.frequency);
                    yFactor = 1.0f - (pitch - minPitch) / pitchDelta;
                    yFactor += Random::next(-0.1, 0.1);
                } else {
                    yFactor = Random::next();
                }

                int colorId = static_cast<int>(static_cast<float>(numColors) * yFactor);
                if (colorId < 0) colorId = 0;
                else if (colorId >= numColors) colorId = numColors-1;

                constexpr float yMin = 150.0f + 20.0f;
                constexpr float yMax = 240.0f + 5.0f;
                float y = yMin + yFactor * (yMax-yMin);
                float x = 150.0f;
                const float shimmerAmount = 0.3f * step.tone / 16000.0f;

                const int tailLength = static_cast<int>(4.0f + 6.0f * step.blur + 4.0f * step.envelope);

                Particle* p = new Particle(COLORS[colorId], step.shape, step.stepsPerSecond, step.mix, shimmerAmount, tailLength);
                p->pos[0] = x;
                p->pos[1] = y;

                const float velocityAdjust = std::max(0.0f, 2.0f * step.envelope - 1.0f);

                // higher up particles start with higher velocity
                p->vel[0] = 180.0f + (1.0f - yFactor) * 80.0f + 800.0f * velocityAdjust;
                p->vel[1] = -60.0f - 40.0f * velocityAdjust;
                addParticle(p);

                dbg = step.stepsPerSecond;
            }
        }
    }

    // clear canvsas
    juce::Image::BitmapData bmp(imageBuffer, juce::Image::BitmapData::readWrite);
    for (int y = 0; y < bmp.height; ++y)
    {
        auto* row = reinterpret_cast<juce::PixelARGB*>(bmp.getLinePointer(y));
        for (int x = 0; x < bmp.width; ++x)
            row[x].set(juce::PixelARGB(0x00, 0x00, 0x00, 0x00));
    }

    // draw particles
    for (Particle* particle: particles) {
        particle->draw(bmp);
    }
}
