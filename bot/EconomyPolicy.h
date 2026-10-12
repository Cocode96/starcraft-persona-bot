#pragma once

// Shared execution guard, not a learned preference for taking or skipping gas.
class EconomyPolicy
{
public:
    static bool pauseGas(bool paused, bool openingComplete, int minerals, int gas)
    {
        if (!openingComplete) return false;
        if (paused) return gas > 500 && minerals < gas;
        return gas >= 1200 && gas > 2 * minerals;
    }
};
