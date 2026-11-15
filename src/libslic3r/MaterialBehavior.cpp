#include "MaterialBehavior.hpp"
#include "Utils.hpp"

#include <boost/log/trivial.hpp>
#include <boost/nowide/fstream.hpp>

namespace Slic3r {

const nlohmann::json &MaterialBehavior::tables()
{
    static nlohmann::json cached = [] {
        nlohmann::json j = nlohmann::json::object();
        std::string path = resources_dir() + "/profiles_template/material_behavior.json";
        try {
            boost::nowide::ifstream file(path);
            if (file.is_open())
                j = nlohmann::json::parse(file);
            else
                BOOST_LOG_TRIVIAL(error) << "material_behavior.json not found: " << path;
        } catch (const std::exception &err) {
            BOOST_LOG_TRIVIAL(error) << "material_behavior.json parse error: " << err.what();
            j = nlohmann::json::object();
        }
        return j;
    }();
    return cached;
}

double MaterialBehavior::lookup(const char *table_key, const std::string &type, double fallback)
{
    const nlohmann::json &j = tables();
    if (j.contains(table_key) && j[table_key].contains(type))
        return j[table_key][type].get<double>();
    std::string default_key = std::string(table_key) + "_default";
    if (j.contains(default_key))
        return j[default_key].get<double>();
    return fallback;
}

bool MaterialBehavior::listed(const char *table_key, const std::string &type)
{
    const nlohmann::json &j = tables();
    return j.contains(table_key) && j[table_key].contains(type);
}

bool MaterialBehavior::contains(const char *list_key, const std::string &type)
{
    const nlohmann::json &j = tables();
    if (!j.contains(list_key)) return false;
    for (const auto &entry : j[list_key])
        if (entry.is_string() && entry.get<std::string>() == type) return true;
    return false;
}

} // namespace Slic3r
