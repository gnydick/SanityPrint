#include "FilamentTypeRegistry.hpp"

#include "libslic3r/Utils.hpp" // resources_dir()

#include <algorithm>

#include <boost/algorithm/string/trim.hpp>
#include <boost/algorithm/string/case_conv.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/nowide/fstream.hpp>
#include <boost/log/trivial.hpp>
#include <nlohmann/json.hpp>

namespace Slic3r {

static inline bool is_type_separator(char c)
{
    return c == '-' || c == '_' || c == ' ' || c == '+' || c == '.';
}

FilamentTypeRegistry& FilamentTypeRegistry::instance()
{
    static FilamentTypeRegistry s_instance;
    return s_instance;
}

std::string FilamentTypeRegistry::normalize(const std::string& s)
{
    std::string n = s;
    boost::algorithm::trim(n);
    boost::algorithm::to_upper(n);
    return n;
}

void FilamentTypeRegistry::ensure_loaded() const
{
    std::call_once(m_load_flag, [this] { load(); });
}

void FilamentTypeRegistry::load() const
{
    namespace fs = boost::filesystem;
    using nlohmann::json;

    auto to_normalized_set = [](const std::vector<std::string>& v) {
        std::unordered_set<std::string> s;
        s.reserve(v.size());
        for (const auto& e : v)
            s.insert(normalize(e));
        return s;
    };

    // Hardcoded fallback, mirroring the previous behavior, used if the json is missing or
    // unparseable so classification never silently disappears.
    auto set_defaults = [this] {
        m_high_temp = {"ABS", "ASA", "PC", "PA", "PA-CF", "PA-GF", "PA6-CF", "PET-CF",
                       "PPS", "PPS-CF", "PPA-GF", "PPA-CF", "ABS-GF", "ASA-AERO"};
        m_low_temp  = {"PLA", "TPU", "PLA-CF", "PLA-AERO", "PVA", "BVOH"};
        m_high_low_compatible = {"HIPS", "PETG", "PCTG", "PE", "PP", "EVA", "PE-CF", "PP-CF", "PP-GF", "PHA"};
        m_base_candidates = {"PLA", "ABS", "ASA", "PETG", "PCTG", "PET", "PA", "PC", "PE",
                             "PP", "TPU", "HIPS", "PVA", "BVOH", "PPS", "PPA", "EVA", "PHA"};
    };

    try {
        const fs::path file_path = fs::path(resources_dir()) / "info" / "filament_info.json";
        boost::nowide::ifstream in(file_path.string());
        const json j = json::parse(in);

        m_high_temp           = to_normalized_set(j.at("high_temp_filament").get<std::vector<std::string>>());
        m_low_temp            = to_normalized_set(j.at("low_temp_filament").get<std::vector<std::string>>());
        m_high_low_compatible = to_normalized_set(j.at("high_low_compatible_filament").get<std::vector<std::string>>());

        // Optional: canonical base types used to infer the base of an unknown/custom type.
        if (j.contains("base_types"))
            for (const auto& e : j.at("base_types").get<std::vector<std::string>>())
                m_base_candidates.push_back(normalize(e));

        // Optional: explicit derived -> base mapping (overrides prefix inference).
        if (j.contains("base_type"))
            for (const auto& kv : j.at("base_type").items())
                m_explicit_base[normalize(kv.key())] = normalize(kv.value().get<std::string>());

        // If no explicit base list was provided, fall back to every known type as a candidate.
        if (m_base_candidates.empty()) {
            for (const auto* set : {&m_high_temp, &m_low_temp, &m_high_low_compatible})
                for (const auto& t : *set)
                    m_base_candidates.push_back(t);
        }
    } catch (const std::exception& err) {
        BOOST_LOG_TRIVIAL(error) << "FilamentTypeRegistry: failed to load filament_info.json (" << err.what()
                                 << "); using built-in defaults";
        set_defaults();
    }

    // Longest base first, so the most specific prefix wins (e.g. PCTG before PC).
    std::sort(m_base_candidates.begin(), m_base_candidates.end(),
              [](const std::string& a, const std::string& b) { return a.size() > b.size(); });
    m_base_candidates.erase(std::unique(m_base_candidates.begin(), m_base_candidates.end()), m_base_candidates.end());
}

FilamentTempType FilamentTypeRegistry::classify(const std::string& n) const
{
    // Order preserved from the original implementation: compatible wins over high/low.
    if (m_high_low_compatible.count(n)) return HighLowCompatible;
    if (m_high_temp.count(n))           return HighTemp;
    if (m_low_temp.count(n))            return LowTemp;
    return Undefine;
}

std::string FilamentTypeRegistry::base_type(const std::string& filament_type) const
{
    ensure_loaded();
    const std::string n = normalize(filament_type);
    if (n.empty())
        return {};

    // Explicit mapping wins.
    const auto it = m_explicit_base.find(n);
    if (it != m_explicit_base.end())
        return it->second;

    // Longest canonical base that equals n, or is a prefix of n followed by a separator.
    for (const auto& base : m_base_candidates) { // already sorted longest-first
        if (n == base)
            return base;
        if (n.size() > base.size() && n.compare(0, base.size(), base) == 0 && is_type_separator(n[base.size()]))
            return base;
    }
    return {};
}

std::string FilamentTypeRegistry::effective_type(const std::string& filament_type) const
{
    ensure_loaded();
    const std::string n = normalize(filament_type);
    // A type the registry recognizes (built-in) keeps its own identity, so built-in behavior
    // is preserved exactly; only unrecognized custom types fall through to their base.
    if (classify(n) != Undefine || m_explicit_base.count(n) != 0)
        return n;
    const std::string base = base_type(filament_type);
    return base.empty() ? n : base;
}

bool FilamentTypeRegistry::is_known(const std::string& filament_type) const
{
    ensure_loaded();
    return classify(normalize(filament_type)) != Undefine;
}

FilamentTempType FilamentTypeRegistry::temp_type(const std::string& filament_type) const
{
    ensure_loaded();
    const std::string n = normalize(filament_type);

    // 1) Direct classification of the type itself.
    const FilamentTempType direct = classify(n);
    if (direct != Undefine)
        return direct;

    // 2) Resolve through the base type (explicit mapping or inferred prefix).
    const std::string base = base_type(filament_type);
    if (!base.empty() && base != n)
        return classify(base);

    return Undefine;
}

} // namespace Slic3r
