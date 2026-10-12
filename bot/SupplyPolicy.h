#pragma once
#include <algorithm>

// Execution buffer in BWAPI half-supply units, not a learned opening choice.
class SupplyPolicy
{
public:
    static int protossBuffer(bool recordedOpening, int productionBuildings, int nexuses)
    {
        return recordedOpening ? 4 : std::clamp(productionBuildings * 4 + nexuses * 2, 8, 48);
    }

    static bool needsProvider(int used, int total, int pending, int buffer)
    {
        return total + pending < 400 && total + pending - used <= buffer;
    }
};
