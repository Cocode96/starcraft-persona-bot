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
    scoutWorkerId_ = -1;
    scoutIndex_ = 0;
    structureCounts_.clear();
    knownStructureTypes_.clear();
    if (Broodwar->isReplay() || !Broodwar->self()) return;
    std::ifstream input("bwapi-data/read/persona.ini");
    std::string error;
    if (!input || !config_.load(input, error))
        Broodwar->printf("Persona profile unavailable (%s). Using explicit baseline rules.",
                        input ? error.c_str() : "file missing");
    // Random/unknown opponents use the pooled profile; do not inspect hidden units.
    if (config_.schemaVersion == 2 && Broodwar->enemy())
    {
        const auto opponent = Broodwar->enemy()->getRace().getName();
        if (opponent == "Zerg" || opponent == "Terran" || opponent == "Protoss")
        {
            std::ifstream matchup("bwapi-data/read/persona-" + opponent + ".ini");
            PersonaConfig candidate;
            if (matchup && candidate.load(matchup, error) && candidate.name == config_.name && candidate.race == config_.race)
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
    for (auto unit : Broodwar->self()->getUnits()) recordStructure(unit);
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

TilePosition PersonaBot::buildTile(UnitType type) const
{
    const auto home = Broodwar->self()->getStartLocation();
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
    if (!worker || !worker->build(type, tile)) return false;
    buildTask_ = {worker->getID(), Broodwar->getFrameCount(), type.getID(), expected};
    return true;
}

void PersonaBot::economy()
{
    if (buildTask_.workerId >= 0)
    {
        auto worker = Broodwar->getUnit(buildTask_.workerId);
        if (!worker || !worker->exists() || !worker->getType().isWorker() ||
            (worker->getType().getRace() != Races::Terran && count(UnitType(buildTask_.typeId)) >= buildTask_.expectedCount) ||
            (Broodwar->getFrameCount() - buildTask_.issuedFrame > 600 && !worker->isConstructing()))
            buildTask_ = {};
    }
    // Assign at most one new gas worker per tick; count units already en route.
    for (auto refinery : Broodwar->self()->getUnits())
    {
        if (!refinery->getType().isRefinery() || !refinery->isCompleted() || refinery->getResources() <= 0) continue;
        int assigned = 0;
        for (auto worker : Broodwar->self()->getUnits())
            if (worker->getType().isWorker() &&
                (worker->getTarget() == refinery || worker->getOrderTarget() == refinery ||
                 (worker->isGatheringGas() && worker->getDistance(refinery) < 128)))
                ++assigned;
        if (assigned < 3)
        {
            for (auto worker : Broodwar->self()->getUnits())
                if (worker->getType().isWorker() && worker->isCompleted() && worker->getID() != scoutWorkerId_ &&
                    worker->getID() != buildTask_.workerId && !worker->isConstructing() &&
                    !worker->isMorphing() && !worker->isGatheringGas() && !worker->isCarryingGas() &&
                    worker->getTarget() != refinery && worker->getOrderTarget() != refinery)
                    if (worker->gather(refinery)) break;
        }
    }
    for (auto worker : Broodwar->self()->getUnits())
    {
        if (!worker->getType().isWorker() || !worker->isIdle() || worker->getID() == scoutWorkerId_ ||
            worker->getID() == buildTask_.workerId) continue;
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

bool PersonaBot::validateProfile() const
{
    const auto race = Broodwar->self()->getRace();
    for (const auto& weight : config_.weights)
    {
        const UnitType type(weight.id);
        if (type.getID() != weight.id || type.isBuilding() || type.isHero() || type.getRace() != race) return false;
    }
    for (const auto& step : config_.steps)
    {
        if (step.kind == 0)
        {
            const UnitType type(step.id);
            if (type.getID() != step.id || !type.isBuilding() || type.getRace() != race) return false;
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
    }
}

void PersonaBot::onUnitCreate(Unit unit) { recordStructure(unit); }
void PersonaBot::onUnitMorph(Unit unit) { recordStructure(unit); }

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
        return type.getRace() == Races::Zerg ? unit->morph(type) : unit->train(type);
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
    int pendingSupply = 0;
    for (auto unit : self->getUnits())
    {
        if (unit->getType() == supplyType && !unit->isCompleted()) pendingSupply += supplyType.supplyProvided();
        else if (unit->isMorphing() && unit->getBuildType() == supplyType) pendingSupply += supplyType.supplyProvided();
    }
    if (self->supplyTotal() < 400 && self->supplyTotal() + pendingSupply - self->supplyUsed() <= 4)
    {
        make(supplyType);
        return;
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
            // Let an already-issued worker build reach the site before spending its resources.
            if (buildTask_.workerId >= 0) return;
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
            if (self->hasResearched(tech)) { finishedSteps_[i] = true; continue; }
            if (self->isResearching(tech)) continue;
            for (auto unit : self->getUnits())
                if (unit->isCompleted() && Broodwar->canResearch(tech, unit) && unit->research(tech)) return;
        }
        else
        {
            const UpgradeType upgrade(step.id);
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
            (type.isWorker() && count(type) >= config_.workerTarget)) continue;
        for (auto producer : self->getUnits())
            if (producer->getType() == type.whatBuilds().first && producer->isCompleted() && !producer->isMorphing() &&
                (!producer->getType().isBuilding() || producer->isIdle()) && Broodwar->canMake(type, producer))
            { legal.push_back(entry.id); break; }
    }
    const int selected = config_.chooseUnit(frame, legal, random_);
    if (selected >= 0) make(UnitType(selected));
}

void PersonaBot::scout()
{
    if (config_.schemaVersion != 2 || searchLocations_.empty() || Broodwar->getFrameCount() < 2400) return;
    if (scoutWorkerId_ >= 0)
    {
        auto worker = Broodwar->getUnit(scoutWorkerId_);
        if (!worker || !worker->exists()) { scoutWorkerId_ = -2; return; }
        if (!enemyBuildings_.empty() || scoutIndex_ >= searchLocations_.size())
        { worker->stop(); scoutWorkerId_ = -2; return; }
        if (Broodwar->isExplored(searchLocations_[scoutIndex_])) { ++scoutIndex_; return; }
        const Position target = Position(searchLocations_[scoutIndex_]) + Position(64, 48);
        if (worker->isIdle() || Broodwar->getFrameCount() - worker->getLastCommandFrame() >= 96) worker->move(target);
    }
    else if (scoutWorkerId_ == -1 && count(Broodwar->self()->getRace().getWorker()) >= 8)
    {
        for (auto worker : Broodwar->self()->getUnits())
            if (worker->getType().isWorker() && worker->isCompleted() && !worker->isConstructing() &&
                !worker->isGatheringGas() && worker->getID() != buildTask_.workerId)
            { scoutWorkerId_ = worker->getID(); break; }
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
    Unitset combat;
    for (auto unit : Broodwar->self()->getUnits())
        if (unit->isCompleted() && !unit->getType().isBuilding() && !unit->getType().isWorker() &&
            (unit->getType().canAttack() || unit->getType() == UnitTypes::Protoss_High_Templar ||
             unit->getType() == UnitTypes::Protoss_Observer || unit->getType() == UnitTypes::Terran_Medic ||
             unit->getType() == UnitTypes::Terran_Science_Vessel)) combat.insert(unit);
    if (config_.schemaVersion == 2)
    {
        if (Broodwar->getFrameCount() >= config_.firstAttackFrame && combat.size() >= 4) attacking_ = true;
    }
    else if (static_cast<int>(combat.size()) >= config_.attackUnits) attacking_ = true;
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
    {
        if (unit->isAttackFrame() || Broodwar->getFrameCount() - unit->getLastCommandFrame() < 48) continue;
        Unit nearby = nullptr;
        int nearbyCount = 0;
        for (auto enemy : Broodwar->getAllUnits())
            if (enemy->isVisible() && enemy->getPlayer()->isEnemy(Broodwar->self()) && !enemy->isFlying() &&
                unit->getDistance(enemy) < 224) { nearby = enemy; ++nearbyCount; }
        if (unit->getType() == UnitTypes::Zerg_Lurker)
        {
            if (nearby && !unit->isBurrowed()) { unit->burrow(); continue; }
            if (!nearby && unit->isBurrowed()) { unit->unburrow(); continue; }
        }
        if (unit->getType() == UnitTypes::Terran_Siege_Tank_Tank_Mode && nearby &&
            Broodwar->self()->hasResearched(TechTypes::Tank_Siege_Mode)) { unit->siege(); continue; }
        if (unit->getType() == UnitTypes::Terran_Siege_Tank_Siege_Mode && !nearby) { unit->unsiege(); continue; }
        if (unit->getType() == UnitTypes::Protoss_High_Templar && nearbyCount >= 3 && unit->getEnergy() >= 75 &&
            Broodwar->self()->hasResearched(TechTypes::Psionic_Storm))
        { if (unit->useTech(TechTypes::Psionic_Storm, nearby->getPosition())) continue; }
        if (unit->getType().canAttack()) unit->attack(target);
        else if (unit->getDistance(target) > 160) unit->move(target);
    }
}

void PersonaBot::onFrame()
{
    if (!active_ || Broodwar->isPaused() || !Broodwar->self()) return;
    Broodwar->drawTextScreen(8, 8, "Persona: %s | samples %d | prototype", config_.name.c_str(), config_.sampleGames);
    if (Broodwar->getFrameCount() % std::max(6, Broodwar->getLatencyFrames()) != 0) return;
    economy();
    scout();
    if (config_.schemaVersion == 2) imitationProduction();
    else production();
    army();
}

void PersonaBot::onEnd(bool isWinner)
{
    if (!active_) return;
    std::ofstream log("bwapi-data/write/persona-results.txt", std::ios::app);
    log << config_.name << '\t' << (isWinner ? "win" : "loss") << '\t'
        << Broodwar->getFrameCount() << '\n';
}
