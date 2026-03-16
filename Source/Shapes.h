#pragma once



class Shapes
{
    public:
        inline static int get(const int idx, const int y, const int x) {
            return DATA[idx][y][x];
        }
        static constexpr int size = 21;

        static void init();
    private:
        static bool initialized;
        static int DATA[4][size][size];

};
