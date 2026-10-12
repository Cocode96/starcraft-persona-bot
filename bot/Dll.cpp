#include "PersonaBot.h"

// Required BWAPI module ABI. BWAPI owns the returned module's lifetime.
extern "C" __declspec(dllexport) void gameInit(BWAPI::Game* game)
{
    BWAPI::BroodwarPtr = game;
}

extern "C" __declspec(dllexport) BWAPI::AIModule* newAIModule()
{
    return new PersonaBot();
}
