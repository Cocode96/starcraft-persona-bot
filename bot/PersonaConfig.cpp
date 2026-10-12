#include "PersonaConfig.h"

#include <map>
#include <set>
#include <stdexcept>

bool PersonaConfig::load(std::istream& input, std::string& error)
{
    PersonaConfig next;
    const std::map<std::string, int PersonaConfig::*> fields = {
        {"sample_games", &PersonaConfig::sampleGames},
        {"pool_frame", &PersonaConfig::poolFrame},
        {"hatchery_frame", &PersonaConfig::hatcheryFrame},
        {"extractor_frame", &PersonaConfig::extractorFrame},
        {"hydra_den_frame", &PersonaConfig::hydraDenFrame},
        {"attack_units", &PersonaConfig::attackUnits},
        {"worker_target", &PersonaConfig::workerTarget},
    };
    std::set<std::string> seen;
    std::string line;
    try
    {
        while (std::getline(input, line))
        {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty() || line.front() == '#') continue;
            const auto separator = line.find('=');
            if (separator == std::string::npos) throw std::runtime_error("missing =");
            const auto key = line.substr(0, separator);
            const auto value = line.substr(separator + 1);
            if (!seen.insert(key).second) throw std::runtime_error("duplicate key: " + key);
            if (key == "name")
            {
                if (value.empty() || value.size() > 64 || value.find_first_of("\r\n\t") != std::string::npos)
                    throw std::runtime_error("invalid name");
                next.name = value;
                continue;
            }
            std::size_t consumed = 0;
            const int number = std::stoi(value, &consumed);
            if (consumed != value.size() || number < 0 || number > 432000)
                throw std::runtime_error("invalid number: " + key);
            if (key == "schema_version")
            {
                if (number != 1) throw std::runtime_error("unsupported schema");
            }
            else
            {
                const auto field = fields.find(key);
                if (field == fields.end()) throw std::runtime_error("unknown key: " + key);
                next.*(field->second) = number;
            }
        }
        if (input.bad()) throw std::runtime_error("read failure");
        if (seen.size() != fields.size() + 2 || !seen.count("schema_version") || !seen.count("name"))
            throw std::runtime_error("incomplete profile");
        if (next.sampleGames < 1 || next.attackUnits < 2 || next.attackUnits > 100 ||
            next.workerTarget < 4 || next.workerTarget > 80)
            throw std::runtime_error("profile bounds exceeded");
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
