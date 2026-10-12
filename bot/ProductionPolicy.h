#pragma once

// Bounded runtime capacity recovery; does not infer personal build preference.
class ProductionPolicy
{
public:
    struct Capacity
    {
        int frame = 0;
        int lastAddition = 0;
        int minerals = 0;
        int completed = 0;
        int busy = 0;
        int incomplete = 0;
        int planned = 0;
        int freeSupply = 0;
        bool affordableUnit = false;
    };

    static bool addGateway(const Capacity& s)
    {
        return s.frame >= 14400 && s.frame - s.lastAddition >= 1200 && s.minerals >= 1200 &&
            s.completed >= 2 && s.completed >= s.planned && s.completed < 12 &&
            s.incomplete == 0 && s.busy * 4 >= s.completed * 3 &&
            s.freeSupply >= 4 && s.affordableUnit;
    }
};
