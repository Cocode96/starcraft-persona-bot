#include "PersonaConfig.h"
#include "define_persona.h"
#include <algorithm>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>

namespace
{
    int number(const std::string& value, int maximum = 432000)
    {
        if (value.empty() || value.find_first_not_of("0123456789") != std::string::npos)
            throw std::runtime_error("invalid unsigned integer");
        const int result = std::stoi(value);
        if (result > maximum) throw std::runtime_error("integer exceeds bounds");
        return result;
    }
    std::vector<int> row(const std::string& value, std::size_t count)
    {
        std::vector<int> result;
        std::istringstream stream(value);
        std::string cell;
        while (std::getline(stream, cell, ',')) result.push_back(number(cell));
        if (result.size() != count || value.empty() || value.back() == ',') throw std::runtime_error("invalid row");
        return result;
    }
}

bool PersonaConfig::load(std::istream& input, std::string& error)
{
    PersonaConfig next;
    const std::map<std::string, int PersonaConfig::*> fields = {
        {"sample_games", &PersonaConfig::sampleGames}, {"pool_frame", &PersonaConfig::poolFrame},
        {"hatchery_frame", &PersonaConfig::hatcheryFrame}, {"extractor_frame", &PersonaConfig::extractorFrame},
        {"hydra_den_frame", &PersonaConfig::hydraDenFrame}, {"attack_units", &PersonaConfig::attackUnits},
        {"worker_target", &PersonaConfig::workerTarget}, {"first_attack_frame", &PersonaConfig::firstAttackFrame},
    };
    std::set<std::string> seen;
    std::set<std::pair<int, int>> weightKeys;
    std::string line;
    try
    {
        while (std::getline(input, line))
        {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty() || line.front() == '#') continue;
            const auto separator = line.find('=');
            if (separator == std::string::npos) throw std::runtime_error("missing =");
            const auto key = line.substr(0, separator), value = line.substr(separator + 1);
            if (key == "step")
            {
                auto values = row(value, 4);
                if (values[1] > 2 || values[2] > 227 || values[3] < 1 || values[3] > 64 ||
                    next.steps.size() >= kMaxPersonaSteps || (!next.steps.empty() && values[0] < next.steps.back().frame))
                    throw std::runtime_error("invalid or unordered step");
                next.steps.push_back({values[0], values[1], values[2], values[3]});
                continue;
            }
            if (key == "weight")
            {
                auto values = row(value, 3);
                if (values[0] > 3 || values[1] > 227 || values[2] < 1 || values[2] > 10000 ||
                    !weightKeys.insert({values[0], values[1]}).second || next.weights.size() >= 4 * 228)
                    throw std::runtime_error("invalid or duplicate unit weight");
                next.weights.push_back({values[0], values[1], values[2]});
                continue;
            }
            if (!seen.insert(key).second) throw std::runtime_error("duplicate key: " + key);
            if (key == "name")
            {
                if (value.empty() || value.size() > 64 || value.find_first_of("\r\n\t") != std::string::npos)
                    throw std::runtime_error("invalid name");
                next.name = value;
            }
            else if (key == "race")
            {
                if (value != "Zerg" && value != "Terran" && value != "Protoss") throw std::runtime_error("unknown race");
                next.race = value;
            }
            else if (key == "schema_version") next.schemaVersion = number(value, 2);
            else if (key == "seed") next.seed = number(value);
            else
            {
                const auto field = fields.find(key);
                if (field == fields.end()) throw std::runtime_error("unknown key: " + key);
                next.*(field->second) = number(value);
            }
        }
        if (input.bad()) throw std::runtime_error("read failure");
        const std::set<std::string> legacy = {"schema_version", "name", "sample_games", "pool_frame", "hatchery_frame",
            "extractor_frame", "hydra_den_frame", "attack_units", "worker_target"};
        const std::set<std::string> current = {"schema_version", "name", "race", "sample_games", "seed", "worker_target", "first_attack_frame"};
        if (next.schemaVersion == 1)
        {
            if (seen != legacy || !next.steps.empty() || !next.weights.empty()) throw std::runtime_error("incomplete legacy profile");
        }
        else if (next.schemaVersion == 2)
        {
            if (seen != current || next.steps.empty()) throw std::runtime_error("incomplete imitation profile");
            for (int phase = 0; phase < 4; ++phase)
                if (std::none_of(next.weights.begin(), next.weights.end(), [phase](const UnitWeight& w) { return w.phase == phase; }))
                    throw std::runtime_error("missing phase policy");
        }
        else throw std::runtime_error("unsupported schema");
        if (next.sampleGames < 1 || next.attackUnits < 2 || next.attackUnits > 100 ||
            next.workerTarget < 4 || next.workerTarget > 80) throw std::runtime_error("profile bounds exceeded");
        *this = next;
        error.clear();
        return true;
    }
    catch (const std::exception& exception)
    {
        error = exception.what();
        return false;
    }
}

int PersonaConfig::chooseUnit(int frame, const std::vector<int>& legalUnits, std::mt19937& random) const
{
    const int phase = static_cast<int>(std::count_if(kPersonaPhaseEnds.begin(), kPersonaPhaseEnds.end(),
        [frame](int end) { return frame >= end; }));
    std::vector<int> ids;
    std::vector<double> probabilities;
    for (const auto& entry : weights)
        if (entry.phase == phase && std::find(legalUnits.begin(), legalUnits.end(), entry.id) != legalUnits.end())
        {
            ids.push_back(entry.id);
            probabilities.push_back(static_cast<double>(entry.weight));
        }
    if (ids.empty()) return -1;
    std::discrete_distribution<std::size_t> choose(probabilities.begin(), probabilities.end());
    return ids[choose(random)];
}
