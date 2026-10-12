#pragma once

#include <BWAPI.h>
#include <vector>
#include <map>

#include "PersonaConfig.h"
#include "TacticalPolicy.h"

class PersonaBot final : public BWAPI::AIModule
{
public:
    void onStart() override;
    void onFrame() override;
    void onEnd(bool isWinner) override;
    void onUnitShow(BWAPI::Unit unit) override;
    void onUnitCreate(BWAPI::Unit unit) override;
    void onUnitMorph(BWAPI::Unit unit) override;
    void onUnitDestroy(BWAPI::Unit unit) override;

private:
    struct BuildTask
    {
        int workerId = -1;
        int issuedFrame = 0;
        int typeId = -1;
        int expectedCount = 0;
        BWAPI::TilePosition tile = BWAPI::TilePositions::None;
        int lastDistance = 0;
        int progressFrame = 0;
    };
    struct Placement
    {
        int typeId;
        int ordinal;
        BWAPI::TilePosition tile;
    };

    int count(BWAPI::UnitType type) const;
    bool morph(BWAPI::UnitType type);
    bool build(BWAPI::UnitType type);
    BWAPI::TilePosition buildTile(BWAPI::UnitType type) const;
    void economy();
    void planMap();
    void rally();
    BWAPI::Position assemblyPoint() const;
    void production();
    void imitationProduction();
    bool openingProduction();
    bool make(BWAPI::UnitType type);
    bool requirementsMet(BWAPI::UnitType type) const;
    bool prepareRequirements(BWAPI::UnitType type, int depth = 0);
    void scout();
    bool validateProfile() const;
    void recordStructure(BWAPI::Unit unit);
    void army();
    BWAPI::Position attackTarget();

    PersonaConfig config_;
    BuildTask buildTask_;
    std::vector<BWAPI::Position> enemyBuildings_;
    std::vector<BWAPI::TilePosition> searchLocations_;
    std::size_t searchIndex_ = 0;
    bool active_ = false;
    bool attacking_ = false;
    std::mt19937 random_;
    std::vector<bool> finishedSteps_;
    int scoutWorkerId_ = -1;
    std::size_t scoutIndex_ = 0;
    std::map<int, int> structureCounts_;
    std::map<int, int> knownStructureTypes_;
    std::map<int, int> productionCounts_;
    std::map<int, int> gasAssignments_;
    std::map<int, int> mineralAssignments_;
    std::size_t openingIndex_ = 0;
    std::vector<BWAPI::TilePosition> baseSites_;
    std::vector<Placement> placements_;
    std::map<std::pair<int, int>, int> failedBuildSites_;
    int lastMacroHatchFrame_ = -1200;
    int testMaxFrames_ = 0;
    int lastEnemyBaseSeenFrame_ = -10000;
    int retreatUntil_ = 0;
    int lastStormFrame_ = -1000;
    BWAPI::Position lastStormPosition_ = BWAPI::Positions::None;
    TacticalPolicy::Stance stance_ = TacticalPolicy::Stance::Gather;
};
