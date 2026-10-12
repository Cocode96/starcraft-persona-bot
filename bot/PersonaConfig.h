#pragma once

#include <istream>
#include <string>
#include <vector>
#include <random>

class PersonaConfig
{
public:
    struct ScriptStep
    {
        int frame;
        int kind; // 0 building, 1 technology, 2 upgrade
        int id;
        int count;
    };
    struct UnitWeight
    {
        int phase;
        int id;
        int weight;
    };
    // Numeric IDs/timings in the replay exporter use the BWAPI unit table.
    bool load(std::istream& input, std::string& error);
    int chooseUnit(int frame, const std::vector<int>& legalUnits, std::mt19937& random) const;

    std::string name = "Baseline Zerg";
    int schemaVersion = 1;
    std::string race = "Zerg";
    unsigned int seed = 0;
    int sampleGames = 0;
    int poolFrame = 1800;
    int hatcheryFrame = 3200;
    int extractorFrame = 5000;
    int hydraDenFrame = 7000;
    int attackUnits = 12;
    int workerTarget = 24;
    int firstAttackFrame = 12000;
    std::vector<ScriptStep> steps;
    std::vector<UnitWeight> weights;
};
