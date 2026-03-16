#include "Shapes.h"
#include <cmath>

int Shapes::DATA[4][size][size] = {0};
bool Shapes::initialized = false;

void Shapes::init() {
    if (initialized)
        return;
    float center = static_cast<float>(size) / 2.0f;
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const float yf = static_cast<float>(y),
                        xf = static_cast<float>(x);

            // circle
            const float radius = center;
            const float dist = std::sqrt((yf-center)*(yf-center) + (xf-center)*(xf-center));
            if (dist < radius)
                DATA[0][y][x] = 1;

            // tri
            if (x < center && y >= size-x-2)
                DATA[1][y][x] = 1;
            else if (x > center && y >= x-2)
                DATA[1][y][x] = 1;


            // saw
            if (y >= x)
                DATA[2][y][x] = 1;

            // square
            if (x <= 5 || x >= size-5 ||
                y <= 5 || y >= size-5)
                    DATA[3][y][x] = 1;
        }
    }


    initialized = true;
}
