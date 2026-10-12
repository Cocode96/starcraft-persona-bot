#include "PersonaBot.h"
#include "SupplyPolicy.h"
#include "ProductionPolicy.h"
#include "EconomyPolicy.h"

#include <algorithm>
#include <fstream>
#include <limits>
#include <cstdlib>
#include <sstream>

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
    scoutWorkerId_ = -1;
    scoutIndex_ = 0;
    structureCounts_.clear();
    knownStructureTypes_.clear();
    productionCounts_.clear();
    gasAssignments_.clear();
    mineralAssignments_.clear();
    gasPaused_ = false;
    openingIndex_ = 0;
    baseSites_.clear();
    placements_.clear();
    failedBuildSites_.clear();
    lastMacroHatchFrame_ = -1200;
    lastMacroGatewayFrame_ = -1200;
    testMaxFrames_ = 0;
    lastEnemyBaseSeenFrame_ = -10000;
    retreatUntil_ = 0;
    lastStormFrame_ = -1000;
    lastStormPosition_ = Positions::None;
    stance_ = TacticalPolicy::Stance::Gather;
    std::ofstream("bwapi-data/write/game-seed.txt") << Broodwar->getRandomSeed() << '\n';
    if (Broodwar->isReplay())
    {
        Broodwar->setLocalSpeed(42);
        if (std::getenv("PERSONA_REPLAY_ANALYSIS"))
        {
            Broodwar->setLocalSpeed(0);
            Broodwar->setFrameSkip(24);
            Broodwar->setGUI(false);
            if (const auto limit = std::getenv("PERSONA_TEST_MAX_FRAMES")) testMaxFrames_ = std::atoi(limit);
        }
        for (auto unit : Broodwar->getAllUnits())
            if (unit->getType().isResourceDepot())
            {
                Broodwar->setScreenPosition(unit->getPosition() - Position(320, 180));
                break;
            }
        return;
    }
    if (!Broodwar->self()) return;
    std::ifstream input("bwapi-data/read/persona.ini");
    std::string error;
    if (!input || !config_.load(input, error))
        Broodwar->printf("Persona profile unavailable (%s). Using explicit baseline rules.",
                        input ? error.c_str() : "file missing");
    // Random/unknown opponents use the pooled profile; do not inspect hidden units.
    if (config_.schemaVersion >= 2 && Broodwar->enemy())
    {
        const auto opponent = Broodwar->enemy()->getRace().getName();
        if (opponent == "Zerg" || opponent == "Terran" || opponent == "Protoss")
        {
            std::ifstream matchup("bwapi-data/read/persona-" + opponent + ".ini");
            PersonaConfig candidate;
            if (matchup && candidate.load(matchup, error) && candidate.schemaVersion == config_.schemaVersion &&
                candidate.name == config_.name && candidate.race == config_.race)
                config_ = candidate;
        }
    }
    if (Broodwar->self()->getRace().getName() != config_.race || !validateProfile())
    {
        Broodwar->printf("Persona profile/race mismatch. Select %s and check profile files.", config_.race.c_str());
        return;
    }
    random_.seed(config_.seed);
    finishedSteps_.assign(config_.steps.size(), false);
    for (const auto tile : Broodwar->getStartLocations())
        if (tile != Broodwar->self()->getStartLocation()) searchLocations_.push_back(tile);
    // BWAPI's default visibility restrictions remain enabled.
    active_ = true;
    if (const auto limit = std::getenv("PERSONA_TEST_MAX_FRAMES")) testMaxFrames_ = std::atoi(limit);
    if (std::getenv("PERSONA_FAST_TEST"))
    {
        Broodwar->setLocalSpeed(0);
        Broodwar->setFrameSkip(24);
        Broodwar->setGUI(false);
    }
    std::ofstream("bwapi-data/write/live-status.tsv") << "frame\tworkers\tarmy\tbuildings\tminerals\tgas\tbuilder\tbuild_type\tsupply_used\tsupply_total\n";
    for (auto unit : Broodwar->self()->getUnits())
    {
        recordStructure(unit);
        if (!unit->getType().isBuilding()) ++productionCounts_[unit->getType().getID()];
    }
    planMap();
    Broodwar->printf("PersonaBot: %s, replay samples=%d. Empirical prototype.",
                    config_.name.c_str(), config_.sampleGames);
}

int PersonaBot::count(UnitType type) const
{
    int total = 0;
    for (auto unit : Broodwar->self()->getUnits())
    {
        if (unit->getType() == type || (type == UnitTypes::Zerg_Hatchery && unit->getType().isResourceDepot())) ++total;
        else if (unit->isMorphing() && unit->getBuildType() == type)
            total += type.isTwoUnitsInOneEgg() ? 2 : 1;
        for (const auto queued : unit->getTrainingQueue()) if (queued == type) ++total;
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

void PersonaBot::planMap()
{
    const auto home = Broodwar->self()->getStartLocation();
    std::vector<Position> remaining;
    for (auto mineral : Broodwar->getStaticMinerals())
        if (mineral->getInitialResources() >= 500) remaining.push_back(mineral->getInitialPosition());
    while (!remaining.empty())
    {
        std::vector<Position> cluster{remaining.back()};
        remaining.pop_back();
        for (std::size_t i = 0; i < cluster.size(); ++i)
            for (auto entry = remaining.begin(); entry != remaining.end();)
                if (entry->getDistance(cluster[i]) < 256)
                { cluster.push_back(*entry); entry = remaining.erase(entry); }
                else ++entry;
        if (cluster.size() < 4) continue;
        Position center(0, 0);
        for (const auto point : cluster) center += point;
        center /= static_cast<int>(cluster.size());
        // Exclude the entire occupied home mineral line, not only tiles near
        // the nexus/hatchery: otherwise its far edge becomes a fake expansion.
        if (center.getDistance(Position(home) + Position(64, 48)) < 400) continue;
        const TilePosition seed(center);
        TilePosition best = TilePositions::None;
        double bestScore = std::numeric_limits<double>::max();
        for (int x = seed.x - 10; x <= seed.x + 10; ++x)
            for (int y = seed.y - 10; y <= seed.y + 10; ++y)
            {
                const TilePosition tile(x, y);
                const Position position = Position(tile) + Position(64, 48);
                if (!tile.isValid() || tile.getDistance(home) < 12 ||
                    !Broodwar->canBuildHere(tile, Broodwar->self()->getRace().getResourceDepot()) ||
                    !Broodwar->hasPath(Position(home) + Position(64, 48), position)) continue;
                double score = 0;
                for (const auto mineral : cluster) score += position.getDistance(mineral);
                for (auto geyser : Broodwar->getStaticGeysers())
                    if (geyser->getInitialPosition().getDistance(center) < 400)
                        score += 2 * position.getDistance(geyser->getInitialPosition());
                if (score < bestScore) { best = tile; bestScore = score; }
            }
        if (best.isValid()) baseSites_.push_back(best);
    }
    std::sort(baseSites_.begin(), baseSites_.end(), [home](TilePosition a, TilePosition b)
    { return a.getDistance(home) < b.getDistance(home); });
    // Optional observed layout for the SAME starting corner. No rotations of
    // an asymmetric wall and no presumed transfer to other map families.
    const auto map = Broodwar->mapFileName();
    if (map.find("FightingSpirit") != std::string::npos || map.find("Fighting Spirit") != std::string::npos)
    {
        std::ifstream input("bwapi-data/read/placements.tsv");
        std::string line;
        while (std::getline(input, line))
        {
            std::istringstream row(line);
            int hx, hy, typeId, ordinal, x, y;
            if (!(row >> hx >> hy >> typeId >> ordinal >> x >> y)) continue;
            if (std::abs(hx - (home.x * 32 + 64)) > 32 || std::abs(hy - (home.y * 32 + 48)) > 32) continue;
            const UnitType type(typeId);
            bool terrainOK = type.isBuilding() && type.getRace() == Broodwar->self()->getRace() && ordinal > 0;
            for (int dx = 0; dx < type.tileWidth(); ++dx)
                for (int dy = 0; dy < type.tileHeight(); ++dy)
                    if (!Broodwar->isBuildable(x + dx, y + dy)) terrainOK = false;
            if (terrainOK) placements_.push_back({typeId, ordinal, TilePosition(x, y)});
        }
    }
    std::ofstream log("bwapi-data/write/map-plan.tsv");
    log << "home\t" << home.x << '\t' << home.y << '\n';
    for (const auto tile : baseSites_) log << "base\t" << tile.x << '\t' << tile.y << '\n';
    for (const auto& placement : placements_)
        log << "placement\t" << placement.typeId << '\t' << placement.ordinal << '\t'
            << placement.tile.x << '\t' << placement.tile.y << '\n';
}

Position PersonaBot::assemblyPoint() const
{
    Position anchor = Position(Broodwar->self()->getStartLocation()) + Position(64, 48);
    if (!baseSites_.empty()) anchor = Position(baseSites_.front()) + Position(64, 48);
    // Stand on the home side of the recorded front pylon/cannons.
    for (const auto& placement : placements_)
        if (placement.typeId == UnitTypes::Protoss_Pylon.getID() && placement.ordinal == 1)
            anchor = Position(placement.tile) + Position(32, 32);
    const auto home = Position(Broodwar->self()->getStartLocation()) + Position(64, 48);
    const int distance = std::max(1, anchor.getApproxDistance(home));
    Position target = anchor + (home - anchor) * std::min(96, distance) / distance;
    if (Broodwar->isWalkable(WalkPosition(target))) return target;
    return home;
}

void PersonaBot::rally()
{
    if (Broodwar->getFrameCount() % 96 != 0) return;
    const auto target = assemblyPoint();
    for (auto building : Broodwar->self()->getUnits())
    {
        if (!building->isCompleted() || !building->getType().isBuilding() || !building->getType().canProduce()) continue;
        bool accepted = false;
        if (building->getType().isResourceDepot() && building->getType().getRace() != Races::Zerg)
        {
            Unit mineral = nullptr;
            for (auto candidate : Broodwar->getMinerals())
                if (candidate->exists() && candidate->getResources() > 0 && building->getDistance(candidate) < 400 &&
                    (!mineral || building->getDistance(candidate) < building->getDistance(mineral))) mineral = candidate;
            if (mineral && building->getRallyUnit() != mineral) accepted = building->setRallyPoint(mineral);
        }
        else if (building->getRallyPosition().getApproxDistance(target) > 32)
            accepted = building->setRallyPoint(target);
        if (accepted) std::ofstream("bwapi-data/write/rally-orders.tsv", std::ios::app)
            << Broodwar->getFrameCount() << '\t' << building->getID() << '\t' << building->getType().getID() << '\n';
    }
}

TilePosition PersonaBot::buildTile(UnitType type) const
{
    auto home = Broodwar->self()->getStartLocation();
    const auto blocked = [this, type](TilePosition tile)
    {
        const auto entry = failedBuildSites_.find({type.getID(), tile.y * Broodwar->mapWidth() + tile.x});
        return entry != failedBuildSites_.end() && entry->second > Broodwar->getFrameCount();
    };
    const auto found = structureCounts_.find(type.getID());
    const int ordinal = (found == structureCounts_.end() ? 0 : found->second) + 1;
    for (const auto& placement : placements_)
        if (placement.typeId == type.getID() && placement.ordinal == ordinal && !blocked(placement.tile))
            return placement.tile;
    // Repeatedly trying nearby tiles inside a crowded main can leave supply
    // construction unreachable. After a failed later pylon, use a less crowded base.
    if (type == UnitTypes::Protoss_Pylon && count(type) >= 3)
    {
        bool failed = false;
        for (const auto& entry : failedBuildSites_)
            if (entry.first.first == type.getID() && entry.second > Broodwar->getFrameCount()) failed = true;
        if (failed)
        {
            int leastBuildings = std::numeric_limits<int>::max();
            for (auto base : Broodwar->self()->getUnits())
                if (base->getType().isResourceDepot() && base->isCompleted())
                {
                    int buildings = 0;
                    for (auto unit : Broodwar->self()->getUnits())
                        if (unit->getType().isBuilding() && unit->getDistance(base) < 512) ++buildings;
                    if (buildings < leastBuildings)
                    { leastBuildings = buildings; home = base->getTilePosition(); }
                }
        }
    }
    if (type.isRefinery())
    {
        TilePosition best = TilePositions::None;
        double distance = std::numeric_limits<double>::max();
        for (auto geyser : Broodwar->getStaticGeysers())
        {
            const auto tile = geyser->getInitialTilePosition();
            const double d = tile.getDistance(home);
            bool nearBase = false;
            for (auto base : Broodwar->self()->getUnits())
                if (base->getType().isResourceDepot() && base->isCompleted() &&
                    tile.getDistance(base->getTilePosition()) < 12) nearBase = true;
            if (nearBase && d < distance && Broodwar->canBuildHere(tile, type))
            {
                best = tile;
                distance = d;
            }
        }
        return best;
    }
    if (type.isResourceDepot())
    {
        // Hatchery count is production capacity, not a request for one new
        // remote mineral line per hatchery. Keep macro hatcheries supported.
        if (type == UnitTypes::Zerg_Hatchery && count(type) >= 3)
        {
            for (auto base : Broodwar->self()->getUnits())
                if (base->getType().isResourceDepot() && base->isCompleted())
                {
                    const auto tile = Broodwar->getBuildLocation(type, base->getTilePosition(), 14, true);
                    if (tile.isValid() && !blocked(tile) && Broodwar->canBuildHere(tile, type)) return tile;
                }
        }
        for (const auto tile : baseSites_)
        {
            bool nearDepot = false;
            for (auto unit : Broodwar->self()->getUnits())
                if (unit->getType().isResourceDepot() && unit->getTilePosition().getDistance(tile) < 10)
                    nearDepot = true;
            bool unsafe = false;
            for (const auto enemy : enemyBuildings_)
                if (Position(tile).getDistance(enemy) < 700) unsafe = true;
            if (!nearDepot && !unsafe && !blocked(tile) && Broodwar->canBuildHere(tile, type)) return tile;
        }
        // A production hatchery is allowed once the natural already exists.
        if (type != UnitTypes::Zerg_Hatchery || count(type) < 2) return TilePositions::None;
    }
    const auto suggested = Broodwar->getBuildLocation(type, home, 24, type.requiresCreep());
    if (suggested.isValid() && !blocked(suggested) && Broodwar->canBuildHere(suggested, type)) return suggested;
    TilePosition best = TilePositions::None;
    double score = std::numeric_limits<double>::max();
    for (int x = home.x - 18; x <= home.x + 18; ++x)
        for (int y = home.y - 18; y <= home.y + 18; ++y)
        {
            const TilePosition tile(x, y);
            if (!tile.isValid() || blocked(tile) || !Broodwar->canBuildHere(tile, type)) continue;
            const double d = tile.getDistance(home);
            if (d < score) { best = tile; score = d; }
        }
    return best;
}

bool PersonaBot::build(UnitType type)
{
    if (!Broodwar->canMake(type)) return false;
    const auto producer = type.whatBuilds().first;
    if (type.isAddon() || (type.getRace() == Races::Zerg && producer.isBuilding()))
    {
        for (auto unit : Broodwar->self()->getUnits())
            if (unit->getType() == producer && unit->isCompleted() && unit->isIdle() && Broodwar->canMake(type, unit))
                return type.isAddon() ? unit->buildAddon(type) : unit->morph(type);
        return false;
    }
    if (buildTask_.workerId >= 0) return false;
    const auto tile = buildTile(type);
    if (!tile.isValid()) return false;
    for (auto enemy : Broodwar->getAllUnits())
        if (enemy->isVisible() && enemy->getPlayer()->isEnemy(Broodwar->self()) &&
            !enemy->getType().isWorker() &&
            (enemy->getType().groundWeapon() != WeaponTypes::None || enemy->getType() == UnitTypes::Protoss_High_Templar) &&
            enemy->getDistance(Position(tile) + Position(64, 48)) < 224)
            return false; // Do not send successive builders into a visible killing zone.
    Unit worker = nullptr; // BWAPI-owned non-owning handle.
    int bestDistance = std::numeric_limits<int>::max();
    for (auto candidate : Broodwar->self()->getUnits())
    {
        if (candidate->getType() != producer || !candidate->isCompleted() || candidate->getID() == scoutWorkerId_ ||
            candidate->isMorphing() || candidate->isConstructing() || candidate->isGatheringGas()) continue;
        const int distance = candidate->getDistance(Position(tile));
        if (distance < bestDistance && candidate->hasPath(Position(tile)))
        {
            worker = candidate;
            bestDistance = distance;
        }
    }
    const int expected = count(type) + 1;
    if (!worker) return false;
    // Claim one builder before travelling. A failed fog/range build order must
    // not recruit a different worker on the next frame.
    const bool accepted = worker->build(type, tile);
    if (!accepted && !worker->move(Position(tile) + Position(64, 48))) return false;
    buildTask_ = {worker->getID(), Broodwar->getFrameCount(), type.getID(), expected, tile};
    buildTask_.lastDistance = worker->getDistance(Position(tile) + Position(64, 48));
    buildTask_.progressFrame = Broodwar->getFrameCount();
    gasAssignments_.erase(worker->getID());
    mineralAssignments_.erase(worker->getID());
    std::ofstream("bwapi-data/write/build-orders.tsv", std::ios::app)
        << Broodwar->getFrameCount() << '\t' << worker->getID() << '\t' << type.getID()
        << '\t' << tile.x << '\t' << tile.y << '\t' << (accepted ? "build" : "travel") << '\n';
    return true;
}

void PersonaBot::economy()
{
    if (buildTask_.workerId >= 0)
    {
        auto worker = Broodwar->getUnit(buildTask_.workerId);
        bool structureStarted = false;
        for (auto unit : Broodwar->self()->getUnits())
            if (unit->getType() == UnitType(buildTask_.typeId) && unit->getTilePosition() == buildTask_.tile)
                structureStarted = true;
        // count() includes planned/morphing types and must not release a walking builder.
        if (worker && worker->exists())
        {
            const int distance = worker->getDistance(Position(buildTask_.tile) + Position(64, 48));
            if (distance + 16 < buildTask_.lastDistance)
            { buildTask_.lastDistance = distance; buildTask_.progressFrame = Broodwar->getFrameCount(); }
        }
        const bool expired = !structureStarted &&
            (Broodwar->getFrameCount() - buildTask_.issuedFrame > 1200 ||
             Broodwar->getFrameCount() - buildTask_.progressFrame > 480);
        if (!worker || !worker->exists() || !worker->getType().isWorker() ||
            (worker->getType().getRace() != Races::Terran && structureStarted) ||
            (worker->getType().getRace() == Races::Terran && structureStarted && !worker->isConstructing()) || expired)
        {
            if (expired)
            {
                failedBuildSites_[{buildTask_.typeId, buildTask_.tile.y * Broodwar->mapWidth() + buildTask_.tile.x}] =
                    Broodwar->getFrameCount() + 2400;
                std::ofstream("bwapi-data/write/build-abandoned.tsv", std::ios::app)
                    << Broodwar->getFrameCount() << '\t' << buildTask_.typeId << '\t'
                    << buildTask_.tile.x << '\t' << buildTask_.tile.y << '\n';
                if (worker && worker->exists() && worker->getType().isWorker()) worker->stop();
            }
            buildTask_ = {};
        }
        else if (!structureStarted && Broodwar->getFrameCount() - worker->getLastCommandFrame() >= 48)
        {
            const UnitType type(buildTask_.typeId);
            if (!worker->build(type, buildTask_.tile))
            {
                const auto error = Broodwar->getLastError().getName();
                std::ofstream("bwapi-data/write/build-retries.tsv", std::ios::app)
                    << Broodwar->getFrameCount() << '\t' << type.getID() << '\t'
                    << error << '\n';
                if (worker->getDistance(Position(buildTask_.tile) + Position(64, 48)) > 160)
                    worker->move(Position(buildTask_.tile) + Position(64, 48));
            }
        }
    }
    for (auto entry = gasAssignments_.begin(); entry != gasAssignments_.end();)
    {
        auto worker = Broodwar->getUnit(entry->first);
        auto gas = Broodwar->getUnit(entry->second);
        if (!worker || !worker->exists() || !worker->getType().isWorker() || !gas || !gas->exists() ||
            !gas->isCompleted() || gas->getResources() <= 0 || entry->first == scoutWorkerId_ || entry->first == buildTask_.workerId)
            entry = gasAssignments_.erase(entry);
        else ++entry;
    }
    const auto self = Broodwar->self();
    const int frame = Broodwar->getFrameCount();
    const bool balanceGas = self->getRace() == Races::Zerg && config_.schemaVersion >= 2 &&
        frame > config_.openingEndFrame;
    gasPaused_ = EconomyPolicy::pauseGas(gasPaused_, balanceGas, self->minerals(), self->gas());
    if (gasPaused_ && frame % 96 == 0)
    {
        // Keep carried resources and redirect only one worker per transfer pass.
        for (auto entry = gasAssignments_.begin(); entry != gasAssignments_.end(); ++entry)
        {
            auto worker = Broodwar->getUnit(entry->first);
            if (!worker || worker->isCarryingGas() || worker->isCarryingMinerals()) continue;
            auto mineral = mineralTarget(worker);
            if (mineral && worker->gather(mineral))
            {
                std::ofstream("bwapi-data/write/gas-transfers.tsv", std::ios::app)
                    << frame << '\t' << worker->getID() << '\t' << entry->second << '\t' << mineral->getID() << '\n';
                mineralAssignments_[worker->getID()] = mineral->getID();
                gasAssignments_.erase(entry);
                break;
            }
        }
    }
    // Explicit assignments include travel/return phases and the latency before an order is visible.
    for (auto refinery : Broodwar->self()->getUnits())
    {
        if (gasPaused_) break;
        if (!refinery->getType().isRefinery() || !refinery->isCompleted() || refinery->getResources() <= 0) continue;
        int assigned = 0;
        for (const auto& assignment : gasAssignments_) if (assignment.second == refinery->getID()) ++assigned;
        if (assigned < 3)
        {
            for (auto worker : Broodwar->self()->getUnits())
                if (worker->getType().isWorker() && worker->isCompleted() && worker->getID() != scoutWorkerId_ &&
                    worker->getID() != buildTask_.workerId && !worker->isConstructing() &&
                    !gasAssignments_.count(worker->getID()) &&
                    !worker->isMorphing() && !worker->isGatheringGas() && !worker->isCarryingGas() &&
                    worker->getTarget() != refinery && worker->getOrderTarget() != refinery)
                    if (worker->gather(refinery))
                    { gasAssignments_[worker->getID()] = refinery->getID(); mineralAssignments_.erase(worker->getID()); break; }
        }
    }
    for (auto worker : Broodwar->self()->getUnits())
    {
        if (!worker->getType().isWorker() || !worker->isIdle() || worker->getID() == scoutWorkerId_ ||
            worker->getID() == buildTask_.workerId) continue;
        const auto gas = gasAssignments_.find(worker->getID());
        if (gas != gasAssignments_.end())
        {
            auto refinery = Broodwar->getUnit(gas->second);
            if (refinery) worker->gather(refinery);
            continue;
        }
        if (worker->getType() == UnitTypes::Terran_SCV)
        {
            bool resumed = false;
            for (auto building : Broodwar->self()->getUnits())
                if (building->getType().isBuilding() && !building->isCompleted() && !building->getBuildUnit())
                    if (worker->rightClick(building)) { resumed = true; break; }
            if (resumed) continue;
        }
        if (worker->isCarryingMinerals() || worker->isCarryingGas())
        {
            worker->returnCargo();
            continue;
        }
        auto target = mineralTarget(worker);
        if (target && worker->gather(target)) mineralAssignments_[worker->getID()] = target->getID();
    }
    if (Broodwar->getFrameCount() % 96 == 0)
    {
        std::map<int, int> loads;
        for (auto worker : Broodwar->self()->getUnits())
        {
            if (!worker->getType().isWorker() || worker->getID() == scoutWorkerId_ ||
                worker->getID() == buildTask_.workerId || gasAssignments_.count(worker->getID())) continue;
            auto target = worker->getOrderTarget();
            if (target && target->getType().isMineralField()) mineralAssignments_[worker->getID()] = target->getID();
            const auto assignment = mineralAssignments_.find(worker->getID());
            if (assignment != mineralAssignments_.end()) ++loads[assignment->second];
        }
        // Transfer only one oversaturated worker per pass, after a new depot is
        // complete. Building a base must not summon the entire mineral line.
        bool transferred = false;
        for (auto mineral : Broodwar->getMinerals())
        {
            if (transferred || !mineral->exists() || mineral->getResources() <= 0 || loads[mineral->getID()] >= 2) continue;
            bool supported = false;
            for (auto depot : Broodwar->self()->getUnits())
                if (depot->getType().isResourceDepot() && depot->isCompleted() && depot->getDistance(mineral) < 320)
                    supported = true;
            if (!supported) continue;
            for (auto worker : Broodwar->self()->getUnits())
            {
                const auto assignment = mineralAssignments_.find(worker->getID());
                if (!worker->getType().isWorker() || !worker->isGatheringMinerals() || worker->isCarryingMinerals() ||
                    worker->getID() == scoutWorkerId_ || worker->getID() == buildTask_.workerId ||
                    gasAssignments_.count(worker->getID()) || assignment == mineralAssignments_.end() ||
                    loads[assignment->second] <= 2 || !worker->hasPath(mineral)) continue;
                if (worker->gather(mineral))
                { mineralAssignments_[worker->getID()] = mineral->getID(); transferred = true; break; }
            }
        }
    }
    if (frame % 240 == 0)
        std::ofstream("bwapi-data/write/gas-balance.tsv", std::ios::app)
            << frame << '\t' << gasPaused_ << '\t' << self->minerals() << '\t' << self->gas()
            << '\t' << gasAssignments_.size() << '\n';
}

Unit PersonaBot::mineralTarget(Unit worker) const
{
    Unit target = nullptr;
    int distance = std::numeric_limits<int>::max();
    for (auto mineral : Broodwar->getMinerals())
    {
        if (!mineral->exists() || mineral->getResources() == 0) continue;
        bool supported = false;
        for (auto depot : Broodwar->self()->getUnits())
            if (depot->getType().isResourceDepot() && depot->isCompleted() &&
                depot->getDistance(mineral) < 400) supported = true;
        int assigned = 0;
        for (const auto& assignment : mineralAssignments_)
        {
            auto other = Broodwar->getUnit(assignment.first);
            if (other && other->exists() && other->getType().isWorker() &&
                assignment.second == mineral->getID() && assignment.first != worker->getID()) ++assigned;
        }
        const int d = worker->getDistance(mineral) + assigned * 160;
        if (supported && d < distance && worker->hasPath(mineral))
        {
            target = mineral;
            distance = d;
        }
    }
    return target;
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

bool PersonaBot::validateProfile() const
{
    const auto race = Broodwar->self()->getRace();
    for (const auto& weight : config_.weights)
    {
        const UnitType type(weight.id);
        if (type.getID() != weight.id || type.isBuilding() || type.isHero() || type.getRace() != race) return false;
    }
    auto allSteps = config_.steps;
    allSteps.insert(allSteps.end(), config_.opening.begin(), config_.opening.end());
    for (const auto& step : allSteps)
    {
        if (step.kind == 0 || step.kind == 3)
        {
            const UnitType type(step.id);
            if (type.getID() != step.id || type.isBuilding() != (step.kind == 0) || type.getRace() != race) return false;
        }
        else if (step.kind == 1)
        {
            const TechType tech(step.id);
            if (tech.getID() != step.id || tech.getRace() != race) return false;
        }
        else
        {
            const UpgradeType upgrade(step.id);
            if (upgrade.getID() != step.id || upgrade.getRace() != race) return false;
        }
    }
    return true;
}

void PersonaBot::recordStructure(Unit unit)
{
    if (!active_ || !unit || unit->getPlayer() != Broodwar->self() || !unit->getType().isBuilding()) return;
    const int type = unit->getType().getID();
    const auto previous = knownStructureTypes_.find(unit->getID());
    if (previous == knownStructureTypes_.end() || previous->second != type)
    {
        ++structureCounts_[type];
        knownStructureTypes_[unit->getID()] = type;
        const auto tile = unit->getTilePosition();
        std::ofstream("bwapi-data/write/structure-events.tsv", std::ios::app)
            << Broodwar->getFrameCount() << '\t' << unit->getID() << '\t' << type
            << '\t' << tile.x << '\t' << tile.y << '\n';
    }
}

void PersonaBot::onUnitCreate(Unit unit) { recordStructure(unit); }
void PersonaBot::onUnitMorph(Unit unit) { recordStructure(unit); }
void PersonaBot::onUnitDestroy(Unit unit)
{
    if (!active_ || !unit || unit->getPlayer() != Broodwar->self()) return;
    std::ofstream("bwapi-data/write/unit-deaths.tsv", std::ios::app)
        << Broodwar->getFrameCount() << '\t' << unit->getID() << '\t' << unit->getType().getID()
        << '\t' << unit->getTilePosition().x << '\t' << unit->getTilePosition().y << '\n';
    if (unit->getID() == buildTask_.workerId)
        failedBuildSites_[{buildTask_.typeId, buildTask_.tile.y * Broodwar->mapWidth() + buildTask_.tile.x}] =
            Broodwar->getFrameCount() + 1200;
}

bool PersonaBot::requirementsMet(UnitType type) const
{
    const auto self = Broodwar->self();
    for (const auto& requirement : type.requiredUnits())
    {
        int completed = self->completedUnitCount(requirement.first);
        if (requirement.first == UnitTypes::Zerg_Hatchery)
            completed += self->completedUnitCount(UnitTypes::Zerg_Lair) + self->completedUnitCount(UnitTypes::Zerg_Hive);
        if (requirement.first == UnitTypes::Zerg_Lair) completed += self->completedUnitCount(UnitTypes::Zerg_Hive);
        if (completed < requirement.second) return false;
    }
    const auto tech = type.requiredTech();
    return tech == TechTypes::None || self->hasResearched(tech);
}

bool PersonaBot::make(UnitType type)
{
    if (type.isBuilding()) return build(type);
    const auto producer = type.whatBuilds().first;
    for (auto unit : Broodwar->self()->getUnits())
    {
        if (unit->getType() != producer || !unit->isCompleted() || unit->isMorphing() ||
            (producer.isBuilding() && !unit->isIdle()) || !Broodwar->canMake(type, unit)) continue;
        const bool accepted = type.getRace() == Races::Zerg ? unit->morph(type) : unit->train(type);
        if (accepted) productionCounts_[type.getID()] += type.isTwoUnitsInOneEgg() ? 2 : 1;
        return accepted;
    }
    return false;
}

bool PersonaBot::prepareRequirements(UnitType type, int depth)
{
    if (depth > 5 || type == UnitTypes::None) return false;
    const auto self = Broodwar->self();
    for (const auto& requirement : type.requiredUnits())
    {
        if (!requirement.first.isBuilding()) continue;
        if (requirement.first == UnitTypes::Zerg_Hive) return false; // No automatic hive tech switch.
        int available = count(requirement.first);
        if (requirement.first == UnitTypes::Zerg_Lair) available += count(UnitTypes::Zerg_Hive);
        if (available >= requirement.second) continue;
        if (prepareRequirements(requirement.first, depth + 1)) return true;
        return make(requirement.first);
    }
    const auto tech = type.requiredTech();
    if (tech != TechTypes::None && !self->hasResearched(tech) && !self->isResearching(tech))
    {
        if (tech.requiredUnit() != UnitTypes::None && count(tech.requiredUnit()) == 0)
        {
            if (prepareRequirements(tech.requiredUnit(), depth + 1)) return true;
            return make(tech.requiredUnit());
        }
        if (count(tech.whatResearches()) == 0)
        {
            if (prepareRequirements(tech.whatResearches(), depth + 1)) return true;
            return make(tech.whatResearches());
        }
        for (auto building : self->getUnits())
            if (building->isCompleted() && Broodwar->canResearch(tech, building))
                return building->research(tech);
    }
    return false;
}

bool PersonaBot::openingProduction()
{
    if (config_.schemaVersion < 3) return false;
    const auto self = Broodwar->self();
    const int frame = Broodwar->getFrameCount();
    while (openingIndex_ < config_.opening.size())
    {
        const auto& order = config_.opening[openingIndex_];
        if (frame < order.frame) return true;
        bool complete = false;
        if (order.kind == 0) complete = structureCounts_[order.id] >= order.count;
        else if (order.kind == 3) complete = productionCounts_[order.id] >= order.count;
        else if (order.kind == 1) complete = self->hasResearched(TechType(order.id)) || self->isResearching(TechType(order.id));
        else complete = self->getUpgradeLevel(UpgradeType(order.id)) >= order.count || self->isUpgrading(UpgradeType(order.id));
        if (complete || frame > order.frame + 2400)
        {
            std::ofstream("bwapi-data/write/opening-execution.tsv", std::ios::app)
                << frame << '\t' << openingIndex_ << '\t' << order.frame << '\t' << order.kind
                << '\t' << order.id << '\t' << (complete ? "observed" : "timeout") << '\n';
            ++openingIndex_;
            continue;
        }
        if (order.kind == 0 || order.kind == 3) make(UnitType(order.id));
        else for (auto unit : self->getUnits())
        {
            if (!unit->isCompleted()) continue;
            if (order.kind == 1 && Broodwar->canResearch(TechType(order.id), unit))
            { unit->research(TechType(order.id)); break; }
            if (order.kind == 2 && Broodwar->canUpgrade(UpgradeType(order.id), unit))
            { unit->upgrade(UpgradeType(order.id)); break; }
        }
        return true;
    }
    return frame < config_.openingEndFrame;
}

bool PersonaBot::executeDueResearch()
{
    const auto self = Broodwar->self();
    const int frame = Broodwar->getFrameCount();
    for (const auto& step : config_.steps)
    {
        if (step.kind != 1 || frame < step.frame) continue;
        const TechType tech(step.id);
        if (self->hasResearched(tech) || self->isResearching(tech)) continue;
        if (tech.requiredUnit() != UnitTypes::None)
        {
            int completed = self->completedUnitCount(tech.requiredUnit());
            if (tech.requiredUnit() == UnitTypes::Zerg_Lair) completed += self->completedUnitCount(UnitTypes::Zerg_Hive);
            if (completed == 0) continue;
        }
        for (auto facility : self->getUnits())
        {
            if (facility->getType() != tech.whatResearches() || !facility->isCompleted() ||
                !facility->isIdle() || !facility->isPowered()) continue;
            // Let an observed, due research save its cost before optional unit
            // production consumes it again. Do not wait for unavailable gas.
            if (self->minerals() < tech.mineralPrice() || self->gas() < tech.gasPrice())
            {
                const bool canSave = self->gas() >= tech.gasPrice() ||
                    self->completedUnitCount(self->getRace().getRefinery()) > 0;
                if (canSave)
                {
                    if (frame % 240 == 0) std::ofstream("bwapi-data/write/research-actions.tsv", std::ios::app)
                        << frame << '\t' << tech.getID() << "\tsaving\t" << self->minerals() << '\t' << self->gas() << '\n';
                    return true;
                }
                continue;
            }
            if (Broodwar->canResearch(tech, facility) && facility->research(tech))
            {
                std::ofstream("bwapi-data/write/research-actions.tsv", std::ios::app)
                    << frame << '\t' << tech.getID() << "\tstarted\t" << facility->getID() << '\n';
                return true;
            }
        }
    }
    return false;
}

void PersonaBot::imitationProduction()
{
    const auto self = Broodwar->self();
    const auto workerType = self->getRace().getWorker();
    const auto supplyType = self->getRace().getSupplyProvider();
    const int frame = Broodwar->getFrameCount();
    if (count(workerType) == 0) { make(workerType); return; }
    bool pendingBuild = buildTask_.workerId >= 0;
    if (pendingBuild)
        for (auto unit : self->getUnits())
            if (unit->getType() == UnitType(buildTask_.typeId) && unit->getTilePosition() == buildTask_.tile)
                pendingBuild = false;
    const int reservedMinerals = pendingBuild ? UnitType(buildTask_.typeId).mineralPrice() : 0;
    const int reservedGas = pendingBuild ? UnitType(buildTask_.typeId).gasPrice() : 0;
    int pendingSupply = 0;
    for (auto unit : self->getUnits())
    {
        if (unit->getType() == supplyType && !unit->isCompleted()) pendingSupply += supplyType.supplyProvided();
        else if (unit->isMorphing() && unit->getBuildType() == supplyType) pendingSupply += supplyType.supplyProvided();
    }
    const bool recordedOpening = config_.schemaVersion >= 3 && frame <= config_.openingEndFrame;
    int supplyBuffer = 4;
    if (self->getRace() == Races::Protoss)
    {
        int producers = 0, nexuses = 0;
        for (auto unit : self->getUnits())
        {
            if (!unit->isCompleted() || !unit->isPowered()) continue;
            const auto type = unit->getType();
            if (type == UnitTypes::Protoss_Nexus) ++nexuses;
            if (type == UnitTypes::Protoss_Gateway || type == UnitTypes::Protoss_Robotics_Facility ||
                type == UnitTypes::Protoss_Stargate) ++producers;
        }
        supplyBuffer = SupplyPolicy::protossBuffer(recordedOpening, producers, nexuses);
    }
    else if (self->getRace() == Races::Zerg)
    {
        int depots = 0;
        for (auto unit : self->getUnits())
            if (unit->getType().isResourceDepot() && unit->isCompleted()) ++depots;
        supplyBuffer = SupplyPolicy::zergBuffer(recordedOpening, depots);
    }
    if (SupplyPolicy::needsProvider(self->supplyUsed(), self->supplyTotal(), pendingSupply, supplyBuffer))
    {
        // During recorded openings only recover a real supply block, preserving observed supply timing.
        if (!recordedOpening || self->supplyTotal() - self->supplyUsed() <= 0)
        {
            if (make(supplyType))
            {
                std::ofstream("bwapi-data/write/supply-plans.tsv", std::ios::app)
                    << frame << '\t' << self->supplyUsed() << '\t' << self->supplyTotal()
                    << '\t' << pendingSupply << '\t' << supplyBuffer << '\n';
                return;
            }
        }
    }
    if (openingProduction()) return;
    if (executeDueResearch()) return;
    if (frame % 120 == 0 && !pendingBuild && self->minerals() >= 200 && self->gas() >= 150)
    {
        const int phase = frame < 4320 ? 0 : frame < 8640 ? 1 : frame < 14400 ? 2 : 3;
        for (const auto& entry : config_.weights)
            if (entry.phase == phase && entry.weight >= 100 && !UnitType(entry.id).isWorker() &&
                !requirementsMet(UnitType(entry.id)) && prepareRequirements(UnitType(entry.id))) return;
    }
    if (self->getRace() == Races::Protoss && frame % 120 == 0 && buildTask_.workerId < 0)
    {
        ProductionPolicy::Capacity capacity;
        capacity.frame = frame;
        capacity.lastAddition = lastMacroGatewayFrame_;
        capacity.minerals = self->minerals();
        capacity.freeSupply = self->supplyTotal() - self->supplyUsed();
        for (auto unit : self->getUnits())
            if (unit->getType() == UnitTypes::Protoss_Gateway)
            {
                if (!unit->isCompleted()) ++capacity.incomplete;
                else if (unit->isPowered())
                { ++capacity.completed; if (unit->isTraining()) ++capacity.busy; }
            }
        for (const auto& step : config_.steps)
            if (step.kind == 0 && step.id == UnitTypes::Protoss_Gateway.getID())
                capacity.planned = std::max(capacity.planned, step.count);
        for (const auto& entry : config_.weights)
        {
            const UnitType type(entry.id);
            if (entry.phase == 3 && entry.weight > 0 && type.whatBuilds().first == UnitTypes::Protoss_Gateway &&
                requirementsMet(type) && self->gas() >= type.gasPrice()) capacity.affordableUnit = true;
        }
        if (ProductionPolicy::addGateway(capacity) && build(UnitTypes::Protoss_Gateway))
        {
            lastMacroGatewayFrame_ = frame;
            std::ofstream("bwapi-data/write/capacity-plans.tsv", std::ios::app)
                << frame << '\t' << capacity.completed << '\t' << capacity.busy << '\t' << capacity.minerals
                << '\t' << capacity.freeSupply << '\t' << buildTask_.tile.x << '\t' << buildTask_.tile.y << '\n';
            return;
        }
    }
    // Execution fallback: when existing hatcheries cannot spend the bank, add
    // production. This is a shared rule, not a learned personal preference.
    if (self->getRace() == Races::Zerg && frame - lastMacroHatchFrame_ >= 1200 &&
        self->minerals() >= 650 && buildTask_.workerId < 0 && count(UnitTypes::Zerg_Hatchery) < 8)
    {
        int larvae = 0, incomplete = 0;
        for (auto unit : self->getUnits())
        {
            if (unit->getType() == UnitTypes::Zerg_Larva) ++larvae;
            if (unit->getType().isResourceDepot() && !unit->isCompleted()) ++incomplete;
        }
        if (larvae <= 2 && incomplete == 0 && build(UnitTypes::Zerg_Hatchery))
        { lastMacroHatchFrame_ = frame; return; }
    }
    for (std::size_t i = 0; i < config_.steps.size(); ++i)
    {
        const auto& step = config_.steps[i];
        if (finishedSteps_[i] || frame < step.frame) continue;
        if (step.kind == 0)
        {
            const UnitType type(step.id);
            if (structureCounts_[step.id] >= step.count) { finishedSteps_[i] = true; continue; }
            if (!requirementsMet(type)) continue;
            // Reserve the outstanding building cost, but do not stall all production while a worker travels.
            if (buildTask_.workerId >= 0) continue;
            if (make(type)) return;
            if (self->minerals() < type.mineralPrice() || self->gas() < type.gasPrice())
            {
                // Do not reserve unavailable gas forever on a failed refinery/empty map.
                if (self->gas() >= type.gasPrice() || self->completedUnitCount(self->getRace().getRefinery()) > 0)
                    return;
            }
        }
        else if (step.kind == 1)
        {
            const TechType tech(step.id);
            if (self->minerals() < tech.mineralPrice() + reservedMinerals || self->gas() < tech.gasPrice() + reservedGas) continue;
            if (self->hasResearched(tech)) { finishedSteps_[i] = true; continue; }
            if (self->isResearching(tech)) continue;
            for (auto unit : self->getUnits())
                if (unit->isCompleted() && Broodwar->canResearch(tech, unit) && unit->research(tech)) return;
        }
        else
        {
            const UpgradeType upgrade(step.id);
            if (self->minerals() < upgrade.mineralPrice() + reservedMinerals || self->gas() < upgrade.gasPrice() + reservedGas) continue;
            if (self->getUpgradeLevel(upgrade) >= step.count) { finishedSteps_[i] = true; continue; }
            if (self->isUpgrading(upgrade)) continue;
            for (auto unit : self->getUnits())
                if (unit->isCompleted() && Broodwar->canUpgrade(upgrade, unit) && unit->upgrade(upgrade)) return;
        }
    }
    // Reavers/carriers require ammunition. This is a shared execution rule.
    for (auto unit : self->getUnits())
    {
        if (!unit->isCompleted()) continue;
        if (unit->getType() == UnitTypes::Protoss_Reaver && unit->getScarabCount() < 5 && unit->getTrainingQueue().empty())
            if (unit->train(UnitTypes::Protoss_Scarab)) return;
        if (unit->getType() == UnitTypes::Protoss_Carrier && unit->getInterceptorCount() < 4 && unit->getTrainingQueue().empty())
            if (unit->train(UnitTypes::Protoss_Interceptor)) return;
    }
    std::vector<int> legal;
    for (const auto& entry : config_.weights)
    {
        const UnitType type(entry.id);
        if (std::find(legal.begin(), legal.end(), entry.id) != legal.end() ||
            self->minerals() < type.mineralPrice() + reservedMinerals || self->gas() < type.gasPrice() + reservedGas ||
            (type.isWorker() && count(type) >= config_.workerTarget)) continue;
        for (auto producer : self->getUnits())
            if (producer->getType() == type.whatBuilds().first && producer->isCompleted() && !producer->isMorphing() &&
                (!producer->getType().isBuilding() || producer->isIdle()) && Broodwar->canMake(type, producer))
            { legal.push_back(entry.id); break; }
    }
    const int selected = config_.chooseUnit(frame, legal, random_);
    if (selected >= 0) make(UnitType(selected));
    if (frame % 240 == 0)
    {
        std::ofstream log("bwapi-data/write/production-decisions.tsv", std::ios::app);
        log << frame << '\t' << selected << '\t' << self->supplyUsed() << '\t' << self->supplyTotal();
        for (const auto id : legal) log << '\t' << id;
        log << '\n';
    }
}

void PersonaBot::scout()
{
    if (config_.schemaVersion < 2 || searchLocations_.empty()) return;
    const auto self = Broodwar->self();
    // Send the first probe after the first pylon starts (pylon scout).
    // This is explicitly a baseline until replay scout timing is reconstructed.
    if (self->getRace() == Races::Protoss)
    { if (structureCounts_[UnitTypes::Protoss_Pylon.getID()] == 0) return; }
    else if (Broodwar->getFrameCount() < 1800) return;
    if (scoutWorkerId_ >= 0)
    {
        auto worker = Broodwar->getUnit(scoutWorkerId_);
        if (!worker || !worker->exists()) { scoutWorkerId_ = -2; return; }
        if (!enemyBuildings_.empty() || scoutIndex_ >= searchLocations_.size())
        { worker->move(Position(self->getStartLocation()) + Position(64, 48)); scoutWorkerId_ = -2; return; }
        if (Broodwar->isExplored(searchLocations_[scoutIndex_])) { ++scoutIndex_; return; }
        const Position target = Position(searchLocations_[scoutIndex_]) + Position(64, 48);
        if (worker->isIdle() || Broodwar->getFrameCount() - worker->getLastCommandFrame() >= 96) worker->move(target);
    }
    else if (scoutWorkerId_ == -1 && count(Broodwar->self()->getRace().getWorker()) >= 8)
    {
        for (auto worker : Broodwar->self()->getUnits())
            if (worker->getType().isWorker() && worker->isCompleted() && !worker->isConstructing() &&
                !worker->isGatheringGas() && worker->getID() != buildTask_.workerId)
            {
                scoutWorkerId_ = worker->getID();
                gasAssignments_.erase(scoutWorkerId_);
                mineralAssignments_.erase(scoutWorkerId_);
                std::ofstream("bwapi-data/write/scout-orders.tsv", std::ios::app)
                    << Broodwar->getFrameCount() << '\t' << scoutWorkerId_ << '\n';
                break;
            }
    }
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
    const int frame = Broodwar->getFrameCount();
    Unitset combat;
    for (auto unit : Broodwar->self()->getUnits())
        if (unit->isCompleted() && !unit->getType().isBuilding() && !unit->getType().isWorker() &&
            (unit->getType().canAttack() || unit->getType() == UnitTypes::Protoss_High_Templar ||
             unit->getType() == UnitTypes::Protoss_Observer || unit->getType() == UnitTypes::Terran_Medic ||
             unit->getType() == UnitTypes::Terran_Science_Vessel)) combat.insert(unit);
    if (combat.empty()) return;
    Position center(0, 0);
    int fighters = 0;
    TacticalPolicy::Situation situation;
    situation.frame = frame;
    situation.attackFrame = config_.firstAttackFrame;
    situation.retreatUntil = retreatUntil_;
    for (auto unit : combat)
    {
        if (!unit->getType().canAttack()) continue;
        center += unit->getPosition();
        ++fighters;
        situation.friendlySupply += unit->getType().supplyRequired();
        situation.hasLurkers |= unit->getType() == UnitTypes::Zerg_Lurker;
    }
    if (fighters == 0) center = assemblyPoint();
    else center /= fighters;
    const auto power = [](Unit unit)
    {
        const auto type = unit->getType();
        const double health = static_cast<double>(unit->getHitPoints() + unit->getShields()) /
            std::max(1, type.maxHitPoints() + type.maxShields());
        double value = std::max(1, type.supplyRequired());
        if (type.isBuilding()) value = 8;
        if (type == UnitTypes::Protoss_High_Templar) value = unit->getEnergy() >= 75 ? 8 : 2;
        if (type.isWorker()) value = 0.5;
        return value * (0.4 + 0.6 * health);
    };
    for (auto unit : combat)
        if (unit->getDistance(center) <= 640) situation.nearbyFriendlyPower += power(unit);
    Position defense = Positions::None;
    std::vector<Unit> visibleEnemies;
    for (auto enemy : Broodwar->getAllUnits())
    {
        if (!enemy->isVisible() || !enemy->getPlayer()->isEnemy(Broodwar->self())) continue;
        visibleEnemies.push_back(enemy);
        if (enemy->getType().isResourceDepot()) lastEnemyBaseSeenFrame_ = frame;
        const bool dangerous = enemy->getType().canAttack() || enemy->getType() == UnitTypes::Protoss_High_Templar;
        if (dangerous && enemy->getDistance(center) <= 640)
        {
            situation.nearbyEnemyPower += power(enemy);
            situation.enemyTemplarSeen |= enemy->getType() == UnitTypes::Protoss_High_Templar;
        }
        for (auto base : Broodwar->self()->getUnits())
            if (dangerous && !enemy->getType().isWorker() && base->getType().isResourceDepot() && enemy->getDistance(base) < 480)
                defense = enemy->getPosition();
    }
    situation.baseThreatened = defense.isValid();
    situation.enemyBaseRecentlySeen = frame - lastEnemyBaseSeenFrame_ <= 1440;
    const auto next = TacticalPolicy::choose(situation);
    if (next != stance_)
    {
        std::ofstream("bwapi-data/write/tactical-decisions.tsv", std::ios::app)
            << frame << '\t' << TacticalPolicy::name(next) << '\t' << situation.friendlySupply << '\t'
            << situation.nearbyFriendlyPower << '\t' << situation.nearbyEnemyPower << '\t'
            << situation.enemyBaseRecentlySeen << '\n';
        if (next == TacticalPolicy::Stance::Retreat) retreatUntil_ = frame + 240;
        stance_ = next;
    }
    attacking_ = stance_ == TacticalPolicy::Stance::Advance;
    Position target = defense.isValid() ? defense : attacking_ ? attackTarget() : assemblyPoint();
    if (stance_ == TacticalPolicy::Stance::Hold) target = center;
    if (std::getenv("PERSONA_AUTO_CAMERA") && Broodwar->getFrameCount() % 240 == 0 && !combat.empty())
    {
        Unit focus = *combat.begin();
        for (auto unit : combat)
            if (unit->isUnderAttack() || unit->getDistance(target) < focus->getDistance(target)) focus = unit;
        Broodwar->setScreenPosition(focus->getPosition() - Position(320, 200));
    }
    for (auto unit : combat)
    {
        if (unit->isAttackFrame() || frame - unit->getLastCommandFrame() < 48) continue;
        Unit nearby = nullptr;
        int nearbyCount = 0;
        for (auto enemy : visibleEnemies)
            if (!enemy->isFlying() && unit->getDistance(enemy) < 256)
            { if (!nearby || unit->getDistance(enemy) < unit->getDistance(nearby)) nearby = enemy; ++nearbyCount; }
        const bool retreating = stance_ == TacticalPolicy::Stance::Retreat;
        if (unit->getType() == UnitTypes::Zerg_Lurker)
        {
            const bool anchor = stance_ == TacticalPolicy::Stance::Hold && unit->getDistance(target) < 128;
            if (!retreating && (nearby || anchor) && !unit->isBurrowed()) { unit->burrow(); continue; }
            if ((retreating || (!nearby && !anchor)) && unit->isBurrowed()) { unit->unburrow(); continue; }
            if (unit->isBurrowed()) continue;
        }
        if (unit->getType() == UnitTypes::Terran_Siege_Tank_Tank_Mode && nearby &&
            Broodwar->self()->hasResearched(TechTypes::Tank_Siege_Mode)) { unit->siege(); continue; }
        if (unit->getType() == UnitTypes::Terran_Siege_Tank_Siege_Mode && !nearby) { unit->unsiege(); continue; }
        if (unit->getType() == UnitTypes::Protoss_High_Templar)
        {
            Unit stormTarget = nullptr;
            int bestScore = 2;
            if (unit->getEnergy() >= 75 && Broodwar->self()->hasResearched(TechTypes::Psionic_Storm))
                for (auto candidate : visibleEnemies)
                {
                    if (candidate->getType().isBuilding() || candidate->getType().isInvincible() ||
                        unit->getDistance(candidate) > 288 || candidate->isUnderStorm() ||
                        (frame - lastStormFrame_ < 72 && candidate->getDistance(lastStormPosition_) < 96)) continue;
                    int score = 0;
                    for (auto enemy : visibleEnemies)
                        if (!enemy->getType().isBuilding() && enemy->getDistance(candidate) < 64) ++score;
                    for (auto friendly : combat) if (friendly->getDistance(candidate) < 64) score -= 2;
                    if (score > bestScore) { stormTarget = candidate; bestScore = score; }
                }
            if (stormTarget && unit->useTech(TechTypes::Psionic_Storm, stormTarget->getPosition()))
            {
                lastStormFrame_ = frame;
                lastStormPosition_ = stormTarget->getPosition();
                std::ofstream("bwapi-data/write/micro-actions.tsv", std::ios::app)
                    << frame << '\t' << unit->getID() << "\tstorm\t" << bestScore << '\n';
                continue;
            }
            // A global army centroid can pull a lone caster through enemy troops.
            Position escortCenter(0, 0);
            int escorts = 0;
            for (auto friendly : combat)
                if (friendly->getType().groundWeapon() != WeaponTypes::None &&
                    !friendly->isFlying() && friendly->getDistance(unit) <= 480)
                { escortCenter += friendly->getPosition(); ++escorts; }
            escortCenter = escorts ? escortCenter / escorts : assemblyPoint();
            Unit casterThreat = nullptr;
            for (auto enemy : visibleEnemies)
                if (enemy->getType().groundWeapon() != WeaponTypes::None &&
                    (!casterThreat || enemy->getDistance(unit) < casterThreat->getDistance(unit))) casterThreat = enemy;
            const Position threat = casterThreat ? casterThreat->getPosition() : target;
            const int distance = std::max(1, escortCenter.getApproxDistance(threat));
            const bool ready = unit->getEnergy() >= 75 && Broodwar->self()->hasResearched(TechTypes::Psionic_Storm);
            Position screen = escortCenter + (escortCenter - threat) * (ready ? 112 : 192) / distance;
            screen.makeValid();
            if (retreating || escorts == 0 || !unit->hasPath(screen)) screen = assemblyPoint();
            const auto clearance = [&](Position position)
            {
                int result = 640;
                for (auto enemy : visibleEnemies)
                {
                    const auto weapon = enemy->getType().groundWeapon();
                    if (weapon == WeaponTypes::None) continue;
                    // Use type range, not unseen opponent upgrade information.
                    result = std::min(result, enemy->getDistance(position) - weapon.maxRange() - 32);
                }
                return result;
            };
            const Position origin = unit->getPosition();
            const int currentClearance = clearance(origin);
            const Position regroup = screen;
            const auto nextWaypoint = [&](Position position)
            {
                const int length = origin.getApproxDistance(position);
                return length > 320 ? origin + (position - origin) * 320 / length : position;
            };
            const auto pathClearance = [&](Position destination)
            {
                int result = currentClearance;
                const int steps = std::max(1, origin.getApproxDistance(destination) / 32);
                for (int step = 1; step <= steps; ++step)
                    result = std::min(result, clearance(origin + (destination - origin) * step / steps));
                return result;
            };
            const auto movementScore = [&](Position position)
            {
                const int edge = std::min({position.x, position.y,
                    Broodwar->mapWidth() * 32 - 1 - position.x, Broodwar->mapHeight() * 32 - 1 - position.y});
                return TacticalPolicy::casterPositionScore(clearance(position), pathClearance(position),
                    currentClearance, origin.getApproxDistance(position), position.getApproxDistance(regroup), edge);
            };
            screen = nextWaypoint(screen);
            const bool evade = currentClearance < (ready ? 96 : 192) ||
                clearance(screen) < 96 || pathClearance(screen) < std::min(0, currentClearance) - 16 ||
                !Broodwar->isWalkable(WalkPosition(screen)) || !unit->hasPath(screen) || unit->isUnderStorm();
            if (evade)
            {
                Position best = origin;
                double bestMoveScore = movementScore(origin);
                std::vector<Position> candidates{screen, assemblyPoint()};
                for (int radius : {160, 320})
                    for (int dx = -1; dx <= 1; ++dx)
                        for (int dy = -1; dy <= 1; ++dy)
                            if (dx || dy) candidates.push_back(origin + Position(dx * radius, dy * radius));
                for (auto candidate : candidates)
                {
                    candidate = nextWaypoint(candidate);
                    if (!candidate.isValid() || !Broodwar->isWalkable(WalkPosition(candidate)) || !unit->hasPath(candidate)) continue;
                    double score = movementScore(candidate);
                    if (unit->isUnderStorm() && origin.getApproxDistance(candidate) >= 96) score += 160;
                    if (score > bestMoveScore) { bestMoveScore = score; best = candidate; }
                }
                screen = best;
            }
            const bool hold = evade && unit->getDistance(screen) <= 24;
            const bool issued = hold ? unit->isMoving() && unit->stop() :
                unit->getDistance(screen) > (evade ? 24 : 64) && unit->move(screen);
            if (issued)
                std::ofstream("bwapi-data/write/caster-moves.tsv", std::ios::app)
                    << frame << '\t' << unit->getID() << '\t' << (hold ? "hold" : evade ? "evade" : "screen")
                    << '\t' << escorts << '\t' << currentClearance << '\t' << clearance(screen)
                    << '\t' << screen.x << '\t' << screen.y << '\t' << origin.x << '\t' << origin.y
                    << '\t' << pathClearance(screen) << '\n';
            continue;
        }
        if (retreating || unit->isUnderStorm())
        {
            Position retreat = assemblyPoint();
            if (unit->isUnderStorm() && nearby)
            {
                retreat = unit->getPosition() + (unit->getPosition() - nearby->getPosition()) * 128 /
                    std::max(1, unit->getDistance(nearby));
                retreat.makeValid();
            }
            unit->move(retreat);
        }
        else if (unit->getType().canAttack() && (attacking_ || defense.isValid() || nearby)) unit->attack(target);
        else if (unit->getDistance(target) > 96) unit->move(target);
    }
}

void PersonaBot::logCasterState(Player player, const char* path) const
{
    // Replay observations are diagnostics only and never feed the live policy.
    std::vector<Unit> enemies;
    for (auto unit : Broodwar->getAllUnits())
        if (unit->isVisible(player) && unit->getPlayer()->isEnemy(player) && !unit->getType().isBuilding())
            enemies.push_back(unit);
    std::ofstream log(path, std::ios::app);
    for (auto caster : player->getUnits())
    {
        if (caster->getType() != UnitTypes::Protoss_High_Templar || !caster->isCompleted()) continue;
        int nearest = -1, bestScore = 0;
        for (auto target : enemies)
        {
            const int distance = caster->getDistance(target);
            if (nearest < 0 || distance < nearest) nearest = distance;
            if (distance > 288 || target->getType().isInvincible() || target->isUnderStorm()) continue;
            int score = 0;
            for (auto enemy : enemies) if (enemy->getDistance(target) < 64) ++score;
            for (auto friendly : player->getUnits())
                if (!friendly->getType().isWorker() && !friendly->getType().isBuilding() &&
                    friendly->getDistance(target) < 64) score -= 2;
            bestScore = std::max(bestScore, score);
        }
        log << Broodwar->getFrameCount() << '\t' << player->getID() << '\t' << caster->getID()
            << '\t' << caster->getEnergy() << '\t' << player->hasResearched(TechTypes::Psionic_Storm)
            << '\t' << player->isResearching(TechTypes::Psionic_Storm) << '\t' << nearest
            << '\t' << bestScore << '\t' << caster->getOrder().getID()
            << '\t' << caster->getPosition().x << '\t' << caster->getPosition().y << '\n';
    }
}

void PersonaBot::onFrame()
{
    if (Broodwar->isReplay())
    {
        if (!std::getenv("PERSONA_REPLAY_ANALYSIS") || Broodwar->isPaused()) return;
        if (Broodwar->getFrameCount() % 240 == 0)
        {
            std::ofstream("bwapi-data/write/replay-progress.txt") << Broodwar->getFrameCount() << '\n';
            for (auto player : Broodwar->getPlayers())
                if (player->getRace() == Races::Protoss)
                    logCasterState(player, "bwapi-data/write/replay-casters.tsv");
        }
        if (testMaxFrames_ > 0 && Broodwar->getFrameCount() >= testMaxFrames_)
        {
            std::ofstream("bwapi-data/write/replay-analysis-limit.txt") << Broodwar->getFrameCount() << '\n';
            Broodwar->leaveGame();
        }
        return;
    }
    if (!active_ || Broodwar->isPaused() || !Broodwar->self()) return;
    if (testMaxFrames_ > 0 && Broodwar->getFrameCount() >= testMaxFrames_)
    {
        std::ofstream("bwapi-data/write/test-limit.txt") << Broodwar->getFrameCount() << '\n';
        Broodwar->leaveGame();
        return;
    }
    Broodwar->drawTextScreen(8, 8, "Persona: %s | samples %d | prototype", config_.name.c_str(), config_.sampleGames);
    if (Broodwar->getFrameCount() % std::max(6, Broodwar->getLatencyFrames()) != 0) return;
    economy();
    scout();
    rally();
    if (config_.schemaVersion >= 2) imitationProduction();
    else production();
    army();
    if (Broodwar->getFrameCount() % 240 == 0)
    {
        if (Broodwar->self()->getRace() == Races::Protoss)
            logCasterState(Broodwar->self(), "bwapi-data/write/caster-state.tsv");
        int workers = 0, troops = 0, buildings = 0;
        for (auto unit : Broodwar->self()->getUnits())
        {
            if (unit->getType().isWorker()) ++workers;
            else if (unit->getType().isBuilding()) ++buildings;
            else if (unit->getType().canAttack()) ++troops;
        }
        std::ofstream log("bwapi-data/write/live-status.tsv", std::ios::app);
        log << Broodwar->getFrameCount() << '\t' << workers << '\t' << troops << '\t' << buildings
            << '\t' << Broodwar->self()->minerals() << '\t' << Broodwar->self()->gas()
            << '\t' << buildTask_.workerId << '\t' << buildTask_.typeId
            << '\t' << Broodwar->self()->supplyUsed() << '\t' << Broodwar->self()->supplyTotal() << '\n';
        std::ofstream units("bwapi-data/write/unit-counts.tsv", std::ios::app);
        std::map<int, int> completed, unfinished;
        for (auto unit : Broodwar->self()->getUnits())
            ++(unit->isCompleted() ? completed : unfinished)[unit->getType().getID()];
        for (const auto& entry : completed)
            units << Broodwar->getFrameCount() << '\t' << entry.first << '\t' << entry.second << '\t'
                  << unfinished[entry.first] << '\n';
        for (const auto& entry : unfinished)
            if (!completed.count(entry.first)) units << Broodwar->getFrameCount() << '\t' << entry.first
                << "\t0\t" << entry.second << '\n';
        std::ofstream facilities("bwapi-data/write/facilities.tsv", std::ios::app);
        for (auto unit : Broodwar->self()->getUnits())
            if (unit->getType().isBuilding() && unit->getType().canProduce())
                facilities << Broodwar->getFrameCount() << '\t' << unit->getID() << '\t'
                    << unit->getType().getID() << '\t' << unit->isCompleted() << '\t' << unit->isPowered()
                    << '\t' << unit->isIdle() << '\t' << unit->getTrainingQueue().size() << '\n';
    }
}

void PersonaBot::onEnd(bool isWinner)
{
    if (!active_) return;
    std::ofstream log("bwapi-data/write/persona-results.txt", std::ios::app);
    const bool capped = testMaxFrames_ > 0 && Broodwar->getFrameCount() >= testMaxFrames_ - 24;
    log << config_.name << '\t' << (capped ? "test_limit" : isWinner ? "win" : "loss") << '\t'
        << Broodwar->getFrameCount() << '\n';
}
