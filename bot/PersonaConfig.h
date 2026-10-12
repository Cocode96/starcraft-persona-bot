#pragma once

#include <istream>
#include <string>

class PersonaConfig
{
public:
    // Numeric IDs/timings in the replay exporter use the BWAPI unit table.
    bool load(std::istream& input, std::string& error);

    std::string name = "Baseline Zerg";
    int sampleGames = 0;
    int poolFrame = 1800;
    int hatcheryFrame = 3200;
    int extractorFrame = 5000;
    int hydraDenFrame = 7000;
    int attackUnits = 12;
    int workerTarget = 24;
};
