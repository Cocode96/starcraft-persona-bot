#include "EconomyPolicy.h"
#include <iostream>
#include <stdexcept>

int main()
{
    try
    {
        const auto require = [](bool value) { if (!value) throw std::runtime_error("Gas balance regression"); };
        require(!EconomyPolicy::pauseGas(false, false, 0, 2000));
        require(!EconomyPolicy::pauseGas(false, true, 100, 1199));
        require(EconomyPolicy::pauseGas(false, true, 100, 1200));
        require(EconomyPolicy::pauseGas(true, true, 100, 1199));
        require(EconomyPolicy::pauseGas(true, true, 100, 501));
        require(!EconomyPolicy::pauseGas(true, true, 100, 500));
        require(!EconomyPolicy::pauseGas(false, true, 800, 1200));
        require(!EconomyPolicy::pauseGas(true, true, 1300, 1200));
        std::cout << "Opening preservation, gas reserve and hysteresis checked\n";
    }
    catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
