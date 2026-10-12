#include "PersonaConfig.h"
#include <BWAPI.h>
#include <fstream>
#include <iostream>

int main(int argc, char** argv)
{
    using namespace BWAPI;
    if (argc != 2) return 2;
    PersonaConfig config;
    std::string error;
    std::ifstream file(argv[1]);
    if (!file || !config.load(file, error)) { std::cerr << error; return 1; }
    auto steps = config.steps;
    steps.insert(steps.end(), config.opening.begin(), config.opening.end());
    for (const auto& step : steps)
    {
        bool valid = false;
        if (step.kind == 0 || step.kind == 3)
        {
            const UnitType type(step.id);
            valid = type.getID() == step.id && type.isBuilding() == (step.kind == 0) && type.getRace().getName() == config.race;
        }
        else if (step.kind == 1)
        {
            const TechType type(step.id);
            valid = type.getID() == step.id && type.getRace().getName() == config.race;
        }
        else
        {
            const UpgradeType type(step.id);
            valid = type.getID() == step.id && type.getRace().getName() == config.race;
        }
        if (!valid) { std::cerr << "Invalid step " << step.kind << ',' << step.id; return 1; }
    }
    std::vector<int> legal;
    for (const auto& weight : config.weights)
    {
        const UnitType type(weight.id);
        if (type.getID() != weight.id || type.isBuilding() || type.isHero() || type.getRace().getName() != config.race)
        { std::cerr << "Invalid unit " << weight.id; return 1; }
        legal.push_back(weight.id);
    }
    std::mt19937 random(config.seed);
    for (int frame : {0, 4320, 8640, 14400})
        if (config.schemaVersion >= 2 && config.chooseUnit(frame, legal, random) < 0) return 1;
    std::cout << config.name << '/' << config.race << ": " << config.steps.size() << " steps, "
              << config.weights.size() << " weights validated against BWAPI\n";
}
