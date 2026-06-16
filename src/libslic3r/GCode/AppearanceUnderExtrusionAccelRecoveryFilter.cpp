#include "AppearanceUnderExtrusionAccelRecoveryFilter.hpp"
#include "InterestRegion.hpp"

#include "../GCode.hpp"
#include "../ExtrusionEntity.hpp"
#include "../GCodeReader.hpp"
#include "../GCodeWriter.hpp"
#include "../LocalesUtils.hpp"
#include "../Utils.hpp"
#include "../libslic3r.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <limits>
#include <optional>
#include <sstream>
#include <string_view>
#include <utility>
#include <vector>

namespace Slic3r {

namespace {

static inline std::string_view trim_left(std::string_view s)
{
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r' || s.front() == '\n'))
        s.remove_prefix(1);
    return s;
}

static inline std::string_view trim(std::string_view s)
{
    s = trim_left(s);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r' || s.back() == '\n'))
        s.remove_suffix(1);
    return s;
}

static inline bool starts_with(std::string_view s, std::string_view prefix)
{
    return s.size() >= prefix.size() && s.substr(0, prefix.size()) == prefix;
}

static inline bool is_ascii_digit(char c)
{
    return c >= '0' && c <= '9';
}

static inline bool is_ascii_alpha(char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

static std::vector<std::string> split_lines_keep_newline(const std::string& gcode)
{
    std::vector<std::string> out;
    out.reserve(std::count(gcode.begin(), gcode.end(), '\n') + 1);
    std::size_t start = 0;
    while (start < gcode.size()) {
        const auto pos = gcode.find('\n', start);
        if (pos == std::string::npos) {
            out.emplace_back(gcode.substr(start));
            break;
        }
        out.emplace_back(gcode.substr(start, pos - start + 1)); // keep newline
        start = pos + 1;
    }
    if (gcode.empty())
        out.emplace_back();
    return out;
}

static bool try_parse_unsigned(std::string_view s, unsigned int& out)
{
    s = trim(s);
    if (s.empty())
        return false;
    unsigned long long v = 0;
    for (char c : s) {
        if (c < '0' || c > '9')
            return false;
        v = v * 10ull + static_cast<unsigned long long>(c - '0');
        if (v > std::numeric_limits<unsigned int>::max())
            return false;
    }
    out = static_cast<unsigned int>(v);
    return true;
}

static bool try_parse_double(std::string_view s, double& out)
{
    s = trim(s);
    if (s.empty())
        return false;
    std::string tmp(s);
    char* endptr = nullptr;
    const double v = std::strtod(tmp.c_str(), &endptr);
    if (endptr == tmp.c_str())
        return false;
    out = v;
    return std::isfinite(out);
}

static bool try_parse_named_value(std::string_view line, std::string_view key, double& out)
{
    const std::size_t pos = line.find(key);
    if (pos == std::string_view::npos)
        return false;
    std::size_t p = pos + key.size();
    // consume optional spaces
    while (p < line.size() && (line[p] == ' ' || line[p] == '\t'))
        ++p;
    std::size_t end = p;
    while (end < line.size() && line[end] != ' ' && line[end] != '\t' && line[end] != '\r' && line[end] != '\n' && line[end] != ';')
        ++end;
    return try_parse_double(line.substr(p, end - p), out);
}

static bool try_parse_extrusion_role_marker(std::string_view raw_line, ExtrusionRole& out_role)
{
    std::string_view s = trim_left(raw_line);
    if (s.empty() || s.front() != ';')
        return false;
    s.remove_prefix(1);
    s = trim_left(s);

    // Numeric marker emitted when pressure equalizer is enabled.
    if (starts_with(s, "_EXTRUSION_ROLE:")) {
        s.remove_prefix(std::string_view("_EXTRUSION_ROLE:").size());
        unsigned int v = 0;
        if (!try_parse_unsigned(s, v))
            return false;
        out_role = static_cast<ExtrusionRole>(v);
        return true;
    }

    // Reserved tags (compatible / BBL): TYPE:...,  FEATURE: ...
    // We accept any prefix ending with ':' and try to parse the remainder as a role string.
    // Examples:
    //  ;TYPE:Outer wall
    //  ; FEATURE: Outer wall
    const std::size_t colon = s.find(':');
    if (colon == std::string_view::npos)
        return false;
    const std::string_view key = trim(s.substr(0, colon));
    if (key != "TYPE" && key != "FEATURE")
        return false;
    std::string_view role_str = s.substr(colon + 1);
    role_str                  = trim(role_str);
    out_role                  = ExtrusionEntity::string_to_role(role_str);
    return out_role != erNone;
}

static std::string strip_feedrate_from_motion_cmd(const std::string& raw_line)
{
    // Keep newline as-is.
    std::string_view line(raw_line);
    std::string      newline;
    if (line.size() >= 2 && line.substr(line.size() - 2) == "\r\n") {
        newline = "\r\n";
        line.remove_suffix(2);
    } else if (!line.empty() && line.back() == '\n') {
        newline = "\n";
        line.remove_suffix(1);
    }

    const std::size_t comment_pos = line.find(';');
    std::string_view  main        = (comment_pos == std::string_view::npos) ? line : line.substr(0, comment_pos);
    std::string_view  comment     = (comment_pos == std::string_view::npos) ? std::string_view() : line.substr(comment_pos);

    // Parse a motion command prefix like "G1" even if it is immediately followed by parameters (e.g. "G1F6000").
    std::string_view sv = trim_left(main);
    if (sv.empty())
        return raw_line;
    if (sv.front() != 'G' && sv.front() != 'g')
        return raw_line;
    std::size_t p = 1;
    if (p >= sv.size() || !is_ascii_digit(sv[p]))
        return raw_line;
    unsigned int gcode = 0;
    while (p < sv.size() && is_ascii_digit(sv[p])) {
        gcode = gcode * 10u + static_cast<unsigned int>(sv[p] - '0');
        ++p;
    }
    if (!(gcode == 0u || gcode == 1u || gcode == 2u || gcode == 3u))
        return raw_line;

    std::string_view cmd  = sv.substr(0, p);
    std::string_view rest = sv.substr(p);

    std::vector<std::string> kept;
    kept.reserve(16);
    kept.emplace_back(cmd);

    bool        removed_f = false;
    std::size_t i         = 0;
    while (i < rest.size()) {
        while (i < rest.size() && (rest[i] == ' ' || rest[i] == '\t'))
            ++i;
        if (i >= rest.size())
            break;

        const char axis = rest[i];
        if (!is_ascii_alpha(axis))
            return raw_line;
        ++i;

        while (i < rest.size() && (rest[i] == ' ' || rest[i] == '\t'))
            ++i;

        const std::size_t value_start = i;
        while (i < rest.size()) {
            const char c = rest[i];
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == ';')
                break;
            if (is_ascii_alpha(c))
                break;
            ++i;
        }

        std::string_view value = rest.substr(value_start, i - value_start);
        value                  = trim(value);
        if (value.empty())
            return raw_line;

        if (axis == 'F' || axis == 'f') {
            removed_f = true;
            continue;
        }

        std::string tok;
        tok.reserve(1 + value.size());
        tok.push_back(axis);
        tok.append(value.data(), value.size());
        kept.push_back(std::move(tok));
    }

    if (!removed_f)
        return raw_line;

    // If it was a pure feedrate command (e.g. "G1 F..." or "G1F..."), drop it (but keep comment-only line if any).
    if (kept.size() == 1) {
        if (comment.empty())
            return std::string();
        std::string out;
        out.reserve(comment.size() + newline.size());
        out.append(comment.data(), comment.size());
        out += newline;
        return out;
    }

    std::string out;
    out.reserve(raw_line.size());
    for (std::size_t k = 0; k < kept.size(); ++k) {
        if (k > 0)
            out.push_back(' ');
        out.append(kept[k]);
    }
    if (!comment.empty()) {
        if (!out.empty())
            out.push_back(' ');
        out.append(comment.data(), comment.size());
    }
    out += newline;
    return out;
}

static bool is_blank_or_comment_only_line(std::string_view raw_line)
{
    // Strip trailing newline(s)
    while (!raw_line.empty() && (raw_line.back() == '\n' || raw_line.back() == '\r'))
        raw_line.remove_suffix(1);

    // Trim whitespace
    auto is_ws = [](unsigned char c) { return std::isspace(c) != 0; };
    while (!raw_line.empty() && is_ws(static_cast<unsigned char>(raw_line.front())))
        raw_line.remove_prefix(1);
    while (!raw_line.empty() && is_ws(static_cast<unsigned char>(raw_line.back())))
        raw_line.remove_suffix(1);

    if (raw_line.empty())
        return true;
    // Comment-only line (";" or "(...)" style)
    return raw_line.front() == ';' || raw_line.front() == '(';
}

static bool is_pure_feedrate_motion_cmd(std::string_view raw_line)
{
    // Strip trailing newline(s)
    while (!raw_line.empty() && (raw_line.back() == '\n' || raw_line.back() == '\r'))
        raw_line.remove_suffix(1);

    // Trim leading whitespace
    auto is_ws = [](unsigned char c) { return std::isspace(c) != 0; };
    while (!raw_line.empty() && is_ws(static_cast<unsigned char>(raw_line.front())))
        raw_line.remove_prefix(1);

    if (raw_line.empty())
        return false;
    if (raw_line.front() == ';' || raw_line.front() == '(')
        return false;

    // Drop trailing comment part.
    const size_t semi = raw_line.find(';');
    std::string_view code = (semi == std::string_view::npos) ? raw_line : raw_line.substr(0, semi);
    while (!code.empty() && is_ws(static_cast<unsigned char>(code.back())))
        code.remove_suffix(1);
    if (code.empty())
        return false;

    // Optional line number: N123 ...
    if (!code.empty() && (code.front() == 'N' || code.front() == 'n')) {
        size_t i = 1;
        while (i < code.size() && std::isdigit(static_cast<unsigned char>(code[i])))
            ++i;
        while (i < code.size() && is_ws(static_cast<unsigned char>(code[i])))
            ++i;
        code = (i < code.size()) ? code.substr(i) : std::string_view();
        if (code.empty())
            return false;
    }

    if (code.empty() || (code.front() != 'G' && code.front() != 'g'))
        return false;
    size_t i = 1;
    if (i >= code.size() || !std::isdigit(static_cast<unsigned char>(code[i])))
        return false;
    int gcode = 0;
    while (i < code.size() && std::isdigit(static_cast<unsigned char>(code[i]))) {
        gcode = gcode * 10 + (code[i] - '0');
        ++i;
    }
    if (!(gcode == 0 || gcode == 1 || gcode == 2 || gcode == 3))
        return false;

    bool has_f = false;
    bool has_other = false;
    while (i < code.size()) {
        // Skip spaces and non-alpha separators
        while (i < code.size() && !std::isalpha(static_cast<unsigned char>(code[i])))
            ++i;
        if (i >= code.size())
            break;
        const char letter = static_cast<char>(std::toupper(static_cast<unsigned char>(code[i])));
        ++i;
        if (letter == 'F')
            has_f = true;
        else
            has_other = true;
        // Skip value (until next alpha)
        while (i < code.size() && !std::isalpha(static_cast<unsigned char>(code[i])))
            ++i;
    }

    return has_f && !has_other;
}

static size_t extend_safe_begin_line(const std::vector<std::string>& lines, size_t defect_line)
{
    // Include any immediately preceding "pure feedrate" commands (often emitted by cooling) and any blank/comment-only
    // lines between them and the first L_safe motion line.
    size_t begin = defect_line;
    while (begin > 0) {
        const std::string& prev = lines[begin - 1];
        if (is_blank_or_comment_only_line(prev) || is_pure_feedrate_motion_cmd(prev))
            --begin;
        else
            break;
    }
    return begin;
}

static bool try_parse_object_id_marker(std::string_view raw, int& out_object_id)
{
    std::string_view sv = trim_left(raw);
    constexpr std::string_view prefix = "; OBJECT_ID:";
    if (sv.size() < prefix.size() || sv.substr(0, prefix.size()) != prefix)
        return false;
    sv.remove_prefix(prefix.size());
    while (!sv.empty() && (sv.front() == ' ' || sv.front() == '\t'))
        sv.remove_prefix(1);
    int  v = 0;
    bool has_digit = false;
    while (!sv.empty()) {
        const unsigned char c = static_cast<unsigned char>(sv.front());
        if (c < '0' || c > '9')
            break;
        has_digit = true;
        v = v * 10 + int(c - '0');
        sv.remove_prefix(1);
    }
    if (!has_digit)
        return false;
    out_object_id = v;
    return true;
}


struct Motion
{
    enum class Type : unsigned char
    {
        G0,
        G1,
        G2,
        G3,
    };

    size_t       line_idx{ 0 };
    Type         type{ Type::G1 };
    bool         extruding{ false };
    ExtrusionRole role{ erNone };
    Vec3d        start{ Vec3d::Zero() };
    Vec3d        end{ Vec3d::Zero() };
    double       length_mm{ 0.0 }; // Path length in mm. For arcs (G2/G3 with I/J), this is the 3D arc length (includes Z delta).
    double       feedrate_mm_s{ 0.0 };
    double       feedrate_mm_min{ 0.0 };
    double       accel_mm_s2{ 0.0 };
    int          object_id{ -1 };

    // For extrusion split
    double e_start_abs{ 0.0 };
    double e_end_abs{ 0.0 };
    double delta_e{ 0.0 };

    // Arc specific (I/J only)
    bool   is_arc{ false };
    bool   arc_ccw{ true };
    Vec2d  arc_center{ Vec2d::Zero() };
    double arc_sweep_rad{ 0.0 }; // signed, matches direction
};

struct ObjectSpan
{
    size_t trigger_begin{ 0 };
    size_t trigger_end{ 0 };
    size_t defect_begin{ 0 };
    size_t defect_end{ 0 };
    int    object_id{ -1 };
};

struct Plan
{
    ObjectSpan span;

    // Where to start applying velocity_safe (end of L_safe_transition).
    bool   has_velocity_safe{ false };
    size_t velocity_safe_motion{ 0 };
    double velocity_safe_fraction{ 0.0 }; // within velocity_safe_motion, [0,1]

    // Where to recover accel/feedrate back to the "real" target.
    size_t boundary_motion{ 0 };
    double boundary_fraction{ 1.0 }; // within boundary_motion, (0,1]

    double accel_safe{ 0.0 };
    double accel_recovered{ 0.0 };
    double velocity_safe_mm_s{ 0.0 };
    double velocity_recovered_mm_s{ 0.0 };
    double L_safe_transition_mm{ 0.0 };
    double L_safe_accel_mm{ 0.0 };
    double L_safe_cruise_mm{ 0.0 };
    double L_safe_total_mm{ 0.0 };
};

static double clamp_non_negative(double v)
{
    return (std::isfinite(v) && v > 0.0) ? v : 0.0;
}

static double compute_length_mm(const Motion& m)
{
    if (m.is_arc) {
        // Arc case: Motion::length_mm already contains the 3D arc length (including Z delta).
        return m.length_mm;
    }
    return m.length_mm;
}

// Build the AUE buffer plan (Plan) for a detected ROI object (Trigger + Defect).
//
// span semantics:
// - span.* are indices into motions[], all closed intervals [begin, end] (both ends inclusive).
//
// Key velocities/accelerations:
// - v_low  : feedrate of the Trigger's last motion (trigger_last.feedrate_mm_s).
// - v_safe : the user-set safe upper speed limit (params.velocity_safe_mm_s).
// - a_safe : the user-set safe upper acceleration limit (params.accel_safe).
// - v_rec  : the Defect's "real speed to recover to" (defect_last.feedrate_mm_s), i.e. the last effective speed setting in that segment.
//           Note: a standalone "G1 F..." does not generate a Motion, but it updates the parser state.feedrate,
//           so it affects the feedrate of subsequent motions and is ultimately reflected in defect_last.feedrate_mm_s.
// - accel_recovered: the acceleration to restore at the recovery point; prefer defect_first.accel_mm_s2, falling back to trigger_last.accel_mm_s2 on failure.
//
// safe-segment length model:
// - L_transition: the transition distance over which v_low is held after defect_begin (params.L_safe_transition_mm).
//   Purpose: slightly extend the low-speed segment after a corner/deceleration, to avoid triggering acceleration/speed-up too early.
// - L_accel: under constant acceleration a_safe, the theoretical distance to accelerate from v_low to v_safe:
//     L_accel = (v_safe^2 - v_low^2) / (2*a_safe) (only when v_safe > v_low)
//   This value is only used to determine "how much distance is ideally needed"; it does not split the safe segment into separate accel/cruise phases;
//   whether the firmware planner can actually reach v_safe within the safe segment depends on the Defect segment's real available path length.
// - L_total = L_transition + L_accel + L_safe_cruise_mm: the total length over which we want to maintain the safe limits along the Defect path.
//
// Determining the boundary (recovery point):
// - Accumulate the geometric length of motions along the defect span; the first position that reaches L_total is the recovery boundary:
//   - boundary_motion   : the Motion containing the recovery boundary (index into motions[])
//   - boundary_fraction : the boundary's position ratio within that Motion (0,1]; 1.0 means it falls at the motion's end (no split needed)
// - If the Defect's total length < L_total, the boundary is clamped to defect_end (fraction=1.0); the safe segment ends early and recovery happens at the Defect's end.
//   This covers three kinds of insufficient distance:
//   - Defect total length < L_transition: even the transition cannot complete; v_safe is not inserted (the whole segment stays at v_low), and recovery happens at defect_end.
//   - L_transition <= Defect total length < L_transition + L_accel: the theoretical acceleration distance is insufficient; the planner may not reach v_safe (no cruise exists).
//   - L_transition + L_accel <= Defect total length < L_total: v_safe can be reached, but the cruise length does not reach the configured value.
//   The current implementation does not distinguish the latter two cases; both recover at defect_end.

static std::optional<Plan> build_plan(const ObjectSpan& span, const std::vector<Motion>& motions, const AppearanceUnderExtrusionAccelRecoveryConfig& params)
{
    if (span.trigger_end >= motions.size() || span.defect_begin >= motions.size() || span.defect_end >= motions.size())
        return std::nullopt;
    if (!(params.accel_safe > 0.0f) || params.velocity_safe_mm_s <= 0.0f)
        return std::nullopt;

    const Motion& trigger_last = motions[span.trigger_end];
    const Motion& defect_first = motions[span.defect_begin];
    const Motion& defect_last  = motions[span.defect_end];

    const double v_low  = clamp_non_negative(trigger_last.feedrate_mm_s);
    const double v_safe = clamp_non_negative(params.velocity_safe_mm_s);
    const double a_safe = clamp_non_negative(static_cast<double>(params.accel_safe));
    if (a_safe <= 0.0)
        return std::nullopt;

    // Recover to the "real" speed at the end of the defect segment (last set feedrate in effect).
    const double v_rec = clamp_non_negative(defect_last.feedrate_mm_s);
    if (v_rec <= 0.0)
        return std::nullopt;

    const double accel_recovered = clamp_non_negative(defect_first.accel_mm_s2) > 0.0 ? defect_first.accel_mm_s2 : trigger_last.accel_mm_s2;
    if (!(accel_recovered > 0.0))
        return std::nullopt;

    // Acceleration distance from v_low -> v_safe, under constant accel a_safe.
    double L_accel = 0.0;
    if (v_safe > v_low) {
        L_accel = (v_safe * v_safe - v_low * v_low) / (2.0 * a_safe);
        if (!std::isfinite(L_accel) || L_accel < 0.0)
            L_accel = 0.0;
    }

    const double L_transition = clamp_non_negative(params.L_safe_transition_mm);

    // Find where transition ends (where we start applying velocity_safe).
    bool   has_velocity_safe = false;
    size_t velocity_safe_motion = span.defect_begin;
    double velocity_safe_fraction = 0.0;
    if (L_transition <= 0.0) {
        has_velocity_safe = true;
        velocity_safe_motion = span.defect_begin;
        velocity_safe_fraction = 0.0;
    } else {
        double acc = 0.0;
        for (size_t i = span.defect_begin; i <= span.defect_end && i < motions.size(); ++i) {
            const double len = clamp_non_negative(compute_length_mm(motions[i]));
            if (len <= 0.0)
                continue;
            if (acc + len >= L_transition) {
                has_velocity_safe = true;
                velocity_safe_motion = i;
                const double remain = L_transition - acc;
                velocity_safe_fraction = std::clamp(remain / len, 0.0, 1.0);
                break;
            }
            acc += len;
        }
    }

    const double L_cruise = clamp_non_negative(params.L_safe_cruise_mm);
    const double L_total  = L_transition + L_accel + L_cruise;
    if (!(L_total > 0.0))
        return std::nullopt;

    // Find recover boundary within defect span.
    double acc = 0.0;
    size_t boundary = span.defect_begin;
    double fraction = 1.0;
    for (size_t i = span.defect_begin; i <= span.defect_end && i < motions.size(); ++i) {
        const double len = clamp_non_negative(compute_length_mm(motions[i]));
        if (len <= 0.0)
            continue;
        if (acc + len >= L_total) {
            boundary = i;
            const double remain = L_total - acc;
            fraction            = std::clamp(remain / len, 0.0, 1.0);
            break;
        }
        acc += len;
        boundary = i;
        fraction = 1.0;
    }

    Plan p;
    p.span                   = span;
    p.has_velocity_safe       = has_velocity_safe;
    p.velocity_safe_motion    = velocity_safe_motion;
    p.velocity_safe_fraction  = velocity_safe_fraction;
    p.boundary_motion         = boundary;
    p.boundary_fraction       = fraction;
    p.accel_safe              = a_safe;
    p.accel_recovered         = accel_recovered;
    p.velocity_safe_mm_s      = v_safe;
    p.velocity_recovered_mm_s = v_rec;
    p.L_safe_transition_mm     = L_transition;
    p.L_safe_accel_mm          = L_accel;
    p.L_safe_cruise_mm         = L_cruise;
    p.L_safe_total_mm          = L_total;
    return p;
}

static std::string make_set_velocity_limit_accel(double accel, const char* comment)
{
    std::ostringstream oss;
    oss << "SET_VELOCITY_LIMIT ACCEL=" << float_to_string_decimal_point(accel);
    if (comment && *comment)
        oss << " ; " << comment;
    oss << "\n";
    return oss.str();
}

static std::string make_set_feedrate_mm_s(double v_mm_s, const char* comment)
{
    const double f_mm_min = v_mm_s * 60.0;
    GCodeG1Formatter f;
    f.emit_f(f_mm_min);
    f.emit_comment(true, comment ? comment : "");
    return f.string();
}

static std::string format_linear_move(const Vec3d& end, double e_value, bool use_relative_e, const char* comment)
{
    GCodeG1Formatter g1;
    g1.emit_xyz(end);
    if (use_relative_e)
        g1.emit_e(e_value);
    else
        g1.emit_e(e_value);
    g1.emit_comment(true, comment ? comment : "");
    return g1.string();
}

static std::string format_arc_move(bool ccw, const Vec3d& end, const Vec2d& ij, double e_value, bool use_relative_e, const char* comment)
{
    GCodeG2G3Formatter g23(ccw);
    g23.emit_xyz(end);
    g23.emit_ij(ij);
    if (use_relative_e)
        g23.emit_e(e_value);
    else
        g23.emit_e(e_value);
    g23.emit_comment(true, comment ? comment : "");
    return g23.string();
}

} // namespace

struct AppearanceUnderExtrusionAccelRecoveryFilter::State
{
    Vec3d         pos{ Vec3d::Zero() };
    double        e_abs{ 0.0 };
    double        feedrate_mm_min{ 0.0 };
    double        accel_mm_s2{ 0.0 };
    ExtrusionRole role{ erNone };
    bool          use_relative_e{ true };
    int           object_id{ -1 };
};

AppearanceUnderExtrusionAccelRecoveryFilter::AppearanceUnderExtrusionAccelRecoveryFilter(const GCodeConfig& config,
                                                                                       const PrintObjectConfig& default_object_config,
                                                                                       std::unordered_map<int, ObjectParams> object_params_by_id,
                                                                                       GCodeFlavor flavor)
    : m_object_params_by_id(std::move(object_params_by_id))
    , m_config(config)
    , m_flavor(flavor)
    , m_state(std::make_unique<State>())
{
    m_params.enabled    = default_object_config.msao_recovery_enable.value;
    m_params.accel_safe = static_cast<float>(default_object_config.msao_safe_accel.value);
    m_params.velocity_safe_mm_s = static_cast<float>(default_object_config.msao_safe_velocity.value);
    m_state->use_relative_e = config.use_relative_e_distances.value;
}

AppearanceUnderExtrusionAccelRecoveryFilter::~AppearanceUnderExtrusionAccelRecoveryFilter() = default;

void AppearanceUnderExtrusionAccelRecoveryFilter::reset()
{
    if (!m_state)
        m_state = std::make_unique<State>();
    *m_state = State{};
    m_state->use_relative_e = m_config.use_relative_e_distances.value;
}

LayerResult AppearanceUnderExtrusionAccelRecoveryFilter::process_layer(LayerResult&& input)
{
    if (input.nop_layer_result)
        return std::move(input);
    if (m_flavor != gcfKlipper)
        return std::move(input);
    if (!m_params.enabled) {
        bool any_enabled = false;
        for (const auto& kv : m_object_params_by_id) {
            if (kv.second.enabled) {
                any_enabled = true;
                break;
            }
        }
        if (!any_enabled)
            return std::move(input);
    }
    if (!m_state)
        m_state = std::make_unique<State>();
    input.gcode = apply_to_gcode_layer(std::move(input.gcode), m_params, m_config, m_flavor, m_object_params_by_id, *m_state);
    return std::move(input);
}

// AUE (Appearance Under-Extrusion) acceleration/velocity recovery buffer (single-layer G-code rewrite).
//
// Background problem:
// - The typical scenario is overhangs/bridges: the overhang segment triggers cooling/overhang slowdown, printing at low speed continuously for a while;
// - When speed recovers to normal (relatively higher), an under-extrusion appearance is commonly seen.
//   This kind of under-extrusion is more like "extrusion can't keep up due to a velocity/acceleration transient mismatch" rather than a sustained flow-rate ceiling shortfall.
//
// What this function does (core idea):
// - First locate within the layer a "Trigger (low-speed trigger segment)" and the following "Defect (suspected under-extrusion segment)", together called the ROI;
// - Then insert a set of Klipper commands around the Trigger/Defect to limit acceleration/velocity to "safe values" over a short distance,
//   and then recover to the "real target velocity/acceleration" at some boundary within the Defect segment, thereby mitigating the low->high speed switching transient.
//
// 1) Parsing phase: build the Motion list from the raw layer gcode (used for distance, velocity, E splitting, etc.)
// - Parse G0/G1/G2/G3 line by line, tracking:
//   - XYZ/E position (used to compute move length, and to split E when splitting a move at a boundary)
//   - the currently effective feedrate (F, mm/min) and accel (from SET_VELOCITY_LIMIT ACCEL=...)
//   - extrusion role (from comment markers, e.g. ";_EXTERNAL_PERIMETER"), used for ROI role filtering
// - Note: a "pure speed-setting command" like "G1 F..." is not recorded as a Motion, but it updates state.feedrate_mm_min,
//   so that the later defect_last.feedrate_mm_s can represent the "real terminal speed (the last feedrate that was set)".
//
// 2) ROI detection: reuse InterestRegion's detection logic directly on motions[] (no second GCodeProcessor run)
// - The ROI detection parameters come from the defaults of InterestRegion::AppearanceUnderExtrusionDefinition (consistent with the GUI preview cache), to avoid the two default sets drifting apart.
// - InterestRegion::detect_appearance_under_extrusion_interest_region(motions, ...) produces an
//   "AppearanceUnderExtrusion" object containing two spans:
//   - Trigger: a sustained low-speed extrusion segment (speed <= max_trigger_speed_mm_s and duration >= min_trigger_time_s)
//   - Defect: the extrusion segment after the Trigger that satisfies defect_roles (default: external perimeter + overhang external perimeter)
// - SegmentSpan uses end-ssid indexing (meaning "the segment from end_ssid-1 to end_ssid"). For the motions[] view:
//   end_ssid = N corresponds directly to motions[N-1], so the span -> motion index mapping is an O(1) subtract-one operation.
//
// 3) Plan generation: place the "safe buffer" boundary at some position along the Defect path
// - v_low: the speed of the Trigger's last motion (trigger_last.feedrate_mm_s)
// - v_safe/a_safe: user parameters (velocity_safe_mm_s / accel_safe)
// - v_recovered: the Defect's last motion "real speed" (defect_last.feedrate_mm_s, i.e. the most recently set feedrate)
// - L_safe_transition_mm: the transition distance over which v_low is held after defect_begin (default 1mm), used to slightly extend the low-speed segment after a corner/deceleration.
// - Compute the theoretical acceleration distance (from v_low up to v_safe, constant acceleration a_safe):
//     L_safe_accel = (v_safe^2 - v_low^2) / (2*a_safe)  (when v_safe > v_low)
//   then define the total safe length:
//     L_safe_total = L_safe_transition_mm + L_safe_accel + L_safe_cruise_mm
// - Accumulate the geometric length of motions within the Defect span and find the first position reaching L_safe_total as the boundary:
//   - If the boundary falls inside some motion, record boundary_fraction (that motion will be split later)
//   - If the Defect's total available length < L_safe_total, the boundary is "clamped to defect_end", meaning the safe segment ends early
//     (in this case the firmware planner may not be able to reach v_safe at all, manifesting as L_safe_accel being "truncated" by insufficient distance)
//
// 4) G-code rewrite: insert safe/recover, and ensure L_safe is affected only by accel_safe and velocity_safe
// - Before the trigger_begin line, insert: SET_VELOCITY_LIMIT ACCEL=accel_safe
// - At transition_end, insert: G1 F(v_safe)   (sets feedrate only, no XYZ/E)
// - For all lines within the L_safe segment:
//   - Remove any leftover/upstream-injected F token (including "G1 X.. F..", "G1F..", and a standalone "G1 F..")
//   - Purpose: ensure the L_safe segment is controlled only by the accel_safe and v_safe we inserted, avoiding modules like CoolingBuffer
//     inserting additional speed-setting commands in the same interval and making the "safe segment impure"
//   - Extra handling: Cooling sometimes inserts a standalone "G1 F..." before the first motion command of the safe segment,
//     so the start of the safe segment is extended backward, cleaning up the immediately adjacent pure-speed-setting/blank/comment lines as well
// - At the boundary, insert recovery commands:
//     SET_VELOCITY_LIMIT ACCEL=accel_recovered
//     G1 F(v_recovered)
//   - If transition_end / boundary is in the middle of a motion: split that G1/G2/G3 (possibly two splits: transition_end + boundary),
//     and insert v_safe / recover at the split points; the emitted segmented moves carry no F.
//
// Notes:
// - The current implementation only takes effect for Klipper (relies on SET_VELOCITY_LIMIT).
// - This module is "text rewrite + inserting control commands"; the actual accel/decel curve is decided by the firmware planner.
// - The reason this filter is recommended to be placed after write_gcode and before fan_mover is to prevent later modules from re-injecting F/speed-limit commands,
//   which would break the L_safe segment's constraint of "being controlled only by accel_safe/velocity_safe".
std::string AppearanceUnderExtrusionAccelRecoveryFilter::apply_to_gcode_layer(std::string&& gcode,
                                                                              const AppearanceUnderExtrusionAccelRecoveryConfig& params,
                                                                              const GCodeConfig& config,
                                                                              GCodeFlavor flavor,
                                                                              const std::unordered_map<int, ObjectParams>& object_params_by_id,
                                                                              State& state)
{
    if (flavor != gcfKlipper)
        return std::move(gcode);
    if (gcode.empty())
        return std::move(gcode);
    if (!params.enabled) {
        bool any_enabled = false;
        for (const auto& kv : object_params_by_id) {
            if (kv.second.enabled) {
                any_enabled = true;
                break;
            }
        }
        if (!any_enabled)
            return std::move(gcode);
    }
    if (params.velocity_safe_mm_s <= 0.0f) {
        bool any_has_velocity = false;
        for (const auto& kv : object_params_by_id) {
            if (kv.second.enabled && kv.second.velocity_safe_mm_s > 0.0f) {
                any_has_velocity = true;
                break;
            }
        }
        if (!any_has_velocity)
            return std::move(gcode);
    }

    state.object_id = -1;


    // Parse lines and build motion list.
    std::vector<std::string> lines = split_lines_keep_newline(gcode);
    std::vector<int>         line_to_motion(lines.size(), -1);
    std::vector<Motion>      motions;
    motions.reserve(lines.size() / 2);

    GCodeReader parser;
    parser.apply_config(config);

    for (size_t li = 0; li < lines.size(); ++li) {
        const std::string& raw = lines[li];

        // Update role markers from comment-only lines.
        int object_id_marker = -1;
        if (try_parse_object_id_marker(raw, object_id_marker)) {
            state.object_id = object_id_marker;
            continue;
        }

        ExtrusionRole role_marker = erNone;
        if (try_parse_extrusion_role_marker(raw, role_marker))
            state.role = role_marker;

        // Extract cmd quickly.
        std::string_view sv(raw);
        sv = trim_left(sv);
        if (sv.empty() || sv.front() == ';')
            continue;

        const std::size_t cmd_end = sv.find_first_of(" \t;\r\n");
        const std::string_view cmd = (cmd_end == std::string_view::npos) ? sv : sv.substr(0, cmd_end);

        // Acceleration tracking (Klipper).
        if (cmd == "SET_VELOCITY_LIMIT") {
            double accel = 0.0;
            if (try_parse_named_value(sv, "ACCEL=", accel) && accel > 0.0 && std::isfinite(accel))
                state.accel_mm_s2 = accel;
            continue;
        }

        // Parse G-code axes for motion / G92, update state positions for length estimation.
        parser.parse_line(raw, [&](GCodeReader&, const GCodeReader::GCodeLine& line) {
            const std::string_view lcmd = line.cmd();

            if (lcmd == "G92") {
                if (line.has(X))
                    state.pos.x() = line.value(X);
                if (line.has(Y))
                    state.pos.y() = line.value(Y);
                if (line.has(Z))
                    state.pos.z() = line.value(Z);
                if (line.has(E))
                    state.e_abs = state.use_relative_e ? 0.0 : static_cast<double>(line.value(E));
                return;
            }

            const bool is_g0 = (lcmd == "G0");
            const bool is_g1 = (lcmd == "G1");
            const bool is_g2 = (lcmd == "G2");
            const bool is_g3 = (lcmd == "G3");
            if (!is_g0 && !is_g1 && !is_g2 && !is_g3)
                return;

            Vec3d start = state.pos;
            Vec3d end   = state.pos;
            if (line.has(X))
                end.x() = line.value(X);
            if (line.has(Y))
                end.y() = line.value(Y);
            if (line.has(Z))
                end.z() = line.value(Z);

            double feedrate_mm_min = state.feedrate_mm_min;
            if (line.has(F))
                feedrate_mm_min = static_cast<double>(line.value(F));
            const double feedrate_mm_s = feedrate_mm_min / 60.0;

            double delta_e = 0.0;
            double e_start = state.e_abs;
            double e_end   = state.e_abs;
            if (line.has(E)) {
                const double e_val = static_cast<double>(line.value(E));
                if (state.use_relative_e) {
                    delta_e = e_val;
                    e_end   = e_start + delta_e;
                } else {
                    e_end   = e_val;
                    delta_e = e_end - e_start;
                }
            }

            const bool has_geom_motion = ((end - start).norm() > 1e-9);
            const bool has_e_motion    = (std::fabs(delta_e) > 0.0);
            if (!has_geom_motion && !has_e_motion) {
                // e.g. "G1 F..." only.
                if (line.has(F))
                    state.feedrate_mm_min = feedrate_mm_min;
                return;
            }

            Motion m;
            m.line_idx          = li;
            m.type              = is_g0 ? Motion::Type::G0 : is_g1 ? Motion::Type::G1 : is_g2 ? Motion::Type::G2 : Motion::Type::G3;
            m.role              = state.role;
            m.start             = start;
            m.end               = end;
            m.feedrate_mm_min   = feedrate_mm_min;
            m.feedrate_mm_s     = feedrate_mm_s;
            m.accel_mm_s2       = state.accel_mm_s2;
            m.object_id         = state.object_id;
            m.e_start_abs       = e_start;
            m.e_end_abs         = e_end;
            m.delta_e           = delta_e;
            m.extruding         = (delta_e > 0.0);

            if (is_g2 || is_g3) {
                m.is_arc  = true;
                m.arc_ccw = is_g3;
                if (line.has(I) && line.has(J)) {
                    const Vec2d ij(static_cast<double>(line.value(I)), static_cast<double>(line.value(J)));
                    m.arc_center = Vec2d(start.x(), start.y()) + ij;
                    const Vec2d sxy(start.x(), start.y());
                    const Vec2d exy(end.x(), end.y());
                    const double r = (sxy - m.arc_center).norm();
                    if (r > 0.0) {
                        const double a0 = std::atan2(sxy.y() - m.arc_center.y(), sxy.x() - m.arc_center.x());
                        const double a1 = std::atan2(exy.y() - m.arc_center.y(), exy.x() - m.arc_center.x());
                        double sweep     = a1 - a0;
                        if (m.arc_ccw) {
                            if (sweep < 0.0)
                                sweep += 2.0 * PI;
                        } else {
                            if (sweep > 0.0)
                                sweep -= 2.0 * PI;
                        }
                        m.arc_sweep_rad = sweep;
                        const double arc_len_xy = std::abs(sweep) * r;
                        const double dz         = end.z() - start.z();
                        m.length_mm             = std::sqrt(arc_len_xy * arc_len_xy + dz * dz);
                    } else {
                        m.length_mm = (end - start).norm();
                    }
                } else {
                    // Fallback: treat as straight chord.
                    m.length_mm = (end - start).norm();
                }
            } else {
                m.length_mm = (end - start).norm();
            }

            line_to_motion[li] = static_cast<int>(motions.size());
            motions.push_back(m);

            // Update state after move.
            if (line.has(F))
                state.feedrate_mm_min = feedrate_mm_min;
            state.pos = end;
            if (line.has(E))
                state.e_abs = e_end;
        });
    }

    if (motions.empty())
        return std::move(gcode);

    // Detect objects and build plans, driven by ROI (InterestRegion::InterestObject).
    std::vector<ObjectSpan> spans;
    {
        // Use InterestRegion defaults (same as GUI preview ROI cache).
        // Defect span cap is derived dynamically from the effective safe params (L_safe_total + margin),
        // so that GUI preview and post-processing stay consistent when msao_safe_accel / msao_safe_velocity changes.
        const InterestRegion::AppearanceUnderExtrusionDefinition def;

        auto defect_cap_base_mm = [&](float v_low_mm_s, int object_id) -> double {
            const auto  it         = object_params_by_id.find(object_id);
            const float accel_safe = (it != object_params_by_id.end()) ? it->second.accel_safe : params.accel_safe;
            const float v_safe     = (it != object_params_by_id.end()) ? it->second.velocity_safe_mm_s : params.velocity_safe_mm_s;
            if (!(accel_safe > 0.0f) || !(v_safe > 0.0f))
                return 0.0;
            return InterestRegion::compute_aue_L_safe_total_mm(v_low_mm_s, v_safe, accel_safe, params.L_safe_transition_mm, params.L_safe_cruise_mm);
        };

        const InterestRegion::InterestRegion region =
            InterestRegion::detect_appearance_under_extrusion_interest_region(motions, def, defect_cap_base_mm);

        auto end_ssid_to_motion_idx = [&](size_t end_ssid) -> std::optional<size_t> {
            if (end_ssid == 0)
                return std::nullopt;
            const size_t idx = end_ssid - 1;
            if (idx >= motions.size())
                return std::nullopt;
            return idx;
        };

        spans.reserve(region.objects.size());
        for (const std::unique_ptr<InterestRegion::InterestObject>& obj : region.objects) {
            if (!obj)
                continue;
            if (std::string_view(obj->type_name()) != "AppearanceUnderExtrusion")
                continue;

            std::optional<InterestRegion::SegmentSpan> trigger_span;
            std::optional<InterestRegion::SegmentSpan> defect_span;
            for (const InterestRegion::TaggedSpan& ts : obj->spans()) {
                if (ts.tag == InterestRegion::SegmentTag::Trigger)
                    trigger_span = ts.span;
                else if (ts.tag == InterestRegion::SegmentTag::Defect)
                    defect_span = ts.span;
            }
            if (!trigger_span || !defect_span)
                continue;

            // SegmentSpan is expressed in end-ssid indices (inclusive). For the MotionSegments view,
            // end_ssid N maps directly to motions[N-1].
            const std::optional<size_t> trigger_begin = end_ssid_to_motion_idx(trigger_span->first_end_ssid);
            const std::optional<size_t> trigger_end   = end_ssid_to_motion_idx(trigger_span->last_end_ssid);
            const std::optional<size_t> defect_begin  = end_ssid_to_motion_idx(defect_span->first_end_ssid);
            const std::optional<size_t> defect_end    = end_ssid_to_motion_idx(defect_span->last_end_ssid);
            if (!trigger_begin || !trigger_end || !defect_begin || !defect_end)
                continue;

            if (*trigger_begin > *trigger_end)
                continue;
            if (*defect_begin > *defect_end)
                continue;

            int span_object_id = motions[*defect_begin].object_id;
            if (span_object_id < 0)
                span_object_id = motions[*trigger_begin].object_id;
            spans.push_back({ *trigger_begin, *trigger_end, *defect_begin, *defect_end, span_object_id });
        }
    }
    if (spans.empty())
        return std::move(gcode);

    std::vector<Plan> plans;
    plans.reserve(spans.size());
    for (const ObjectSpan& s : spans) {
        const auto it = object_params_by_id.find(s.object_id);
        const bool  enabled    = (it != object_params_by_id.end()) ? it->second.enabled : params.enabled;
        const float accel_safe = (it != object_params_by_id.end()) ? it->second.accel_safe : params.accel_safe;
        const float velocity_safe_mm_s = (it != object_params_by_id.end()) ? it->second.velocity_safe_mm_s : params.velocity_safe_mm_s;
        if (!enabled)
            continue;

        AppearanceUnderExtrusionAccelRecoveryConfig effective = params;
        effective.accel_safe = accel_safe;
        effective.velocity_safe_mm_s = velocity_safe_mm_s;
        auto p = build_plan(s, motions, effective);
        if (p)
            plans.push_back(*p);
    }
    if (plans.empty())
        return std::move(gcode);

    // Drop overlapping plans to avoid nested state.
    // This part could probably be optimized, since I've already adjusted things so they won't overlap, TODO
    std::sort(plans.begin(), plans.end(), [&](const Plan& a, const Plan& b) { return motions[a.span.trigger_begin].line_idx < motions[b.span.trigger_begin].line_idx; });
    std::vector<Plan> non_overlapping;
    non_overlapping.reserve(plans.size());
    size_t last_end_line = 0;
    bool   has_last_end  = false;
    for (const Plan& p : plans) {
        const size_t begin_line = motions[p.span.trigger_begin].line_idx;
        const size_t end_line   = motions[p.boundary_motion].line_idx;
        if (has_last_end && begin_line <= last_end_line)
            continue;
        non_overlapping.push_back(p);
        last_end_line  = end_line;
        has_last_end   = true;
    }
    plans.swap(non_overlapping);
    if (plans.empty())
        return std::move(gcode);

    // Build quick lookup by line index for insertions / splits.
    struct LineAction
    {
        enum class Kind : unsigned char
        {
            InsertAccelSafe,
            InsertSpeedSafe,
            InsertRecover,
            InsertDefectSpanEnd,
            SplitSpeedSafe,
            SplitRecover,
        };
        Kind   kind;
        size_t plan_idx;
    };

    // Lookup tables by original layer G-code line index (li):
    //  - before[li] / after[li]: actions inserted immediately before/after line li.
    //  - split[li]: actions that require splitting the motion line li at a plan-defined fraction.
    //              (For one plan, we may split twice on the same line: transition-end and recover boundary.)
    //  - safe_plan_of_line[li]: marks L_safe lines where we must strip any legacy feedrate (F) tokens,
    //                           including standalone commands like "G1 F...".
    std::vector<std::vector<LineAction>> before(lines.size());
    std::vector<std::vector<LineAction>> split(lines.size());
    std::vector<std::vector<LineAction>> after(lines.size());
    std::vector<int>                     safe_plan_of_line(lines.size(), -1);

    for (size_t pi = 0; pi < plans.size(); ++pi) {
        const Plan& p = plans[pi];

        const size_t trigger_line    = motions[p.span.trigger_begin].line_idx;
        const size_t defect_line     = motions[p.span.defect_begin].line_idx;
        const size_t boundary_line   = motions[p.boundary_motion].line_idx;
        const size_t defect_end_line = motions[p.span.defect_end].line_idx;

        if (trigger_line < lines.size())
            before[trigger_line].push_back({ LineAction::Kind::InsertAccelSafe, pi });

        // velocity_safe is applied only after transition ends (or immediately if transition length is 0).
        if (p.has_velocity_safe) {
            const size_t velocity_safe_line = motions[p.velocity_safe_motion].line_idx;
            constexpr double begin_eps = 1e-6;
            constexpr double end_eps   = 1e-6;
            if (p.velocity_safe_fraction <= begin_eps) {
                if (velocity_safe_line < lines.size())
                    before[velocity_safe_line].push_back({ LineAction::Kind::InsertSpeedSafe, pi });
            } else if (p.velocity_safe_fraction < 1.0 - end_eps) {
                if (velocity_safe_line < lines.size())
                    split[velocity_safe_line].push_back({ LineAction::Kind::SplitSpeedSafe, pi });
            } else {
                if (velocity_safe_line < lines.size())
                    after[velocity_safe_line].push_back({ LineAction::Kind::InsertSpeedSafe, pi });
            }
        }

        // Mark lines within the safe segment [defect_line, boundary_line] for feedrate cleanup.
        // Note: cooling may emit standalone "G1 F..." right before the first safe-segment motion line, so extend
        // the begin line backwards to include those pure-feedrate commands as well.
        if (defect_line < lines.size()) {
            const size_t begin = extend_safe_begin_line(lines, defect_line);
            const size_t last  = std::min(boundary_line, lines.size() - 1);
            for (size_t l = begin; l <= last; ++l)
                safe_plan_of_line[l] = static_cast<int>(pi);
        }

        constexpr double end_eps = 1e-6;
        if (p.boundary_fraction < 1.0 - end_eps) {
            if (boundary_line < lines.size())
                split[boundary_line].push_back({ LineAction::Kind::SplitRecover, pi });
        } else {
            if (boundary_line < lines.size())
                after[boundary_line].push_back({ LineAction::Kind::InsertRecover, pi });
        }

        if (defect_end_line < lines.size())
            after[defect_end_line].push_back({ LineAction::Kind::InsertDefectSpanEnd, pi });
    }

    const bool use_relative_e = config.use_relative_e_distances.value;
    const bool emit_aue_cmt = params.emit_aue_comments;
    auto aue_comment = [emit_aue_cmt](const char* s) -> const char* { return emit_aue_cmt ? s : ""; };

    std::string out;
    out.reserve(gcode.size() + plans.size() * 256);

    for (size_t li = 0; li < lines.size(); ++li) {
        // Before-line insertions.
        for (const LineAction& a : before[li]) {
            const Plan& p = plans[a.plan_idx];
            if (a.kind == LineAction::Kind::InsertAccelSafe) {
                out += make_set_velocity_limit_accel(p.accel_safe, aue_comment("AUE accel_safe"));
            } else if (a.kind == LineAction::Kind::InsertSpeedSafe) {
                out += make_set_feedrate_mm_s(p.velocity_safe_mm_s, aue_comment("AUE velocity_safe"));
            }
        }

        const int mi = line_to_motion[li];
        if (!split[li].empty() && mi >= 0) {
            const Motion& m = motions[static_cast<size_t>(mi)];

            struct SplitEvent
            {
                LineAction::Kind kind;
                size_t           plan_idx;
                double           fraction;
            };

            std::vector<SplitEvent> events;
            events.reserve(split[li].size());
            for (const LineAction& a : split[li]) {
                const Plan& p = plans[a.plan_idx];
                double      f = 1.0;
                if (a.kind == LineAction::Kind::SplitSpeedSafe)
                    f = p.velocity_safe_fraction;
                else if (a.kind == LineAction::Kind::SplitRecover)
                    f = p.boundary_fraction;
                else
                    continue;
                f = std::clamp(f, 0.0, 1.0);
                events.push_back({ a.kind, a.plan_idx, f });
            }

            if (!events.empty()) {
                const auto kind_priority = [](LineAction::Kind k) -> int {
                    if (k == LineAction::Kind::SplitSpeedSafe)
                        return 0;
                    if (k == LineAction::Kind::SplitRecover)
                        return 1;
                    return 2;
                };
                std::sort(events.begin(), events.end(), [&](const SplitEvent& a, const SplitEvent& b) {
                    if (a.fraction != b.fraction)
                        return a.fraction < b.fraction;
                    return kind_priority(a.kind) < kind_priority(b.kind);
                });

                // Split this motion at one or two fractions (transition end / recover boundary).
                const Vec3d start = m.start;
                const Vec3d end   = m.end;

                auto point_at = [&](double f) -> Vec3d {
                    f = std::clamp(f, 0.0, 1.0);
                    Vec3d pt = end;
                    if (f > 0.0 && f < 1.0) {
                        if (!m.is_arc) {
                            pt = start + (end - start) * f;
                        } else if (m.is_arc && std::fabs(m.arc_sweep_rad) > 0.0) {
                            const Vec2d c = m.arc_center;
                            const Vec2d sxy(start.x(), start.y());
                            const double r = (sxy - c).norm();
                            const double a0 = std::atan2(sxy.y() - c.y(), sxy.x() - c.x());
                            const double sweep = m.arc_sweep_rad * f;
                            const double a = a0 + sweep;
                            const Vec2d pxy(c.x() + r * std::cos(a), c.y() + r * std::sin(a));
                            pt.x() = pxy.x();
                            pt.y() = pxy.y();
                            pt.z() = start.z() + (end.z() - start.z()) * f;
                        } else {
                            pt = start + (end - start) * f;
                        }
                    }
                    return pt;
                };

                const double e_total = m.e_end_abs - m.e_start_abs;
                auto e_abs_at = [&](double f) -> double {
                    f = std::clamp(f, 0.0, 1.0);
                    return m.e_start_abs + e_total * f;
                };

                // Determine the active stage at the beginning of this motion.
                // Overlapping plans are dropped earlier, so split events are expected to belong to a single plan.
                const Plan&   p0 = plans[events.front().plan_idx];
                const size_t  motion_idx = static_cast<size_t>(mi);
                constexpr double begin_eps = 1e-6;

                bool speed_safe_active = false;
                if (p0.has_velocity_safe) {
                    if (motion_idx > p0.velocity_safe_motion)
                        speed_safe_active = true;
                    else if (motion_idx == p0.velocity_safe_motion && p0.velocity_safe_fraction <= begin_eps)
                        speed_safe_active = true;
                }
                bool recovered_active = false;

                auto segment_comment = [&](bool arc) -> const char* {
                    if (recovered_active)
                        return nullptr;
                    if (speed_safe_active)
                        return aue_comment(arc ? "AUE safe(arc)" : "AUE safe(part)");
                    return aue_comment(arc ? "AUE transition(arc)" : "AUE transition(part)");
                };

                double prev_f = 0.0;
                Vec3d  prev_pt = start;
                for (const SplitEvent& ev : events) {
                    const double f = std::clamp(ev.fraction, 0.0, 1.0);
                    if (f < prev_f)
                        continue;

                    if (f > prev_f) {
                        const Vec3d pt = point_at(f);
                        const double e_val = use_relative_e ? (m.delta_e * (f - prev_f)) : e_abs_at(f);

                        // Output segment up to the split point without any F token.
                        if (!m.is_arc) {
                            out += format_linear_move(pt, e_val, use_relative_e, segment_comment(false));
                        } else {
                            const Vec2d c = m.arc_center;
                            const Vec2d ij(c.x() - prev_pt.x(), c.y() - prev_pt.y());
                            out += format_arc_move(m.arc_ccw, pt, ij, e_val, use_relative_e, segment_comment(true));
                        }

                        prev_f  = f;
                        prev_pt = pt;
                    }

                    // Insert commands at split point.
                    const Plan& p = plans[ev.plan_idx];
                    if (ev.kind == LineAction::Kind::SplitSpeedSafe) {
                        out += make_set_feedrate_mm_s(p.velocity_safe_mm_s, aue_comment("AUE velocity_safe"));
                        speed_safe_active = true;
                    } else if (ev.kind == LineAction::Kind::SplitRecover) {
                        out += make_set_velocity_limit_accel(p.accel_recovered, aue_comment("AUE accel_recovered"));
                        out += make_set_feedrate_mm_s(p.velocity_recovered_mm_s, aue_comment("AUE velocity_recovered"));
                        recovered_active = true;
                    }
                }

                // Output the remaining segment to the original end point.
                const double e_last = use_relative_e ? (m.delta_e * (1.0 - prev_f)) : m.e_end_abs;
                if (!m.is_arc) {
                    out += format_linear_move(end, e_last, use_relative_e, segment_comment(false));
                } else {
                    const Vec2d c = m.arc_center;
                    const Vec2d ij(c.x() - prev_pt.x(), c.y() - prev_pt.y());
                    out += format_arc_move(m.arc_ccw, end, ij, e_last, use_relative_e, segment_comment(true));
                }

                // After-line insertions for this line (recover-at-end, defect span end marker, etc.).
                for (const LineAction& a : after[li]) {
                    const Plan& ap = plans[a.plan_idx];
                    if (a.kind == LineAction::Kind::InsertSpeedSafe) {
                        out += make_set_feedrate_mm_s(ap.velocity_safe_mm_s, aue_comment("AUE velocity_safe"));
                    } else if (a.kind == LineAction::Kind::InsertRecover) {
                        out += make_set_velocity_limit_accel(ap.accel_recovered, aue_comment("AUE accel_recovered"));
                        out += make_set_feedrate_mm_s(ap.velocity_recovered_mm_s, aue_comment("AUE velocity_recovered"));
                    } else if (a.kind == LineAction::Kind::InsertDefectSpanEnd) {
                        if (emit_aue_cmt)
                            out += "; defect span end\n";
                    }
                }

                continue; // skip original line
            }
        }

        // If this line is within the safe segment (L_safe), remove any legacy feedrate (F) tokens,
        // including standalone commands like "G1 F...".
        if (safe_plan_of_line[li] >= 0)
            out += strip_feedrate_from_motion_cmd(lines[li]);
        else
            out += lines[li];

        // After-line insertions (recover-at-end).
        for (const LineAction& a : after[li]) {
            const Plan& p = plans[a.plan_idx];
            if (a.kind == LineAction::Kind::InsertSpeedSafe) {
                out += make_set_feedrate_mm_s(p.velocity_safe_mm_s, aue_comment("AUE velocity_safe"));
            } else if (a.kind == LineAction::Kind::InsertRecover) {
                out += make_set_velocity_limit_accel(p.accel_recovered, aue_comment("AUE accel_recovered"));
                out += make_set_feedrate_mm_s(p.velocity_recovered_mm_s, aue_comment("AUE velocity_recovered"));
            } else if (a.kind == LineAction::Kind::InsertDefectSpanEnd) {
                if (emit_aue_cmt)
                    out += "; defect span end\n";
            }
        }
    }

    return out;
}

} // namespace Slic3r
