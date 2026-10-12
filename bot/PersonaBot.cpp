#include "PersonaBot.h"

#include <algorithm>
#include <fstream>
#include <limits>

using namespace BWAPI;

void PersonaBot::onStart()
{
    active_ = false;
    attacking_ = false;
    buildTask_ = {};
    enemyBuildings_.clear();
    searchLocations_.clear();
    searchIndex_ = 0;
    config_ = {};
    if (Broodwar->isReplay() || !Broodwar->self()) return;
    if (Broodwar->self()->getRace() != Races::Zerg)
    {
        Broodwar->printf("PersonaBot currently requires Zerg. Select Zerg before the game.");
        return;
    }
    std::ifstream input("bwapi-data/read/persona.ini");
    std::string error;
    if (!input || !config_.load(input, error))
        Broodwar->printf("Persona profile unavailable (%s). Using explicit baseline rules.",
                        input ? error.c_str() : "file missing");
    for (const auto tile : Broodwar->getStartLocations())
        if (tile != Broodwar->self()->getStartLocation()) searchLocations_.push_back(tile);
    // BWAPI's default visibility restrictions remain enabled.
    active_ = true;
    Broodwar->printf("PersonaBot: %s, replay samples=%d. Prototype Zerg bot.",
                    config_.name.c_str(), config_.sampleGames);
}

int PersonaBot::count(UnitType type) const
{
    int total = 0;
    for (auto unit : Broodwar->self()->getUnits())
    {
        if (unit->getType() == type) ++total;
        else if (unit->getType() == UnitTypes::Zerg_Egg && unit->getBuildType() == type)
            total += type.isTwoUnitsInOneEgg() ? 2 : 1;
    }
    return total;
}

bool PersonaBot::morph(UnitType type)
{
    for (auto larva : Broodwar->self()->getUnits())
        if (larva->getType() == UnitTypes::Zerg_Larva && Broodwar->canMake(type, larva))
            return larva->morph(type);
    return false;
}

TilePosition PersonaBot::buildTile(UnitType type) const
{
    const auto home = Broodwar->self()->getStartLocation();
    if (type == UnitTypes::Zerg_Extractor)
    {
        TilePosition best = TilePositions::None;
        double distance = std::numeric_limits<double>::max();
        for (auto geyser : Broodwar->getStaticGeysers())
        {
            const auto tile = geyser->getInitialTilePosition();
            const double d = tile.getDistance(home);
            if (d < 18 && d < distance && Broodwar->canBuildHere(tile, type))
            {
                best = tile;
                distance = d;
            }
        }
        return best;
    }
    if (type == UnitTypes::Zerg_Hatchery)
    {
        TilePosition best = TilePositions::None;
        double distance = std::numeric_limits<double>::max();
        for (auto mineral : Broodwar->getStaticMinerals())
        {
            const auto seed = mineral->getInitialTilePosition();
            // Prefer a nearby expansion; getBuildLocation only accepts explored terrain.
            if (seed.getDistance(home) < 12) continue;
            const auto tile = Broodwar->getBuildLocation(type, seed, 10);
            if (!tile.isValid()) continue;
            bool nearDepot = false;
            for (auto unit : Broodwar->self()->getUnits())
                if (unit->getType().isResourceDepot() && unit->getTilePosition().getDistance(tile) < 10)
                    nearDepot = true;
            const double d = tile.getDistance(home);
            if (!nearDepot && d < distance)
            {
                best = tile;
                distance = d;
            }
        }
        if (best.isValid()) return best;
        // If the natural is not explored yet, a macro hatchery keeps production moving.
    }
    return Broodwar->getBuildLocation(type, home, 24, type.requiresCreep());
}

bool PersonaBot::build(UnitType type)
{
    if (buildTask_.workerId >= 0 || !Broodwar->canMake(type)) return false;
    const auto tile = buildTile(type);
    if (!tile.isValid()) return false;
    Unit worker = nullptr; // BWAPI-owned non-owning handle.
    int bestDistance = std::numeric_limits<int>::max();
    for (auto candidate : Broodwar->self()->getUnits())
    {
        if (candidate->getType() != UnitTypes::Zerg_Drone || !candidate->isCompleted() ||
            candidate->isMorphing() || candidate->isConstructing() || candidate->isGatheringGas()) continue;
        const int distance = candidate->getDistance(Position(tile));
        if (distance < bestDistance && candidate->hasPath(Position(tile)))
        {
            worker = candidate;
            bestDistance = distance;
        }
    }
    if (!worker || !worker->build(type, tile)) return false;
    buildTask_ = {worker->getID(), Broodwar->getFrameCount()};
    return true;
}

void PersonaBot::economy()
{
    if (buildTask_.workerId >= 0)
    {
        auto worker = Broodwar->getUnit(buildTask_.workerId);
        if (!worker || !worker->exists() || worker->getType() != UnitTypes::Zerg_Drone ||
            Broodwar->getFrameCount() - buildTask_.issuedFrame > 600)
            buildTask_ = {};
    }
    // Assign at most one new gas worker per tick; count units already en route.
    for (auto refinery : Broodwar->self()->getUnits())
    {
        if (!refinery->getType().isRefinery() || !refinery->isCompleted() || refinery->getResources() <= 0) continue;
        int assigned = 0;
        for (auto worker : Broodwar->self()->getUnits())
            if (worker->getType().isWorker() &&
                (worker->getTarget() == refinery || worker->getOrderTarget() == refinery || worker->isGatheringGas()))
                ++assigned;
        if (assigned < 3)
        {
            for (auto worker : Broodwar->self()->getUnits())
                if (worker->getType() == UnitTypes::Zerg_Drone && worker->isCompleted() &&
                    worker->getID() != buildTask_.workerId && !worker->isConstructing() &&
                    !worker->isMorphing() && !worker->isGatheringGas() && !worker->isCarryingGas() &&
                    worker->getTarget() != refinery && worker->getOrderTarget() != refinery)
                    if (worker->gather(refinery)) break;
        }
    }
    for (auto worker : Broodwar->self()->getUnits())
    {
        if (worker->getType() != UnitTypes::Zerg_Drone || !worker->isIdle() ||
            worker->getID() == buildTask_.workerId) continue;
        if (worker->isCarryingMinerals() || worker->isCarryingGas())
        {
            worker->returnCargo();
            continue;
        }
        Unit target = nullptr;
        int distance = std::numeric_limits<int>::max();
        for (auto mineral : Broodwar->getMinerals())
        {
            if (!mineral->exists() || mineral->getResources() == 0) continue;
            bool supported = false;
            for (auto depot : Broodwar->self()->getUnits())
                if (depot->getType().isResourceDepot() && depot->isCompleted() &&
                    depot->getDistance(mineral) < 400) supported = true;
            const int d = worker->getDistance(mineral);
            if (supported && d < distance && worker->hasPath(mineral))
            {
                target = mineral;
                distance = d;
            }
        }
        if (target) worker->gather(target);
    }
}

void PersonaBot::production()
{
    const auto self = Broodwar->self();
    const int frame = Broodwar->getFrameCount();
    if (count(UnitTypes::Zerg_Drone) == 0)
    {
        morph(UnitTypes::Zerg_Drone);
        return;
    }
    int pendingSupply = 0;
    for (auto unit : self->getUnits())
        if (unit->getType() == UnitTypes::Zerg_Egg && unit->getBuildType() == UnitTypes::Zerg_Overlord)
            pendingSupply += 16; // BWAPI represents supply in half-units.
    if (self->supplyTotal() < 400 && self->supplyTotal() + pendingSupply - self->supplyUsed() <= 4)
    {
        morph(UnitTypes::Zerg_Overlord);
        return;
    }
    // Save for overdue structures instead of spending every mineral on units.
    if (frame >= config_.poolFrame && count(UnitTypes::Zerg_Spawning_Pool) == 0 &&
        buildTile(UnitTypes::Zerg_Spawning_Pool).isValid())
    {
        build(UnitTypes::Zerg_Spawning_Pool);
        return;
    }
    if (frame >= config_.hatcheryFrame && count(UnitTypes::Zerg_Hatchery) < 2 &&
        buildTile(UnitTypes::Zerg_Hatchery).isValid())
    {
        build(UnitTypes::Zerg_Hatchery);
        return;
    }
    if (frame >= config_.extractorFrame && count(UnitTypes::Zerg_Extractor) == 0)
    {
        // A mineral-only map must not stall the rest of production.
        if (buildTile(UnitTypes::Zerg_Extractor).isValid())
        {
            build(UnitTypes::Zerg_Extractor);
            return;
        }
    }
    if (frame >= config_.hydraDenFrame && self->gas() >= 50 &&
        self->completedUnitCount(UnitTypes::Zerg_Spawning_Pool) > 0 &&
        count(UnitTypes::Zerg_Hydralisk_Den) == 0 &&
        buildTile(UnitTypes::Zerg_Hydralisk_Den).isValid())
    {
        build(UnitTypes::Zerg_Hydralisk_Den);
        return;
    }
    if (count(UnitTypes::Zerg_Drone) < config_.workerTarget && morph(UnitTypes::Zerg_Drone)) return;
    if (self->completedUnitCount(UnitTypes::Zerg_Hydralisk_Den) > 0 && morph(UnitTypes::Zerg_Hydralisk)) return;
    morph(UnitTypes::Zerg_Zergling);
}

void PersonaBot::onUnitShow(Unit unit)
{
    if (!active_ || !unit->isVisible() || !unit->getPlayer()->isEnemy(Broodwar->self()) ||
        !unit->getType().isBuilding()) return;
    const auto position = unit->getPosition();
    if (std::find(enemyBuildings_.begin(), enemyBuildings_.end(), position) == enemyBuildings_.end())
        enemyBuildings_.push_back(position);
}

Position PersonaBot::attackTarget()
{
    // Remove stale sightings only after the location becomes visible again.
    enemyBuildings_.erase(std::remove_if(enemyBuildings_.begin(), enemyBuildings_.end(), [](Position position)
    {
        if (!Broodwar->isVisible(TilePosition(position))) return false;
        for (auto unit : Broodwar->getAllUnits())
            if (unit->isVisible() && unit->getPlayer()->isEnemy(Broodwar->self()) &&
                unit->getType().isBuilding() && unit->getDistance(position) < 160) return false;
        return true;
    }), enemyBuildings_.end());
    if (!enemyBuildings_.empty()) return enemyBuildings_.front();
    if (searchLocations_.empty()) return Position(Broodwar->self()->getStartLocation());
    if (Broodwar->isVisible(searchLocations_[searchIndex_]))
        searchIndex_ = (searchIndex_ + 1) % searchLocations_.size();
    return Position(searchLocations_[searchIndex_]) + Position(64, 48);
}

void PersonaBot::army()
{
    Unitset combat;
    for (auto unit : Broodwar->self()->getUnits())
        if (unit->isCompleted() && (unit->getType() == UnitTypes::Zerg_Zergling ||
                                   unit->getType() == UnitTypes::Zerg_Hydralisk)) combat.insert(unit);
    if (static_cast<int>(combat.size()) >= config_.attackUnits) attacking_ = true;
    if (combat.size() < 4) attacking_ = false;
    Position target = Positions::None;
    for (auto enemy : Broodwar->getAllUnits())
    {
        if (!enemy->isVisible() || !enemy->getPlayer()->isEnemy(Broodwar->self())) continue;
        for (auto base : Broodwar->self()->getUnits())
            if (base->getType().isResourceDepot() && enemy->getDistance(base) < 600)
                target = enemy->getPosition();
    }
    if (!target.isValid() && attacking_) target = attackTarget();
    if (!target.isValid()) return;
    for (auto unit : combat)
        if (!unit->isAttackFrame() && (unit->isIdle() ||
            Broodwar->getFrameCount() - unit->getLastCommandFrame() >= 48))
            unit->attack(target);
}

void PersonaBot::onFrame()
{
    if (!active_ || Broodwar->isPaused() || !Broodwar->self()) return;
    Broodwar->drawTextScreen(8, 8, "Persona: %s | samples %d | prototype", config_.name.c_str(), config_.sampleGames);
    if (Broodwar->getFrameCount() % std::max(6, Broodwar->getLatencyFrames()) != 0) return;
    economy();
    production();
    army();
}

void PersonaBot::onEnd(bool isWinner)
{
    if (!active_) return;
    std::ofstream log("bwapi-data/write/persona-results.txt", std::ios::app);
    log << config_.name << '\t' << (isWinner ? "win" : "loss") << '\t'
        << Broodwar->getFrameCount() << '\n';
}
