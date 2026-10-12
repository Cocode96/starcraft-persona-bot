#include "PersonaConfig.h"

#include <iostream>
#include <fstream>
#include <sstream>
#include <stdexcept>

const std::string valid =
    "schema_version=1\nname=Fixture\nsample_games=5\npool_frame=1800\n"
    "hatchery_frame=2900\nextractor_frame=3900\nhydra_den_frame=6500\n"
    "attack_units=12\nworker_target=24\n";

void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

int main(int argc, char** argv)
{
    try
    {
        if (argc == 2)
        {
            PersonaConfig actual;
            std::string error;
            std::ifstream file(argv[1]);
            require(static_cast<bool>(file), "exported profile file missing");
            if (!actual.load(file, error)) throw std::runtime_error(error);
            std::cout << "Validated exported profile: " << actual.name << " (" << actual.sampleGames << " games)\n";
            return 0;
        }
        PersonaConfig config;
        std::string error;
        std::istringstream good(valid);
        require(config.load(good, error), "valid Python export rejected");
        require(config.name == "Fixture" && config.hatcheryFrame == 2900, "configuration lost");
        for (const auto& bad : {std::string(""), valid + "worker_target=80\n",
                              valid + "unknown=2\n", std::string("schema_version=2\n"),
                              std::string("schema_version=1\nname=Partial\npool_frame=-1\n")})
        {
            std::istringstream input(bad);
            require(!config.load(input, error), "invalid profile accepted");
            require(config.name == "Fixture" && config.hatcheryFrame == 2900, "failed load mutated configuration");
            require(!error.empty(), "missing failure reason");
        }
        std::string oversized = valid;
        oversized.replace(oversized.find("worker_target=24"), 16, "worker_target=81");
        std::istringstream input(oversized);
        require(!config.load(input, error), "unsafe worker count accepted");
        const std::string imitation = "schema_version=2\nname=Test\nrace=Protoss\nsample_games=8\nseed=42\n"
            "worker_target=30\nfirst_attack_frame=6000\nstep=1000,0,156,1\nstep=2000,0,160,1\n"
            "weight=0,65,9000\nweight=0,66,1000\nweight=1,66,10000\nweight=2,65,10000\nweight=3,66,10000\n";
        std::istringstream current(imitation);
        require(config.load(current, error), "schema 2 rejected");
        std::mt19937 random(42);
        int zealots = 0;
        for (int i = 0; i < 10000; ++i) zealots += config.chooseUnit(0, {65, 66}, random) == 65;
        require(zealots > 8700 && zealots < 9300, "learned distribution not respected");
        require(config.chooseUnit(0, {66}, random) == 66, "illegal choice escaped mask");
        require(config.chooseUnit(4320, {65, 66}, random) == 66, "phase boundary mismatch");
        require(config.chooseUnit(0, {}, random) == -1, "empty action set not handled");
        for (const auto& suffix : {"weight=0,65,12\n", "step=1,0,156,1\n", "step=\n", "weight=0,999,1\n"})
        {
            std::istringstream invalid(imitation + suffix);
            require(!config.load(invalid, error), "invalid imitation profile accepted");
            require(config.race == "Protoss" && config.steps.size() == 2, "failed schema 2 load mutated state");
        }
        std::cout << "Profile config contract validated\n";
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
