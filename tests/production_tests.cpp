#include "ProductionPolicy.h"
#include <iostream>
#include <stdexcept>

int main()
{
    try
    {
        const auto require = [](bool value) { if (!value) throw std::runtime_error("Capacity policy regression"); };
        ProductionPolicy::Capacity s{18000, 0, 1800, 5, 5, 0, 5, 24, true};
        require(ProductionPolicy::addGateway(s));
        s.busy = 2; require(!ProductionPolicy::addGateway(s)); s.busy = 5;
        s.freeSupply = 0; require(!ProductionPolicy::addGateway(s)); s.freeSupply = 24;
        s.affordableUnit = false; require(!ProductionPolicy::addGateway(s)); s.affordableUnit = true;
        s.incomplete = 1; require(!ProductionPolicy::addGateway(s)); s.incomplete = 0;
        s.planned = 8; require(!ProductionPolicy::addGateway(s)); s.planned = 5;
        s.lastAddition = 17900; require(!ProductionPolicy::addGateway(s)); s.lastAddition = 0;
        s.frame = 13000; require(!ProductionPolicy::addGateway(s)); s.frame = 18000;
        s.completed = 12; s.busy = 12; require(!ProductionPolicy::addGateway(s));
        std::cout << "Capacity exhaustion distinguished from supply, resources and idle production\n";
    }
    catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
