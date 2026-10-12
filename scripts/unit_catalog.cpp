#include <BWAPI.h>
#include <iomanip>
#include <iostream>

int main()
{
    using namespace BWAPI;
    std::cout << "{\n";
    bool first = true;
    for (auto type : UnitTypes::allUnitTypes())
    {
        const auto race = type.getRace();
        if (race != Races::Zerg && race != Races::Terran && race != Races::Protoss) continue;
        if (type.isHero() || type.isSpecialBuilding() || type.whatBuilds().first == UnitTypes::None) continue;
        if (!first) std::cout << ",\n";
        first = false;
        std::cout << std::quoted(std::to_string(type.getID())) << ": {\"name\":" << std::quoted(type.getName())
                  << ",\"race\":" << std::quoted(race.getName())
                  << ",\"building\":" << (type.isBuilding() ? "true" : "false")
                  << ",\"worker\":" << (type.isWorker() ? "true" : "false")
                  << ",\"depot\":" << (type.isResourceDepot() ? "true" : "false")
                  << ",\"producer\":" << type.whatBuilds().first.getID() << '}';
    }
    std::cout << "\n}\n";
}
