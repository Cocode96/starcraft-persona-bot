#include <BWAPI.h>
#include <Windows.h>
#include <iostream>

int main(int argc, char** argv)
{
    if (argc != 2) return 2;
    const auto library = LoadLibraryA(argv[1]);
    if (!library)
    {
        std::cerr << "DLL load failed: " << GetLastError() << '\n';
        return 1;
    }
    const auto initialize = reinterpret_cast<void (*)(BWAPI::Game*)>(GetProcAddress(library, "gameInit"));
    const auto create = reinterpret_cast<BWAPI::AIModule* (*)()>(GetProcAddress(library, "newAIModule"));
    if (!initialize || !create)
    {
        FreeLibrary(library);
        return 1;
    }
    initialize(nullptr);
    auto module = create();
    if (!module)
    {
        FreeLibrary(library);
        return 1;
    }
    delete module;
    FreeLibrary(library);
    std::cout << "32-bit DLL loaded; BWAPI factory created/destroyed the module\n";
}
