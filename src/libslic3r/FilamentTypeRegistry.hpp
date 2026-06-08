#ifndef slic3r_FilamentTypeRegistry_hpp_
#define slic3r_FilamentTypeRegistry_hpp_

#include <string>
#include <vector>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

#include "libslic3r/FDM/Filament.hpp" // Slic3r::FilamentTempType

namespace Slic3r {

// Central registry of filament-type metadata.
//
// Built-in types are loaded from resources/info/filament_info.json. The key idea for
// "arbitrary filament types" is base-type inheritance: a custom/unknown type (e.g.
// "PLA-Galaxy") resolves its behavior through a *base type* (e.g. "PLA") rather than
// falling through to Undefine. The base is taken from an explicit derived->base map in
// the json, or — when absent — inferred from the type name (the longest known base that
// is a prefix of the name, separated by -, _, space, + or .).
//
// Lookups are normalized (trimmed + upper-cased) so casing/whitespace don't matter.
//
// Phase 1 exposes temperature classification + base resolution; later phases extend the
// json schema and this class with adhesion, cooling, soluble/support, AMS, etc., all
// resolved through the same base-inheritance mechanism.
class FilamentTypeRegistry
{
public:
    static FilamentTypeRegistry& instance();

    // Temperature class for a (possibly custom) filament type; resolves via base type.
    FilamentTempType temp_type(const std::string& filament_type) const;

    // The canonical base a (possibly custom) type derives from: explicit mapping first,
    // else the longest known base that is a prefix of the (normalized) name, else "".
    std::string base_type(const std::string& filament_type) const;

    // The key to use when matching type-specific behavior. A type the registry recognizes
    // (a built-in: directly classified, or present in the explicit base map) returns itself,
    // so built-in behavior is preserved exactly; an unrecognized custom type returns its
    // inferred base so it inherits behavior. Returns a normalized (upper-case) string.
    std::string effective_type(const std::string& filament_type) const;

    // True when the type is a directly-known built-in (exact match, case/space-insensitive).
    bool is_known(const std::string& filament_type) const;

    // trim + upper-case, used for all matching. Public so callers can match consistently.
    static std::string normalize(const std::string& s);

private:
    FilamentTypeRegistry() = default;
    void ensure_loaded() const;
    void load() const;
    FilamentTempType classify(const std::string& normalized) const;

    mutable std::once_flag                              m_load_flag;
    mutable std::unordered_set<std::string>             m_high_temp;             // normalized
    mutable std::unordered_set<std::string>             m_low_temp;              // normalized
    mutable std::unordered_set<std::string>             m_high_low_compatible;   // normalized
    mutable std::unordered_map<std::string, std::string> m_explicit_base;        // normalized derived -> normalized base
    mutable std::vector<std::string>                    m_base_candidates;       // normalized, sorted longest-first
};

} // namespace Slic3r

#endif // slic3r_FilamentTypeRegistry_hpp_
