#ifndef slic3r_MaterialBehavior_hpp_
#define slic3r_MaterialBehavior_hpp_

#include <string>
#include <nlohmann/json.hpp>

namespace Slic3r {

// Material-behavior seed values, loaded once from
// resources_dir()/profiles_template/material_behavior.json.
// Single source of truth shared by the create-filament dialog and the
// CrealityPrint user-preset migration.
class MaterialBehavior
{
public:
    // Parsed table file; empty json object if the file is missing/broken.
    static const nlohmann::json &tables();

    // tables()[table_key][type] if present, else tables()[table_key+"_default"],
    // else `fallback`.
    static double lookup(const char *table_key, const std::string &type, double fallback);
    // True if tables()[table_key] (an object) has an entry for `type`.
    static bool   listed(const char *table_key, const std::string &type);
    // True if `type` appears in the string array tables()[list_key].
    static bool   contains(const char *list_key, const std::string &type);
};

} // namespace Slic3r

#endif
