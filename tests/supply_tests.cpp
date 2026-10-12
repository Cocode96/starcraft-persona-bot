#include "SupplyPolicy.h"
#include <stdexcept>
#include <iostream>

int main()
{
    try
    {
        const auto require = [](bool value) { if (!value) throw std::runtime_error("Supply policy regression"); };
        require(SupplyPolicy::protossBuffer(true, 8, 2) == 4);
        const int buffer = SupplyPolicy::protossBuffer(false, 8, 2);
        require(SupplyPolicy::needsProvider(150, 164, 0, buffer));
        require(!SupplyPolicy::needsProvider(150, 164, 32, buffer));
        require(!SupplyPolicy::needsProvider(392, 384, 16, buffer));
        require(!SupplyPolicy::needsProvider(400, 400, 0, buffer));
        require(SupplyPolicy::protossBuffer(false, 50, 10) == 48);
        std::cout << "Supply lead time and pending capacity validated\n";
    }
    catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
