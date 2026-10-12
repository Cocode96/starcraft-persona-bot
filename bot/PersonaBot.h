#pragma once

#include <BWAPI.h>
#include <vector>

#include "PersonaConfig.h"

class PersonaBot final : public BWAPI::AIModule
{
public:
    void onStart() override;
    void onFrame() override;
    void onEnd(bool isWinner) override;
    void onUnitShow(BWAPI::Unit unit) override;

private:
    struct BuildTask
    {
        int workerId = -1;
        int issuedFrame = 0;
    };

    int count(BWAPI::UnitType type) const;
    bool morph(BWAPI::UnitType type);
    bool build(BWAPI::UnitType type);
    BWAPI::TilePosition buildTile(BWAPI::UnitType type) const;
    void economy();
    void production();
    void army();
    BWAPI::Position attackTarget();

    PersonaConfig config_;
    BuildTask buildTask_;
    std::vector<BWAPI::Position> enemyBuildings_;
    std::vector<BWAPI::TilePosition> searchLocations_;
    std::size_t searchIndex_ = 0;
    bool active_ = false;
    bool attacking_ = false;
};
