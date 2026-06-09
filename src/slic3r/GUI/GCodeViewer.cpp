#include "libslic3r/libslic3r.h"
#include "GCodeViewer.hpp"

#include "libslic3r/BuildVolume.hpp"
#include "libslic3r/ClipperUtils.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/Geometry.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/ModelVolume.hpp"
#include "libslic3r/ModelInstance.hpp"
#include "libslic3r/Utils.hpp"
#include "libslic3r/LocalesUtils.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/GCode/InterestRegion.hpp"
//BBS: add convex hull logic for toolpath check
#include "libslic3r/Geometry/ConvexHull.hpp"
#include "slic3r/GUI/PartPlate.hpp"
#include "slic3r/Utils/TestHelper.hpp"
#include "../Config/DispConfig.h"
#include "GUI_App.hpp"
#include "GUI_ObjectList.hpp"
#include "MainFrame.hpp"
#include "Plater.hpp"
#include "Camera.hpp"
#include "I18N.hpp"
#include "GUI_Utils.hpp"
#include "GUI.hpp"
#include "GLCanvas3D.hpp"
#include "GLToolbar.hpp"
#include "GUI_Preview.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/Layer.hpp"
#include "Widgets/ProgressDialog.hpp"
#include "NotificationManager.hpp"
#include "MsgDialog.hpp"
#include <imgui/imgui_internal.h>

#include <GL/glew.h>
#include <boost/log/trivial.hpp>
#include <boost/log/core.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/nowide/cstdio.hpp>
#include <boost/nowide/fstream.hpp>
#include <string>
#include <wx/progdlg.h>
#include <wx/numformatter.h>

#include <array>
#include <algorithm>
#include <chrono>
#include <unordered_set>
#include <utility>

#define SHORT_TYPE_POSITION_SCALE            (50.0)
#define SHORT_TYPE_POSITION_SCALE_INVERSE    (1.0 / SHORT_TYPE_POSITION_SCALE)
#define SHORT_TYPE_NORMAL_SCALE              (30000.0)  //use 32767.0 may cause rendering issues

#define ENABLE_FILTERED_STRIDE_DIALOG (0)

#define MARKING_DIALOG
#ifndef MARKING_DIALOG
#define MARKING_DIALOG(msg) \
	if (std::string(PROJECT_VERSION_EXTRA) == std::string("Alpha")) \
    { \
        MessageDialog dlg(nullptr, msg, _L("Preview"), wxYES); \
        dlg.ShowModal(); \
    }
#endif // !MARKING_DIALOG

namespace Slic3r {
namespace GUI {

    static std::vector<ColorRGBA> Extrusion_Role_Colors{ {
        { 0.90f, 0.70f, 0.70f, 1.0f },   // erNone
        { 1.00f, 0.90f, 0.30f, 1.0f },   // erPerimeter
        { 1.00f, 0.49f, 0.22f, 1.0f },   // erExternalPerimeter
        { 0.12f, 0.12f, 1.00f, 1.0f },   // erOverhangPerimeter
        { 0.69f, 0.19f, 0.16f, 1.0f },   // erInternalInfill
        { 0.59f, 0.33f, 0.80f, 1.0f },   // erSolidInfill
        { 0.94f, 0.25f, 0.25f, 1.0f },   // erTopSolidInfill
        { 0.40f, 0.36f, 0.78f, 1.0f },   // erBottomSurface
        { 1.00f, 0.55f, 0.41f, 1.0f },   // erIroning
        { 0.30f, 0.40f, 0.63f, 1.0f },   // erBridgeInfill
        { 0.30f, 0.50f, 0.73f, 1.0f },   // erInternalBridgeInfill
        { 1.00f, 1.00f, 1.00f, 1.0f },   // erGapFill
        { 0.00f, 0.53f, 0.43f, 1.0f },   // erSkirt
        { 0.00f, 0.23f, 0.43f, 1.0f },   // erBrim
        { 0.00f, 1.00f, 0.00f, 1.0f },   // erSupportMaterial
        { 0.00f, 0.50f, 0.00f, 1.0f },   // erSupportMaterialInterface
        { 0.00f, 0.25f, 0.00f, 1.0f },   // erSupportTransition
        { 0.70f, 0.89f, 0.67f, 1.0f },   // erWipeTower
        { 0.37f, 0.82f, 0.58f, 1.0f }    // erCustom
    } };

    static std::vector<ColorRGBA> Options_Colors{ {
        { 0.803f, 0.135f, 0.839f, 1.0f },   // Retractions
        { 0.287f, 0.679f, 0.810f, 1.0f },   // Unretractions
        { 0.900f, 0.900f, 0.900f, 1.0f },   // Seams
        { 0.758f, 0.744f, 0.389f, 1.0f },   // ToolChanges
        { 0.856f, 0.582f, 0.546f, 1.0f },   // ColorChanges
        { 0.322f, 0.942f, 0.512f, 1.0f },   // PausePrints
        { 0.886f, 0.825f, 0.262f, 1.0f }    // CustomGCodes
    } };

    static std::vector<ColorRGBA> Travel_Colors{ {
        { 0.219f, 0.282f, 0.609f, 1.0f }, // Move
        { 0.112f, 0.422f, 0.103f, 1.0f }, // Extrude
        { 0.505f, 0.064f, 0.028f, 1.0f }  // Retract
    } };

    static std::vector<ColorRGBA> Range_Colors{ {
        decode_color_to_float_array("#0b2c7a"),  // bluish
        decode_color_to_float_array("#135985"),
        decode_color_to_float_array("#1c8891"),
        decode_color_to_float_array("#04d60f"),
        decode_color_to_float_array("#aaf200"),
        decode_color_to_float_array("#fcf903"),
        decode_color_to_float_array("#f5ce0a"),
        //decode_color_to_float_array("#e38820"),
        decode_color_to_float_array("#d16830"),
        decode_color_to_float_array("#c2523c"),
        decode_color_to_float_array("#942616")    // reddish
    } };

    static ColorRGBA Wipe_Color = ColorRGBA::YELLOW();
    static ColorRGBA Flush_Color = { 1.0f, 0.6f, 0.8f, 1.0f };  // pink
    static ColorRGBA Neutral_Color = ColorRGBA::DARK_GRAY();
    static bool s_preview_section_fold = false;

static std::string get_view_type_string(GCodeViewer::EViewType view_type)
{
    if (view_type == GCodeViewer::EViewType::FeatureType)
        return _u8L("Line Type");
    else if (view_type == GCodeViewer::EViewType::Custom)
        return _u8L("Custom");
    else if (view_type == GCodeViewer::EViewType::Height)
        return _u8L("Layer Height");
    else if (view_type == GCodeViewer::EViewType::Width)
        return _u8L("Line Width");
    else if (view_type == GCodeViewer::EViewType::Feedrate)
        return _u8L("Speed");
    else if (view_type == GCodeViewer::EViewType::FanSpeed)
        return _u8L("Fan Speed");
    else if (view_type == GCodeViewer::EViewType::Temperature)
        return _u8L("Temperature");
    else if (view_type == GCodeViewer::EViewType::VolumetricRate)
        return _u8L("Flow");
    else if (view_type == GCodeViewer::EViewType::Tool)
        return _u8L("Tool");
    else if (view_type == GCodeViewer::EViewType::ColorPrint)
        return _u8L("Filament");
    else if (view_type == GCodeViewer::EViewType::LayerTime)
        return _u8L("Layer Time");
    else if (view_type == GCodeViewer::EViewType::LayerTimeLog)
        return _u8L("Layer Time (log)");
    else if (view_type == GCodeViewer::EViewType::Acceleration)
        return _u8L("Acceleration");
    return "";
}

static std::pair<ColorRGBA, ColorRGBA> pick_interest_region_highlight_colors(const ColorRGBA& base_color)
{
    // Pick two highlight colors with good contrast against the base (filament) color.
    // They are also forced to be different from each other so that Trigger/Defect can be distinguished.
    static const std::array<ColorRGBA, 7> candidates = {
        ColorRGBA::YELLOW(),
        ColorRGBA::MAGENTA(),
        ColorRGBA::CYAN(),
        ColorRGBA::ORANGE(),
        ColorRGBA::GREEN(),
        ColorRGBA::RED(),
        ColorRGBA::BLUE()
    };

    auto dist2 = [](const ColorRGBA& a, const ColorRGBA& b) {
        const float dr = a.r() - b.r();
        const float dg = a.g() - b.g();
        const float db = a.b() - b.b();
        return dr * dr + dg * dg + db * db;
    };

    // Find best and second best by distance.
    ColorRGBA best1 = candidates.front();
    float best1_score = -1.0f;
    ColorRGBA best2 = candidates.front();
    float best2_score = -1.0f;

    for (const ColorRGBA& c : candidates) {
        const float score = dist2(base_color, c);
        if (score > best1_score) {
            best2 = best1;
            best2_score = best1_score;
            best1 = c;
            best1_score = score;
        } else if (score > best2_score) {
            best2 = c;
            best2_score = score;
        }
    }

    // Return (trigger_color, defect_color). Defect is more critical so use the farthest.
    return { best2, best1 };
}


static unsigned char buffer_id(EMoveType type) {
    return static_cast<unsigned char>(type) - static_cast<unsigned char>(EMoveType::Retract);
}

static EMoveType buffer_type(unsigned char id) {
    return static_cast<EMoveType>(static_cast<unsigned char>(EMoveType::Retract) + id);
}

// Round to a bin with minimum two digits resolution.
// Equivalent to conversion to string with sprintf(buf, "%.2g", value) and conversion back to float, but faster.
static float round_to_bin(const float value)
{
//    assert(value > 0);
    constexpr float const scale    [5] = { 100.f,  1000.f,  10000.f,  100000.f,  1000000.f };
    constexpr float const invscale [5] = { 0.01f,  0.001f,  0.0001f,  0.00001f,  0.000001f };
    constexpr float const threshold[5] = { 0.095f, 0.0095f, 0.00095f, 0.000095f, 0.0000095f };
    // Scaling factor, pointer to the tables above.
    int                   i            = 0;
    // While the scaling factor is not yet large enough to get two integer digits after scaling and rounding:
    for (; value < threshold[i] && i < 4; ++ i) ;
    return std::round(value * scale[i]) * invscale[i];
}

// Find an index of a value in a sorted vector, which is in <z-eps, z+eps>.
// Returns -1 if there is no such member.
static int find_close_layer_idx(const std::vector<double> &zs, double &z, double eps)
{
    if (zs.empty()) return -1;
    auto it_h = std::lower_bound(zs.begin(), zs.end(), z);
    if (it_h == zs.end()) {
        auto it_l = it_h;
        --it_l;
        if (z - *it_l < eps) return int(zs.size() - 1);
    } else if (it_h == zs.begin()) {
        if (*it_h - z < eps) return 0;
    } else {
        auto it_l = it_h;
        --it_l;
        double dist_l = z - *it_l;
        double dist_h = *it_h - z;
        if (std::min(dist_l, dist_h) < eps) { return (dist_l < dist_h) ? int(it_l - zs.begin()) : int(it_h - zs.begin()); }
    }
    return -1;
}

static Vec2f calc_pt_in_screen(const Vec3d& pt, const Matrix4d& view_proj_mat, int window_width, int window_height)
{
    auto  tran = view_proj_mat;
    Vec4d temp_center(pt.x(), pt.y(), pt.z(), 1.0);
    Vec4d temp_ndc          = tran * temp_center;
    Vec3d screen_box_center = Vec3d(temp_ndc.x(), temp_ndc.y(), temp_ndc.z()) / temp_ndc.w();

    float x = 0.5f * (1 + screen_box_center(0)) * window_width;
    float y = 0.5f * (1 - screen_box_center(1)) * window_height;
    return Vec2f(x, y);
}

static bool has_non_zero_volume(const std::map<size_t, double>& volumes, const double eps = 1e-9)
{
    return std::any_of(volumes.begin(), volumes.end(), [eps](const auto& item) { return item.second > eps; });
}

static void collect_active_extruders(const std::map<size_t, double>& volumes, std::unordered_set<size_t>& out, const double eps = 1e-9)
{
    for (const auto& [extruder_id, volume] : volumes) {
        if (volume > eps)
            out.insert(extruder_id);
    }
}

// Merge "Flushed" into "Model" only when the plate truly behaves as a single-nozzle print:
// one active extruder in statistics, flush exists, and no support / wipe tower material.
static bool should_merge_flush_into_model(const PrintEstimatedStatistics& stats)
{
    const bool has_flush = has_non_zero_volume(stats.flush_per_filament);
    if (!has_flush)
        return false;

    std::unordered_set<size_t> active_extruders;
    collect_active_extruders(stats.total_volumes_per_extruder, active_extruders);
    // Fallback for incomplete totals.
    if (active_extruders.empty()) {
        collect_active_extruders(stats.model_volumes_per_extruder, active_extruders);
        collect_active_extruders(stats.flush_per_filament, active_extruders);
        collect_active_extruders(stats.support_volumes_per_extruder, active_extruders);
        collect_active_extruders(stats.wipe_tower_volumes_per_extruder, active_extruders);
    }

    const bool has_support    = has_non_zero_volume(stats.support_volumes_per_extruder);
    const bool has_wipe_tower = has_non_zero_volume(stats.wipe_tower_volumes_per_extruder);
    return active_extruders.size() == 1 && !has_support && !has_wipe_tower;
}
void GCodeViewer::VBuffer::reset()
{
    // release gpu memory
    if (!vbos.empty()) {
        glsafe(::glDeleteBuffers(static_cast<GLsizei>(vbos.size()), static_cast<const GLuint*>(vbos.data())));
        vbos.clear();
    }
    sizes.clear();
    count = 0;
}

void GCodeViewer::InstanceVBuffer::Ranges::reset()
{
    for (Range& range : ranges) {
        // release gpu memory
        if (range.vbo > 0)
            glsafe(::glDeleteBuffers(1, &range.vbo));
    }

    ranges.clear();
}

void GCodeViewer::InstanceVBuffer::reset()
{
    s_ids.clear();
    s_ids.shrink_to_fit();
    buffer.clear();
    buffer.shrink_to_fit();
    offsets.clear();
    offsets.shrink_to_fit();
    render_ranges.reset();
}

void GCodeViewer::IBuffer::reset()
{
    // release gpu memory
    if (ibo > 0) {
        glsafe(::glDeleteBuffers(1, &ibo));
        ibo = 0;
    }

    vbo = 0;
    count = 0;
}

bool GCodeViewer::Path::matches(const GCodeProcessorResult::MoveVertex& move) const
{
    auto matches_percent = [](float value1, float value2, float max_percent) {
        return std::abs(value2 - value1) / value1 <= max_percent;
    };

    switch (move.type)
    {
    case EMoveType::Tool_change:
    case EMoveType::Color_change:
    case EMoveType::Pause_Print:
    case EMoveType::Custom_GCode:
    case EMoveType::Retract:
    case EMoveType::Unretract:
    case EMoveType::Seam:
    case EMoveType::Extrude_Alter:
    case EMoveType::Extrude: {
        // use rounding to reduce the number of generated paths
        return type == move.type && extruder_id == move.extruder_id && cp_color_id == move.cp_color_id && role == move.extrusion_role &&
            move.position.z() <= sub_paths.front().first.position.z() && feedrate == move.feedrate && fan_speed == move.fan_speed &&
            height == round_to_bin(move.height) && width == round_to_bin(move.width) &&
            matches_percent(volumetric_rate, move.volumetric_rate(), 0.05f) && layer_time == move.layer_duration
            && acceleration == move.acceleration;
    }
    case EMoveType::Travel: {
        return type == move.type && feedrate == move.feedrate && extruder_id == move.extruder_id && cp_color_id == move.cp_color_id;
    }
    default: { return false; }
    }
}

void GCodeViewer::TBuffer::Model::reset()
{
    instances.reset();
}

void GCodeViewer::TBuffer::reset()
{
    vertices.reset();
    for (IBuffer& buffer : indices) {
        buffer.reset();
    }

    indices.clear();
    paths.clear();
    render_paths.clear();
    model.reset();
}

void GCodeViewer::TBuffer::add_path(const GCodeProcessorResult::MoveVertex& move, unsigned int b_id, size_t i_id, size_t s_id)
{
    Path::Endpoint endpoint = { b_id, i_id, s_id, move.position };
    // use rounding to reduce the number of generated paths
    paths.push_back({ move.type, move.extrusion_role, move.delta_extruder,
        round_to_bin(move.height), round_to_bin(move.width),
        move.feedrate, move.fan_speed, move.temperature,
        move.volumetric_rate(), move.layer_duration, move.acceleration, move.extruder_id, move.cp_color_id, { { endpoint, endpoint } } });
}

unsigned int GCodeViewer::TBuffer::max_vertices_per_segment() const
{
    switch (render_primitive_type)
    {
    case ERenderPrimitiveType::Line: { return 2; }
    case ERenderPrimitiveType::Triangle: { return 8; }
    default: { return 0; }
    }
}

size_t GCodeViewer::TBuffer::max_vertices_per_segment_size_floats() const
{
    return vertices.vertex_size_floats() * static_cast<size_t>(max_vertices_per_segment());
}

size_t GCodeViewer::TBuffer::max_vertices_per_segment_size_bytes() const
{
    return max_vertices_per_segment_size_floats() * sizeof(float);
}

unsigned int GCodeViewer::TBuffer::indices_per_segment() const
{
    switch (render_primitive_type)
    {
    case ERenderPrimitiveType::Line: { return 2; }
    case ERenderPrimitiveType::Triangle: { return 30; } // 3 indices x 10 triangles
    default: { return 0; }
    }
}

size_t GCodeViewer::TBuffer::indices_per_segment_size_bytes() const
{
    return static_cast<size_t>(indices_per_segment() * sizeof(IBufferType));
}

unsigned int GCodeViewer::TBuffer::max_indices_per_segment() const
{
    switch (render_primitive_type)
    {
    case ERenderPrimitiveType::Line: { return 2; }
    case ERenderPrimitiveType::Triangle: { return 36; } // 3 indices x 12 triangles
    default: { return 0; }
    }
}

size_t GCodeViewer::TBuffer::max_indices_per_segment_size_bytes() const
{
    return max_indices_per_segment() * sizeof(IBufferType);
}

bool GCodeViewer::TBuffer::has_data() const
{
    switch (render_primitive_type)
    {
    case ERenderPrimitiveType::Line:
    case ERenderPrimitiveType::Triangle: {
        return !vertices.vbos.empty() && vertices.vbos.front() != 0 && !indices.empty() && indices.front().ibo != 0;
    }
    case ERenderPrimitiveType::InstancedModel: { return model.model.is_initialized() && !model.instances.buffer.empty(); }
    case ERenderPrimitiveType::BatchedModel: {
        return !model.data.vertices.empty() && !model.data.indices.empty() &&
            !vertices.vbos.empty() && vertices.vbos.front() != 0 && !indices.empty() && indices.front().ibo != 0;
    }
    default: { return false; }
    }
}

ColorRGBA GCodeViewer::Extrusions::Range::get_color_at(float value) const
{
    // Input value scaled to the colors range
    const float step = step_size();
    float _min = min;
    if(log_scale) {
        value = std::log(value);
        _min = std::log(min);
    }
    const float global_t = (step != 0.0f) ? std::max(0.0f, value - _min) / step : 0.0f; // lower limit of 0.0f

    const size_t color_max_idx = Range_Colors.size() - 1;

    // Compute the two colors just below (low) and above (high) the input value
    const size_t color_low_idx = std::clamp<size_t>(static_cast<size_t>(global_t), 0, color_max_idx);
    const size_t color_high_idx = std::clamp<size_t>(color_low_idx + 1, 0, color_max_idx);

    // Interpolate between the low and high colors to find exactly which color the input value should get
    return lerp(Range_Colors[color_low_idx], Range_Colors[color_high_idx], global_t - static_cast<float>(color_low_idx));
}

float GCodeViewer::Extrusions::Range::step_size() const {
if (log_scale)
    {
        float min_range = min;
        if (min_range == 0)
            min_range = 0.001f;
        return (std::log(max / min_range) / (static_cast<float>(Range_Colors.size()) - 1.0f));
    } else
    return (max - min) / (static_cast<float>(Range_Colors.size()) - 1.0f);
}

float GCodeViewer::Extrusions::Range::get_value_at_step(int step) const {
    if (!log_scale)
        return min + static_cast<float>(step) * step_size();
    else
    return std::exp(std::log(min) + static_cast<float>(step) * step_size());
    
}
GCodeViewer::SequentialRangeCap::~SequentialRangeCap() {
    if (ibo > 0)
        glsafe(::glDeleteBuffers(1, &ibo));
}

void GCodeViewer::SequentialRangeCap::reset() {
    if (ibo > 0)
        glsafe(::glDeleteBuffers(1, &ibo));

    buffer = nullptr;
    ibo = 0;
    vbo = 0;
    color = { 0.0f, 0.0f, 0.0f, 1.0f };
}

void GCodeViewer::Marker::init(std::string filename)
{
    if (filename.empty()) {
        m_model.init_from(stilized_arrow(16, 1.5f, 3.0f, 0.8f, 3.0f));
    } else {
        m_model.init_from_file(filename);
    }
    m_model.set_color({ 1.0f, 1.0f, 1.0f, 0.5f });
}

const Slic3r::BoundingBoxf3& GCodeViewer::Marker::get_bounding_box() const { 
    return m_model.get_bounding_box();
}

void GCodeViewer::Marker::set_world_position(const Vec3f& position)
{
    m_world_position = position;
    m_world_transform = (Geometry::assemble_transform((position + m_z_offset * Vec3f::UnitZ()).cast<double>()) * Geometry::assemble_transform(m_model.get_bounding_box().size().z() * Vec3d::UnitZ(), { M_PI, 0.0, 0.0 })).cast<float>();
}

void GCodeViewer::Marker::set_world_offset(const Vec3f& offset) { 
    m_world_offset = offset; 
}

void GCodeViewer::Marker::update_curr_move(const GCodeProcessorResult::MoveVertex move) {
    m_curr_move = move;
}

//BBS: GUI refactor: add canvas size from parameters
void GCodeViewer::Marker::render(int canvas_width, int canvas_height, const EViewType& view_type, bool showMark)
{
    if (!m_visible)
        return;

    if (showMark) {
    GLShaderProgram* shader = wxGetApp().get_shader("gouraud_light");
    if (shader == nullptr)
        return;

    glsafe(::glEnable(GL_BLEND));
    glsafe(::glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA));

    shader->start_using();
    shader->set_uniform("emission_factor", 0.0f);

    const Camera& camera = wxGetApp().plater()->get_camera();
    const Transform3d& view_matrix = camera.get_view_matrix();
    const Transform3d model_matrix = m_world_transform.cast<double>();
    shader->set_uniform("view_model_matrix", view_matrix * model_matrix);
    shader->set_uniform("projection_matrix", camera.get_projection_matrix());
    const Matrix3d view_normal_matrix = view_matrix.matrix().block(0, 0, 3, 3) * model_matrix.matrix().block(0, 0, 3, 3).inverse().transpose();
    shader->set_uniform("view_normal_matrix", view_normal_matrix);

    m_model.render();

    shader->stop_using();

    glsafe(::glDisable(GL_BLEND));
    }

    static float last_window_width = 0.0f;
    size_t text_line = 0;
    static size_t last_text_line = 0;
    const ImU32 text_name_clr = m_is_dark ? IM_COL32(255, 255, 255, 0.88 * 255) : IM_COL32(38, 46, 48, 255);
    const ImU32   text_value_clr    = m_is_dark ? IM_COL32(255, 255, 255, 255) : IM_COL32(51, 51, 51, 255);

    ImGuiWrapper& imgui = *wxGetApp().imgui();
    //BBS: GUI refactor: add canvas size from parameters

    const float scale = wxGetApp().plater()->get_current_canvas3D()->get_scale();
    float left_widgets_width = (90 + 300) * scale; // thumbs + gcode viewer
    ImGuiWindow* win = ImGui::FindWindowByName("gcode_legend");
    if (win) {
        left_widgets_width = std::max(win->Pos.x + win->SizeFull.x, left_widgets_width);
    }
    const float right_widgets_width = (215 + 10) * scale; // slicer buttons
    const float remaining_space     = canvas_width - left_widgets_width - right_widgets_width;
       
    imgui.set_next_window_pos(left_widgets_width + remaining_space / 2.0, static_cast<float>(canvas_height), ImGuiCond_Always, 0.5f, 1.0f);
    imgui.push_toolbar_style(m_scale);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 6.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0, 2.0 * m_scale));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(20.0 * m_scale, 3.0 * m_scale));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize,  m_is_dark ? 0.0f : 1.0f);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                          m_is_dark ? ImVec4(1.0f, 1.0f, 1.0f, 1.0f) : ImVec4(78.0f / 255.0f, 89.0f / 255.0f, 105.0f / 255.0f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Text, m_is_dark ? ImVec4(219.0f / 255.0f, 219.0f / 255.0f, 219.0f / 255.0f, 1.0f) :
                                                     ImVec4(78.0f / 255.0f, 89.0f / 255.0f, 105.0f / 255.0f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(225.0f / 255.0f, 228.0f / 255.0f, 233.0f / 255.0f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, m_is_dark ? ImVec4(34.0f / 255.0f, 34.0f / 255.0f, 34.0f / 255.0f, 0.6f) :
                                                     ImVec4(1.0f, 1.0f, 1.0f, 0.6f));

    const ImGuiStyle& st = ImGui::GetStyle();
    const float target_h = (ImGui::GetTextLineHeight() + st.WindowPadding.y * 1.4f) * 2.0f;
    ImGui::SetNextWindowSizeConstraints(ImVec2(0.0f, target_h), ImVec2(1e9f, target_h));
    imgui.begin(std::string("ExtruderPosition"),
                ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    // Use default font size (revert any scaling)
    ImGui::SetWindowFontScale(1.1f);

    std::vector<std::string> position_row_texts;
    position_row_texts.reserve(6);
    float       column_width = 0.0f;
    float       column_step  = 0.0f;
    const float window_padding = st.WindowPadding.x;
    const float item_spacing   = imgui.get_item_spacing().x;

    {
        //BBS: minus the plate offset when show tool position
        PartPlateList& partplate_list = wxGetApp().plater()->get_partplate_list();
        PartPlate*     plate          = partplate_list.get_curr_plate();
        const Vec3f    position       = m_world_position + m_world_offset;

        std::string x = ImGui::ColorMarkerStart + std::string("X: ") + ImGui::ColorMarkerEnd;
        std::string y = ImGui::ColorMarkerStart + std::string("Y: ") + ImGui::ColorMarkerEnd;
        std::string z = ImGui::ColorMarkerStart + std::string("Z: ") + ImGui::ColorMarkerEnd;
        std::string height = ImGui::ColorMarkerStart + _u8L("Height: ") + ImGui::ColorMarkerEnd;
        std::string width = ImGui::ColorMarkerStart + _u8L("Width: ") + ImGui::ColorMarkerEnd;
        std::string speed = ImGui::ColorMarkerStart + _u8L("Speed: ") + ImGui::ColorMarkerEnd;
        std::string flow = ImGui::ColorMarkerStart + _u8L("Flow: ") + ImGui::ColorMarkerEnd;
        std::string layer_time = ImGui::ColorMarkerStart + _u8L("Layer Time: ") + ImGui::ColorMarkerEnd;
        std::string fanspeed = ImGui::ColorMarkerStart + _u8L("Fan: ") + ImGui::ColorMarkerEnd;
        std::string temperature = ImGui::ColorMarkerStart + _u8L("Temperature: ") + ImGui::ColorMarkerEnd;
        std::string acceleration = ImGui::ColorMarkerStart + _u8L("Acceleration") + ": " + ImGui::ColorMarkerEnd;

        const float base_column_width = imgui.calc_text_size(std::string_view{"X: 000.000000  "}).x;
        char        buf[1024];

        sprintf(buf, "%s%.3f", x.c_str(), position.x() - plate->get_origin().x());
        position_row_texts.emplace_back(buf);
        sprintf(buf, "%s%.3f", y.c_str(), position.y() - plate->get_origin().y());
        position_row_texts.emplace_back(buf);
        sprintf(buf, "%s%.3f", z.c_str(), position.z());
        position_row_texts.emplace_back(buf);
        sprintf(buf, "%s%.0f", speed.c_str(), m_curr_move.feedrate);
        position_row_texts.emplace_back(buf);

        switch (view_type) {
        case EViewType::Height: {
            sprintf(buf, "%s%.2f", height.c_str(), m_curr_move.height);
            position_row_texts.emplace_back(buf);
            break;
        }
        case EViewType::Width: {
            sprintf(buf, "%s%.2f", width.c_str(), m_curr_move.width);
            position_row_texts.emplace_back(buf);
            break;
        }
        case EViewType::VolumetricRate: {
            sprintf(buf, "%s%.2f", flow.c_str(), m_curr_move.volumetric_rate());
            position_row_texts.emplace_back(buf);
            break;
        }
        case EViewType::FanSpeed: {
            sprintf(buf, "%s%.0f", fanspeed.c_str(), m_curr_move.fan_speed);
            position_row_texts.emplace_back(buf);
            break;
        }
        case EViewType::Temperature: {
            sprintf(buf, "%s%.0f", temperature.c_str(), m_curr_move.temperature);
            position_row_texts.emplace_back(buf);
            break;
        }
        case EViewType::LayerTime:
        case EViewType::LayerTimeLog: {
            sprintf(buf, "%s%.1f", layer_time.c_str(), m_curr_move.layer_duration);
            position_row_texts.emplace_back(buf);
            break;
        }
        case EViewType::Acceleration:
            sprintf(buf, "%s%.0f", acceleration.c_str(), m_curr_move.acceleration);
            position_row_texts.emplace_back(buf);
            break;
        default:
            break;
        }

        column_width = base_column_width;
        for (const std::string& text : position_row_texts)
            column_width = std::max(column_width, ImGui::CalcTextSize(text.c_str()).x);

        column_step = column_width + item_spacing;

        const float first_row_width = window_padding * 2.0f +
                                      static_cast<float>(position_row_texts.size()) * column_width +
                                      (static_cast<float>(position_row_texts.size()) - 1.0f) * item_spacing;
        ImGui::SetWindowSize(ImVec2(first_row_width, target_h));
    }


    {
        ImVec2 win_pos = ImGui::GetWindowPos();
        ImVec2 win_sz  = ImGui::GetWindowSize();
        ImDrawList* bg = ImGui::GetBackgroundDrawList();
        const ImGuiStyle& style = ImGui::GetStyle();
        
        const ImVec4 sh_rgb = ImVec4(118.0f/255.0f, 142.0f/255.0f, 171.0f/255.0f, 1.0f);
       
        const float alphas[8] = { 0.050f, 0.040f, 0.032f, 0.026f, 0.020f, 0.014f, 0.010f, 0.006f };
        
        const float steps[8]  = { 2.00f, 1.60f, 1.20f, 0.90f, 0.70f, 0.50f, 0.35f, 0.20f };
        for (int i = 0; i < 8; ++i) {
            float spread = steps[i] * m_scale;
            float round  = style.WindowRounding + spread;

            float off_x = spread * 0.10f;
            float off_y = spread * 0.80f;
            ImU32 col = ImGui::ColorConvertFloat4ToU32(ImVec4(sh_rgb.x, sh_rgb.y, sh_rgb.z, alphas[i]));
            bg->AddRectFilled(ImVec2(win_pos.x - off_x, win_pos.y + off_y),
                              ImVec2(win_pos.x + win_sz.x + off_x, win_pos.y + win_sz.y + spread + off_y),
                              col, round);
        }
    }
    ImGui::AlignTextToFramePadding();

    {
        const ImGuiStyle& st        = ImGui::GetStyle();
        float             content_h = ImGui::GetWindowHeight() - st.WindowPadding.y * 2.0f;
        float             line_h    = ImGui::GetFrameHeight();
        float             y_offset  = std::max(0.0f, (content_h - line_h) * 0.5f);
        ImGui::SetCursorPosY(st.WindowPadding.y + y_offset);
    }

    if (true) {
        auto draw_value = [&](const std::string& text) {
            ImGui::PushItemWidth(column_width);
            imgui.text(text.c_str());
        };

        for (size_t idx = 0; idx < position_row_texts.size(); ++idx) {
            if (idx > 0)
                ImGui::SameLine(window_padding + static_cast<float>(idx) * column_step);
            draw_value(position_row_texts[idx]);
        }
        ImGui::NewLine();
        text_line = 1;
    }
    // else {
    //     sprintf(buf, "%s%.3f", x.c_str(), position.x() - plate->get_origin().x());
    //     imgui.text(buf);

    //     ImGui::SameLine();
    //     sprintf(buf, "%s%.3f", y.c_str(), position.y() - plate->get_origin().y());
    //     imgui.text(buf);

    //     ImGui::SameLine();
    //     sprintf(buf, "%s%.3f", z.c_str(), position.z());
    //     imgui.text(buf);

    //     text_line = 1;
    // }

    // force extra frame to automatically update window size
    float window_width = ImGui::GetWindowWidth();
    if (window_width != last_window_width || text_line != last_text_line) {
        last_window_width = window_width;
        last_text_line = text_line;
#if ENABLE_ENHANCED_IMGUI_SLIDER_FLOAT
        imgui.set_requires_extra_frame();
#else
        wxGetApp().plater()->get_current_canvas3D()->set_as_dirty();
        wxGetApp().plater()->get_current_canvas3D()->request_extra_frame();
#endif // ENABLE_ENHANCED_IMGUI_SLIDER_FLOAT
    }


    ImGui::SetWindowFontScale(1.0f);
    imgui.end();
    ImGui::PopStyleVar(4);
    ImGui::PopStyleColor(4);
    imgui.pop_toolbar_style();
}

void GCodeViewer::GCodeWindow::load_gcode(const std::string& filename, const std::vector<size_t> &lines_ends)
{
    assert(! m_file.is_open());
    if (m_file.is_open())
        return;

    m_filename   = filename;
    m_lines_ends = lines_ends;

    m_selected_line_id = 0;
    m_last_lines_size = 0;

    try
    {
        m_file.open(boost::filesystem::path(m_filename));
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": mapping file " << m_filename;
    }
    catch (...)
    {
        BOOST_LOG_TRIVIAL(error) << "Unable to map file " << m_filename << ". Cannot show G-code window.";
        reset();
    }
}

void GCodeViewer::GCodeWindow::reset()
{
    stop_mapping_file();
    m_lines_ends.clear();
    m_lines_ends.shrink_to_fit();
    m_lines.clear();
    m_lines.shrink_to_fit();
    m_filename.clear();
    m_filename.shrink_to_fit();
}

void GCodeViewer::GCodeWindow::renderGcode(uint64_t curr_line_id, int canvas_width, int canvas_height, bool isReduceHeight)
{
    if (!wxGetApp().show_gcode_window() 
        || m_filename.empty() 
        || m_lines_ends.empty()
        || curr_line_id == 0)
        return;

    auto pos = ImGui::GetCursorPos();
    auto siz = ImGui::GetWindowSize();

    float wnd_height ;
    float scale = wxGetApp().plater()->get_current_canvas3D()->get_scale();
    auto screenPoxY = ImGui::GetCursorScreenPos().y;
    float canvas_h = wxGetApp().plater()->get_current_canvas3D()->get_canvas_size().get_height();
    if (isReduceHeight)
    {
        wnd_height = canvas_h - screenPoxY - GCODE_REDUCE_HEIGHT*scale;
    }
    else
    {
        wnd_height = canvas_h - screenPoxY;
    }

    const float zoom = 0.8f;
    //float wnd_height = siz.y - pos.y;
    const float text_height = ImGui::CalcTextSize("0").y* zoom;
    const ImGuiStyle& style = ImGui::GetStyle();
    uint64_t lines_count = static_cast<uint64_t>((wnd_height - 2.0f * style.WindowPadding.y + style.ItemSpacing.y) / (text_height + style.ItemSpacing.y));
    float sc = wxGetApp().plater()->get_current_canvas3D()->get_scale();
    if (lines_count == 0) {
        ImVec2 size = DispConfig().getWindowSize(DispConfig::e_wt_gcode,sc);
        if (size.y < 0.0f) {
            ImVec2 cur_pos = ImGui::GetCursorScreenPos();
            wnd_height = canvas_height - cur_pos.y;

            lines_count = static_cast<uint64_t>((wnd_height - 2.0f * style.WindowPadding.y + style.ItemSpacing.y) / (text_height + style.ItemSpacing.y));
            if (lines_count == 0)
                return;
        }
    }

    // visible range
    const uint64_t half_lines_count = lines_count / 2;
    uint64_t start_id = (curr_line_id >= half_lines_count) ? curr_line_id - half_lines_count : 0;
    uint64_t end_id = start_id + lines_count - 1;
    if (end_id >= static_cast<uint64_t>(m_lines_ends.size())) {
        end_id = static_cast<uint64_t>(m_lines_ends.size()) - 1;
        start_id = end_id - lines_count + 1;
    }

    // Orca: truncate long lines(>55 characters), add "..." at the end
    auto update_lines = [this](uint64_t start_id, uint64_t end_id) {
        std::vector<Line> ret;
        ret.reserve(end_id - start_id + 1);
        for (uint64_t id = start_id; id <= end_id; ++id) {
            // read line from file
            const size_t start = id == 1 ? 0 : m_lines_ends[id - 2];
            const size_t original_len = m_lines_ends[id - 1] - start;

            // fix bug[10385] task[2520] : 超长，不截断，改为另起一行
            std::string  gline(m_file.data() + start, original_len); 

            std::string command, parameters, comment;
            // extract comment
            std::vector<std::string> tokens;
            boost::split(tokens, gline, boost::is_any_of(";"), boost::token_compress_on);
            command = tokens.front();
            if (tokens.size() > 1)
                comment = ";" + tokens.back();

            // extract gcode command and parameters
            if (!command.empty()) {
                boost::split(tokens, command, boost::is_any_of(" "), boost::token_compress_on);
                command = tokens.front();
                if (tokens.size() > 1) {
                    for (size_t i = 1; i < tokens.size(); ++i) {
                        parameters += " " + tokens[i];
                    }
                }
            }
            ret.push_back({ command, parameters, comment });
        }
        return ret;
    };

    // updates list of lines to show, if needed
    if (m_selected_line_id != curr_line_id || m_last_lines_size != end_id - start_id + 1) {
        try
        {
            *const_cast<std::vector<Line>*>(&m_lines) = update_lines(start_id, end_id);
        }
        catch (...)
        {
            BOOST_LOG_TRIVIAL(error) << "Error while loading from file " << m_filename << ". Cannot show G-code window.";
            return;
        }
        *const_cast<uint64_t*>(&m_selected_line_id) = curr_line_id;
        *const_cast<size_t*>(&m_last_lines_size) = m_lines.size();
    }
    // line number's column width
    const float id_width = ImGui::CalcTextSize(std::to_string(end_id).c_str()).x;
    // center the text in the window by pushing down the first line
    const float f_lines_count = static_cast<float>(lines_count);
    ImGui::SetCursorPosY(0.5f * (wnd_height - f_lines_count * text_height - (f_lines_count - 1.0f) * style.ItemSpacing.y)+ pos.y);
    static const ImVec4 LINE_NUMBER_COLOR = ImGuiWrapper::COL_ORANGE_LIGHT;
    static const ImVec4 SELECTION_RECT_COLOR = ImGuiWrapper::COL_ORANGE_DARK;
    static const ImVec4 COMMAND_COLOR = { 0.8f, 0.8f, 0.0f, 1.0f };
    static const ImVec4 PARAMETERS_COLOR = m_is_dark ? ImVec4{ 1.0f, 1.0f, 1.0f, 1.0f } : ImVec4{ 0.0f, 0.0f, 0.0f, 1.0f };
    static const ImVec4 COMMENT_COLOR = m_is_dark ? ImVec4{ 0.7f, 0.7f, 0.7f, 1.0f } : ImVec4{ 0.0f, 0.0f, 0.0f, 0.7f };

    ImGuiWrapper& imgui = *wxGetApp().imgui();
    for (uint64_t id = start_id; id <= end_id; ++id) {
        const Line& line = m_lines[id - start_id];

        // rect around the current selected line
        if (id == curr_line_id) {
            //BBS: GUI refactor: move to right
            const float pos_y = ImGui::GetCursorScreenPos().y;
            const float pos_x = ImGui::GetCursorScreenPos().x;
            const float half_ItemSpacing_y = 0.5f * style.ItemSpacing.y;
            const float half_ItemSpacing_x = 0.5f * style.ItemSpacing.x;
            ImGui::GetWindowDrawList()->AddRect({ pos_x, pos_y - half_ItemSpacing_y },
                { pos_x+ImGui::GetCurrentWindow()->Size.x - half_ItemSpacing_x- 2*style.WindowPadding.x ,
                pos_y + text_height + half_ItemSpacing_y },
                ImGui::GetColorU32(SELECTION_RECT_COLOR));
        }

        // render line number
        const std::string id_str = std::to_string(id);
        // spacer to right align text
        ImGui::Dummy({ id_width - ImGui::CalcTextSize(id_str.c_str()).x* zoom, text_height });
        ImGui::SameLine(0.0f, 0.0f);
        float id_start_x = ImGui::GetCursorPosX() + id_width; // 行号右边界

        ImGui::PushStyleColor(ImGuiCol_Text, LINE_NUMBER_COLOR);
        DispConfig().boldText(id_str, zoom);
        //imgui.text(id_str);
        ImGui::PopStyleColor();

        if (!line.command.empty() || !line.comment.empty())
            ImGui::SameLine();

        // render command
        if (!line.command.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, COMMAND_COLOR);
            DispConfig().boldText(line.command, zoom);
            ImGui::PopStyleColor();
        }

        // render parameters
        if (!line.parameters.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, PARAMETERS_COLOR);

            // 计算行号和 command 的宽度
            float id_width_actual  = ImGui::CalcTextSize(std::to_string(id).c_str()).x * zoom;
            float command_width    = ImGui::CalcTextSize(line.command.c_str()).x * zoom;
            float parameters_width = ImGui::CalcTextSize(line.parameters.c_str()).x * zoom;

            float total_width  = id_width_actual + command_width + parameters_width + 3 * style.ItemSpacing.x;
            float window_width = ImGui::GetContentRegionAvail().x;

            // fix bug[10385] task[2520] : 超长，不截断，改为另起一行
            if (total_width < window_width) {
                ImGui::SameLine(0.0f, 0.0f);
                DispConfig().boldText(line.parameters, zoom);
            } else {
                // 设置光标位置对齐行号
                std:string s_param = line.parameters;
                if (!s_param.empty() && s_param.front() == ' ') {
                    s_param.erase(0, 1); // 去掉第一个空格
                }
                ImGui::SetCursorPosX(id_start_x ); // 对齐行号右边界
                float wrap_x = ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x ;
                DispConfig().boldTextWrapped(s_param, zoom, wrap_x);
            }

            ImGui::PopStyleColor();
        }

        // render comment
        if (!line.comment.empty()) {
            if (!line.command.empty())
                ImGui::SameLine(0.0f, 0.0f);
            ImGui::PushStyleColor(ImGuiCol_Text, COMMENT_COLOR);
            DispConfig().boldText(line.comment, zoom);
            ImGui::PopStyleColor();
        }
    }
}

void GCodeViewer::GCodeWindow::stop_mapping_file()
{
    //BBS: add log to trace the gcode file issue
    if (m_file.is_open()) {
        m_file.close();
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ": finished mapping file " << m_filename;
    }
}

const std::vector<double>& GCodeViewer::FilterLayerResult::rebuild_filter_layers_z_offset(
    const GCodeViewer* viewer, const int dynamic_stride, const std::array<unsigned int, 2>& layers_z_range, const Layers& layers)
{
    double              filtered_z = 0.0;
    std::vector<size_t> filtered_layers;

    m_layers_z_offset.resize(layers.size(), 0.0);
    m_layers_z_offset.shrink_to_fit();

    for (size_t i = layers_z_range[0]; i <= layers_z_range[1]; i++) {
        if (viewer->should_be_filtered_of_layer(dynamic_stride, i)) {
            filtered_z += layers.get_z_at(i) - layers.get_z_at(i - 1);
            filtered_layers.push_back(i);
        }
    }

    double gap   = filtered_z / (layers_z_range[1] - layers_z_range[0] - filtered_layers.size()); // gap between retain layers
    double acc_z = layers.get_z_at(layers_z_range[0] - 1);

    // calculate the offset of each retain layers
    size_t xxd = 0;
    for (size_t i = layers_z_range[0]; i <= layers_z_range[1]; i++) {
#if 0
            auto it = std::find_if(filtered_layers.begin(), filtered_layers.end(), [i](size_t layer) { return layer == i; });
            
			if (it == filtered_layers.end()) {
                //layer i be retain, otherwise will be filtered
                double origin_z = layers.get_z_at(i);
                acc_z += origin_z - layers.get_z_at(i - 1);  
				
				double offset = acc_z - origin_z;
				
				acc_z += gap;
                m_layers_z_offset[i] = offset;
            }
#else
        bool be_filter = false;
        for (size_t j = xxd; j < filtered_layers.size(); j++) {
            const size_t& x = filtered_layers[j];
            if (x == i) {
                be_filter = true;
                xxd       = j + 1;
                break;
            } else if (x > i) {
                break;
            }
        }

        if (!be_filter) {
            double origin_z = layers.get_z_at(i);
            acc_z += origin_z - layers.get_z_at(i - 1);

            /*double offset = acc_z - origin_z;
            acc_z += gap;
            m_layers_z_offset[i] = offset;*/

            m_layers_z_offset[i] = acc_z - origin_z;
            acc_z += gap;
        }
#endif
    }

    // capture build parameters, it may include layers, but layers may to big
    m_dynamic_stride = dynamic_stride;
    m_layers_z_range = layers_z_range;
    m_valid          = true;

    return m_layers_z_offset;
}	


GCodeViewer::GCodeViewer()
{
    m_moves_slider  = new IMSlider(0, 0, 0, 100, wxSL_HORIZONTAL);
    m_layers_slider = new IMSlider(0, 0, 0, 100, wxSL_VERTICAL);
    m_cliper_slider = new IMSlider(0, 100, 0, 100, wxSL_VERTICAL);
    m_extrusions.reset_role_visibility_flags();
    m_moves_slider->set_animate_tick([this](bool manual) {

        const int cur_move = m_moves_slider->GetHigherValue();
        const int max_move = m_moves_slider->GetMaxValue();
        
        if (cur_move < max_move) 
        {
            const int next = cur_move + 1.0;
            m_moves_slider->SetHigherValue(next);
            m_moves_slider->set_as_dirty(true);
        
        } else {
        
            const int cur_layer = m_layers_slider->GetHigherValue();
            const int max_layer = m_layers_slider->GetMaxValue();
            if (cur_layer < max_layer) 
            {
                const int next_layer = cur_layer + 1.0;
                m_layers_slider->SetHigherValue(next_layer); 
                set_layers_z_range({static_cast<unsigned int>(m_layers_slider->GetLowerValue()),
                                                   static_cast<unsigned int>(m_layers_slider->GetHigherValue())});
                
                update_moves_slider(false);
                m_layers_slider->set_as_dirty(false);
                m_moves_slider->SetHigherValue(m_moves_slider->GetMinValue());
                m_moves_slider->set_as_dirty(true);

            } else {
                
                if (manual) {
                    //restart
                    const int first = m_layers_slider->GetMinValue();
                    m_layers_slider->SetHigherValue(first);
                    set_layers_z_range({static_cast<unsigned int>(m_layers_slider->GetLowerValue()),
                                        static_cast<unsigned int>(m_layers_slider->GetHigherValue())});

                    update_moves_slider(false);
                    m_layers_slider->set_as_dirty(false);
                    m_moves_slider->SetHigherValue(m_moves_slider->GetMinValue());
                    m_moves_slider->set_as_dirty(true);

                } else {
                    // stop
                    m_moves_slider->set_animating(false);
                }
            }
        }
        

        int layers = m_layers_slider->GetHigherValue() + 1.0;

    });
}

GCodeViewer::~GCodeViewer()
{
    reset();
    if (m_moves_slider) {
        delete m_moves_slider;
        m_moves_slider = nullptr;
    }
    if (m_layers_slider) {
        delete m_layers_slider;
        m_layers_slider = nullptr;
    }
    if (m_cliper_slider) {
        delete m_cliper_slider;
        m_cliper_slider = nullptr;
    }
}

void GCodeViewer::init(ConfigOptionMode mode, PresetBundle* preset_bundle,bool isgcode)
{
    if (m_gl_data_initialized)
        return;

    if (isgcode)
    {
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(": enter, m_buffers.size=%1%") % m_buffers.size();
        // initializes opengl data of TBuffers
        for (size_t i = 0; i < m_buffers.size(); ++i) {
            TBuffer&  buffer = m_buffers[i];
            EMoveType type   = buffer_type(i);
            switch (type) {
            default: {
                break;
            }
            case EMoveType::Tool_change:
            case EMoveType::Color_change:
            case EMoveType::Pause_Print:
            case EMoveType::Custom_GCode:
            case EMoveType::Retract:
            case EMoveType::Unretract:
            case EMoveType::Seam: {
                //            if (wxGetApp().is_gl_version_greater_or_equal_to(3, 3)) {
                //                buffer.render_primitive_type = TBuffer::ERenderPrimitiveType::InstancedModel;
                //                buffer.shader = "gouraud_light_instanced";
                //                buffer.model.model.init_from(diamond(16));
                //                buffer.model.color = option_color(type);
                //                buffer.model.instances.format = InstanceVBuffer::EFormat::InstancedModel;
                //            }
                //            else {
                if (type == EMoveType::Seam)
                    buffer.visible = true;

                buffer.render_primitive_type = TBuffer::ERenderPrimitiveType::BatchedModel;
                buffer.vertices.format       = VBuffer::EFormat::PositionNormal3;
                buffer.shader                = "gouraud_light";

                buffer.model.data             = diamond(16);
                buffer.model.color            = option_color(type);
                buffer.model.instances.format = InstanceVBuffer::EFormat::BatchedModel;
                //            }
                break;
            }
            case EMoveType::Extrude_Alter:
            case EMoveType::Wipe:
            case EMoveType::Extrude: {
                buffer.render_primitive_type = TBuffer::ERenderPrimitiveType::Triangle;
                buffer.vertices.format       = VBuffer::EFormat::PositionNormal3;
                buffer.shader                = "gouraud_light";
                break;
            }
            case EMoveType::Travel: {
                buffer.render_primitive_type = TBuffer::ERenderPrimitiveType::Line;
                buffer.vertices.format       = VBuffer::EFormat::Position;
                buffer.shader                = "flat";
                break;
            }
            }

            set_toolpath_move_type_visible(EMoveType::Extrude, true);
        }

        // initializes tool marker
        std::string filename;
        if (preset_bundle != nullptr) {
            const Preset* curr = &preset_bundle->printers.get_selected_preset();
            if (curr->is_system)
                filename = PresetUtils::system_printer_hotend_model(*curr);
            else {
                auto* printer_model = curr->config.opt<ConfigOptionString>("printer_model");
                if (printer_model != nullptr && !printer_model->value.empty()) {
                    filename = preset_bundle->get_hotend_model_for_printer_model(printer_model->value);
                }

                if (filename.empty()) {
                    filename = preset_bundle->get_hotend_model_for_printer_model(PresetBundle::BBL_DEFAULT_PRINTER_MODEL);
                }
            }
        }

        marker.init(filename);

        // initializes point sizes
        std::array<int, 2> point_sizes;
        ::glGetIntegerv(GL_ALIASED_POINT_SIZE_RANGE, point_sizes.data());
        m_detected_point_sizes = {static_cast<float>(point_sizes[0]), static_cast<float>(point_sizes[1])};

        // BBS initialzed view_type items
        m_user_mode = mode;
        update_by_mode(m_user_mode);
        m_moves_slider->init_texture(true);
    } else {
        m_cliper_slider->init_texture(false);
    }
    m_gl_data_initialized = true;
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(": finished");
}

void GCodeViewer::on_change_color_mode(bool is_dark) {
    m_is_dark = is_dark;
    marker.on_change_color_mode(m_is_dark);
    gcode_window.on_change_color_mode(m_is_dark);
}

void GCodeViewer::set_scale(float scale)
{
    if (m_layers_slider)
        m_layers_slider->set_scale(scale * GCODE_VIEWER_SLIDER_SCALE);
    if (m_moves_slider)
        m_moves_slider->set_scale(scale * GCODE_VIEWER_SLIDER_SCALE);
    if (m_cliper_slider)
        m_cliper_slider->set_scale(scale * GCODE_VIEWER_SLIDER_SCALE);
    m_scale = scale;
    marker.m_scale = scale;
}

void GCodeViewer::update_by_mode(ConfigOptionMode mode)
{
    view_type_items.clear();
    view_type_items_str.clear();
    options_items.clear();

    // BBS initialzed view_type items
    view_type_items.push_back(EViewType::FeatureType);
    // Creality:for appearance shortage
#if ENABLE_AUE_CUSTOM_PREVIEW
    if (!m_only_gcode_in_preview)
        view_type_items.push_back(EViewType::Custom);
#endif // ENABLE_AUE_CUSTOM_PREVIEW
    view_type_items.push_back(EViewType::ColorPrint);
    view_type_items.push_back(EViewType::Feedrate);
    view_type_items.push_back(EViewType::Height);
    view_type_items.push_back(EViewType::Width);
    view_type_items.push_back(EViewType::VolumetricRate);
    view_type_items.push_back(EViewType::LayerTime);
    view_type_items.push_back(EViewType::LayerTimeLog);
    view_type_items.push_back(EViewType::FanSpeed);
    view_type_items.push_back(EViewType::Temperature);
    view_type_items.push_back(EViewType::Acceleration);
    //if (mode == ConfigOptionMode::comDevelop) {
    //    view_type_items.push_back(EViewType::Tool);
    //}

    for (int i = 0; i < view_type_items.size(); i++) {
        view_type_items_str.push_back(get_view_type_string(view_type_items[i]));
    }

    // Creality:for appearance shortage
    {
        auto it = std::find(view_type_items.begin(), view_type_items.end(), m_view_type);
        if (it != view_type_items.end())
            m_view_type_sel = static_cast<int>(std::distance(view_type_items.begin(), it));
        else {
            m_view_type_sel = 0;
            set_view_type(view_type_items.front());
        }
    }

    // BBS for first layer inspection
    view_type_items.push_back(EViewType::FilamentId);

    options_items.push_back(EMoveType::Travel);
    options_items.push_back(EMoveType::Retract);
    options_items.push_back(EMoveType::Unretract);
    options_items.push_back(EMoveType::Wipe);
    //if (mode == ConfigOptionMode::comDevelop) {
    //    options_items.push_back(EMoveType::Tool_change);
    //}
    //BBS: seam is not real move and extrusion, put at last line
    options_items.push_back(EMoveType::Seam);
}

std::vector<int> GCodeViewer::get_plater_extruder()
{
    return m_plater_extruder;
}

//BBS: always load shell at preview
void GCodeViewer::load(const GCodeProcessorResult& gcode_result, const Print& print, const BuildVolume& build_volume,
                const std::vector<BoundingBoxf3>& exclude_bounding_box, ConfigOptionMode mode, bool only_gcode)
{
    BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << " start";
    system_memory_stats(__FUNCTION__);
    m_is_belt = gcode_result.machine_is_belt;

    // avoid processing if called with the same gcode_result
    if (m_last_result_id == gcode_result.id) {
        //BBS: add logs
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(": the same id %1%, return directly, result %2% ") % m_last_result_id % (&gcode_result);
        return;
    }

    ResGuard res([&]
        {
            m_bLoaded = false;
            if (!only_gcode)
            {
                wxGetApp().notification_manager()->set_slicing_progress_began();
                print.set_status(95, _u8L("Loading display data"));
                wxGetApp().process_msg_loop();
            }
        },
        [&]
        {
            if (!only_gcode)
            {
                wxGetApp().notification_manager()->set_slicing_progress_hidden();
                wxGetApp().process_msg_loop();
            }
            m_bLoaded = true;
        });

    //BBS: add logs
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(": gcode result %1%, new id %2%, gcode file %3% ") % (&gcode_result) % m_last_result_id % gcode_result.filename;

    // release gpu memory, if used
    reset();

    //BBS: add mutex for protection of gcode result
    gcode_result.lock();
    //BBS: add safe check
    if (gcode_result.moves.size() == 0) {
        //result cleaned before slicing ,should return here
        BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << boost::format(": gcode result reset before, return directly!");
        gcode_result.unlock();
        return;
    }

	PartPlate* current_plate = wxGetApp().plater()->get_partplate_list().get_curr_plate();
    bool current_has_print_instances = current_plate->has_printable_instances();
    bool only_gcode_3mf = current_plate->is_slice_result_valid() && wxGetApp().model().objects.empty() && !current_has_print_instances;

    //BBS: move the id to the end of reset
    m_last_result_id = gcode_result.id;
    m_gcode_result = &gcode_result;
    m_only_gcode_in_preview = only_gcode || only_gcode_3mf;
    // Creality:for appearance shortage
    m_user_mode = mode;
    update_by_mode(mode);
    m_contentWidth = -1.0f;

    gcode_window.load_gcode(gcode_result.filename, gcode_result.lines_ends);

    //BBS: add only gcode mode
    //if (wxGetApp().is_gcode_viewer())
    if (m_only_gcode_in_preview)
        m_custom_gcode_per_print_z = gcode_result.custom_gcode_per_print_z;

    m_max_print_height = gcode_result.printable_height;
    PresetBundle* preset_bundle   = wxGetApp().preset_bundle;
    bool          machine_is_belt = preset_bundle->printers.get_edited_preset().config.opt_bool("machine_is_belt");
    if (machine_is_belt) {
        m_max_print_height = 0.0f;
    }

    load_toolpaths(gcode_result, build_volume, exclude_bounding_box);

    //BBS: add mutex for protection of gcode result
    if (m_layers.empty()) {
        gcode_result.unlock();
        return;
    }

    m_settings_ids = gcode_result.settings_ids;
    m_filament_diameters = gcode_result.filament_diameters;
    m_filament_densities = gcode_result.filament_densities;

    if (m_only_gcode_in_preview) {
        Pointfs printable_area;
        //BBS: add bed exclude area
        Pointfs bed_exclude_area = Pointfs();
        std::string texture;
        std::string model;

        if (!gcode_result.printable_area.empty()) {
            // bed shape detected in the gcode
            printable_area    = gcode_result.printable_area;
            const auto    bundle = wxGetApp().preset_bundle;
            const Preset* preset = nullptr;
            if (bundle != nullptr && !gcode_result.printer_model.empty()) {
                // Creality: find in all presets
                auto&         ppresets = bundle->printers.get_presets();
                for (const Preset& p : ppresets) {
                    if (p.name.find(gcode_result.printer_model) != std::string::npos) {
                        preset = &p;
                        break;
                    }
                }
            }

            //BBS: add bed exclude area
            if (!gcode_result.bed_exclude_area.empty())
                bed_exclude_area = gcode_result.bed_exclude_area;

            //Creality: update bed only by printer_model
            if (preset)
                wxGetApp().plater()->set_gcode_bed_shape(printable_area, bed_exclude_area, gcode_result.printable_height, *preset);
            else
                wxGetApp().plater()->set_bed_shape(printable_area, bed_exclude_area, gcode_result.printable_height, "", "", true);  
                
        }
    }

    m_print_statistics = gcode_result.print_statistics;

    if (m_time_estimate_mode != PrintEstimatedStatistics::ETimeMode::Normal) {
        const float time = m_print_statistics.modes[static_cast<size_t>(m_time_estimate_mode)].time;
        if (time == 0.0f ||
            short_time(get_time_dhms(time)) == short_time(get_time_dhms(m_print_statistics.modes[static_cast<size_t>(PrintEstimatedStatistics::ETimeMode::Normal)].time)))
            m_time_estimate_mode = PrintEstimatedStatistics::ETimeMode::Normal;
    }

    // set to color print by default if use multi extruders
    if (m_extruder_ids.size() > 1) {
        for (int i = 0; i < view_type_items.size(); i++) {
            if (view_type_items[i] == EViewType::ColorPrint) {
                m_view_type_sel = i;
                break;
            }
        }

        set_view_type(EViewType::ColorPrint);
    }

    m_fold = false;
    _on_set_fold(false);

    if (only_gcode_3mf) {
        wxGetApp().plater()->set_only_gcode(true);
        wxGetApp().plater()->check_sidebar_state_in_only_gcode_mode();
    }
    m_layers_slider->set_menu_enable(!(only_gcode || only_gcode_3mf));
    m_layers_slider->set_as_dirty();
    m_moves_slider->set_as_dirty();

    //BBS
    m_conflict_result = gcode_result.conflict_result;
    if (m_conflict_result) { m_conflict_result.value().layer = m_layers.get_l_at(m_conflict_result.value()._height); }

    m_toolpath_outside_result = gcode_result.toolpath_outside_result;

    //BBS: add mutex for protection of gcode result
    gcode_result.unlock();
    //BBS: add logs
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(": finished, m_buffers size %1%!")%m_buffers.size();
    BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << " error";
}

void GCodeViewer::refresh(const GCodeProcessorResult& gcode_result, const std::vector<std::string>& str_tool_colors)
{
    if (!m_bLoaded) {
        return;
    }

	if (m_gcode_result == nullptr) {
		return;
	}

	if (m_last_result_id != gcode_result.id || m_gcode_result != &gcode_result) {
		return;
	}

#if ENABLE_GCODE_VIEWER_STATISTICS
    auto start_time = std::chrono::high_resolution_clock::now();
#endif // ENABLE_GCODE_VIEWER_STATISTICS

    //BBS: add mutex for protection of gcode result
    gcode_result.lock();

    //BBS: add safe check
    if (gcode_result.moves.size() == 0) {
        //result cleaned before slicing ,should return here
        BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << boost::format(": gcode result reset before, return directly!");
        gcode_result.unlock();
        return;
    }

    //BBS: add mutex for protection of gcode result
    if (m_moves_count == 0) {
        BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << boost::format(": gcode result m_moves_count is 0, return directly!");
        gcode_result.unlock();
        return;
    }

    wxBusyCursor busy;

    if ((m_view_type == EViewType::Tool
#if ENABLE_AUE_CUSTOM_PREVIEW
         || m_view_type == EViewType::Custom
#endif // ENABLE_AUE_CUSTOM_PREVIEW
        ) && !gcode_result.extruder_colors.empty()) {
        // update tool colors from config stored in the gcode
        decode_colors(gcode_result.extruder_colors, m_tools.m_tool_colors);
        m_tools.m_tool_visibles = std::vector<bool>(m_tools.m_tool_colors.size());
        for (auto item: m_tools.m_tool_visibles) item = true;
    }
    else {
        // update tool colors
        decode_colors(str_tool_colors, m_tools.m_tool_colors);
        m_tools.m_tool_visibles = std::vector<bool>(m_tools.m_tool_colors.size());
        for (auto item : m_tools.m_tool_visibles) item = true;
    }

    for (int i = 0; i < m_tools.m_tool_colors.size(); i++) {
        m_tools.m_tool_colors[i] = adjust_color_for_rendering(m_tools.m_tool_colors[i]);
    }
    ColorRGBA default_color;
    decode_color("#FF8000", default_color);
	// ensure there are enough colors defined
    while (m_tools.m_tool_colors.size() < std::max(size_t(1), gcode_result.extruders_count)) {
        m_tools.m_tool_colors.push_back(default_color);
        m_tools.m_tool_visibles.push_back(true);
    }

    // update ranges for coloring / legend
    m_extrusions.reset_ranges();
    for (size_t i = 0; i < m_moves_count; ++i) {
        // skip first vertex
        if (i == 0)
            continue;

        const GCodeProcessorResult::MoveVertex& curr = gcode_result.moves[i];

        switch (curr.type)
        {
        case EMoveType::Extrude:
        {
            m_extrusions.ranges.height.update_from(round_to_bin(curr.height));
            m_extrusions.ranges.width.update_from(round_to_bin(curr.width));
            m_extrusions.ranges.fan_speed.update_from(curr.fan_speed);
            m_extrusions.ranges.temperature.update_from(curr.temperature);
            if (curr.extrusion_role != erCustom || is_visible(erCustom))
                m_extrusions.ranges.volumetric_rate.update_from(round_to_bin(curr.volumetric_rate()));

            if (curr.layer_duration > 0.f) {
                m_extrusions.ranges.layer_duration.update_from(curr.layer_duration);
m_extrusions.ranges.layer_duration_log.update_from(curr.layer_duration);
            }
            m_extrusions.ranges.acceleration.update_from(curr.acceleration);
            [[fallthrough]];
        }
        case EMoveType::Travel:
        {
            if (m_buffers[buffer_id(curr.type)].visible)
                m_extrusions.ranges.feedrate.update_from(curr.feedrate);

            break;
        }
        default: { break; }
        }
    }

#if ENABLE_GCODE_VIEWER_STATISTICS
    m_statistics.refresh_time = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - start_time).count();
#endif // ENABLE_GCODE_VIEWER_STATISTICS

    //BBS: add mutex for protection of gcode result
    gcode_result.unlock();

    // update buffers' render paths
    refresh_render_paths();
    log_memory_used("Refreshed G-code extrusion paths, ");
}

void GCodeViewer::refresh_render_paths()
{
    refresh_render_paths(false, false);
}

void GCodeViewer::update_shells_color_by_extruder(const DynamicPrintConfig *config)
{
    if (config != nullptr)
        m_shells.volumes.update_colors_by_extruder(config, false);
}

void GCodeViewer::set_shell_transparency(float alpha) { m_shells.volumes.set_transparency(alpha); }

//BBS: always load shell at preview
void GCodeViewer::reset_shell()
{
    m_shells.volumes.clear();
    m_shells.print_id = -1;
    m_shell_bounding_box = BoundingBoxf3();
}

void GCodeViewer::reset()
{
    //BBS: should also reset the result id
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(": current result id %1% ")%m_last_result_id;
    m_last_result_id = -1;
    //BBS: add only gcode mode
    m_only_gcode_in_preview = false;
	m_gcode_result = nullptr;
    m_is_lite_mode = false;

    m_moves_count = 0;
    m_ssid_to_moveid_map.clear();
    m_ssid_to_moveid_map.shrink_to_fit();
    m_ssid_arc_extra_segments.clear();
    m_ssid_arc_extra_segments.shrink_to_fit();
	m_sid_to_moveid.clear();
    m_sid_to_moveid.shrink_to_fit();
    for (TBuffer& buffer : m_buffers) {
        buffer.reset();
    }
    m_paths_bounding_box = BoundingBoxf3();
    m_printable_area_box    = BoundingBoxf3();
    m_max_bounding_box = BoundingBoxf3();
    m_max_print_height = 0.0f;
    m_tools.m_tool_colors = std::vector<ColorRGBA>();
    m_tools.m_tool_visibles = std::vector<bool>();
    m_extruders_count = 0;
    m_extruder_ids = std::vector<unsigned char>();
    m_filament_diameters = std::vector<float>();
    m_filament_densities = std::vector<float>();
    m_extrusions.reset_ranges();
    //BBS: always load shell at preview
    //m_shells.volumes.clear();
    m_layers.reset();
    m_layers_z_range = { 0, 0 };
    m_roles = std::vector<ExtrusionRole>();
    m_print_statistics.reset();
    m_custom_gcode_per_print_z = std::vector<CustomGCode::Item>();
    gcode_window.reset();
#if ENABLE_GCODE_VIEWER_STATISTICS
    m_statistics.reset_all();
#endif // ENABLE_GCODE_VIEWER_STATISTICS
    m_contained_in_bed = true;
    m_filter_layer_result.reset();
    m_top_surface_layer = std::set<int>();
    m_contentWidth = -1.0f;
    m_custom_interest_cache_result_id = 0;
    m_custom_interest_by_ssid.clear();
}


#define ENABLE_CALIBRATION_THUMBNAIL_OUTPUT 0
#if ENABLE_CALIBRATION_THUMBNAIL_OUTPUT
static void debug_calibration_output_thumbnail(const ThumbnailData& thumbnail_data)
{
    // debug export of generated image
    wxImage image(thumbnail_data.width, thumbnail_data.height);
    image.InitAlpha();

    for (unsigned int r = 0; r < thumbnail_data.height; ++r)
    {
        unsigned int rr = (thumbnail_data.height - 1 - r) * thumbnail_data.width;
        for (unsigned int c = 0; c < thumbnail_data.width; ++c)
        {
            unsigned char* px = (unsigned char*)thumbnail_data.pixels.data() + 4 * (rr + c);
            image.SetRGB((int)c, (int)r, px[0], px[1], px[2]);
            image.SetAlpha((int)c, (int)r, px[3]);
        }
    }

    image.SaveFile("D:/calibrate.png", wxBITMAP_TYPE_PNG);
}
#endif

void GCodeViewer::_render_calibration_thumbnail_internal(ThumbnailData& thumbnail_data, const ThumbnailsParams& thumbnail_params, PartPlateList& partplate_list, OpenGLManager& opengl_manager)
{
    int plate_idx = thumbnail_params.plate_id;
    PartPlate* plate = partplate_list.get_plate(plate_idx);
    BoundingBoxf3 plate_box = plate->get_bounding_box(false);
    plate_box.min.z() = 0.0;
    plate_box.max.z() = 0.0;
    Vec3d center = plate_box.center();

#if 1
    Camera camera;
    camera.set_viewport(0, 0, thumbnail_data.width, thumbnail_data.height);
    camera.apply_viewport();
    camera.set_scene_box(plate_box);
    camera.set_type(Camera::EType::Ortho);
    camera.set_target(center);
    camera.select_view("top");
    camera.zoom_to_box(plate_box, 1.0f);
    camera.apply_projection(plate_box);

    auto render_as_triangles = [
#if ENABLE_GCODE_VIEWER_STATISTICS
        this
#endif // ENABLE_GCODE_VIEWER_STATISTICS
    ](TBuffer &buffer, std::vector<RenderPath>::iterator it_path, std::vector<RenderPath>::iterator it_end, GLShaderProgram& shader, int uniform_color) {
        for (auto it = it_path; it != it_end && it_path->ibuffer_id == it->ibuffer_id; ++it) {
            const RenderPath& path = *it;
            // Some OpenGL drivers crash on empty glMultiDrawElements, see GH #7415.
            assert(!path.sizes.empty());
            assert(!path.offsets.empty());
            shader.set_uniform(uniform_color, path.color);
            glsafe(::glMultiDrawElements(GL_TRIANGLES, (const GLsizei*)path.sizes.data(), GL_UNSIGNED_SHORT, (const void* const*)path.offsets.data(), (GLsizei)path.sizes.size()));
#if ENABLE_GCODE_VIEWER_STATISTICS
            ++m_statistics.gl_multi_triangles_calls_count;
#endif // ENABLE_GCODE_VIEWER_STATISTICS
        }
    };

    auto render_as_instanced_model = [
#if ENABLE_GCODE_VIEWER_STATISTICS
        this
#endif // ENABLE_GCODE_VIEWER_STATISTICS
    ](TBuffer& buffer, GLShaderProgram& shader) {
        for (auto& range : buffer.model.instances.render_ranges.ranges) {
            if (range.vbo == 0 && range.count > 0) {
                glsafe(::glGenBuffers(1, &range.vbo));
                glsafe(::glBindBuffer(GL_ARRAY_BUFFER, range.vbo));
                glsafe(::glBufferData(GL_ARRAY_BUFFER, range.count * buffer.model.instances.instance_size_bytes(), (const void*)&buffer.model.instances.buffer[range.offset * buffer.model.instances.instance_size_floats()], GL_STATIC_DRAW));
                glsafe(::glBindBuffer(GL_ARRAY_BUFFER, 0));
            }

            if (range.vbo > 0) {
                buffer.model.model.set_color(range.color);
                buffer.model.model.render_instanced(range.vbo, range.count);
#if ENABLE_GCODE_VIEWER_STATISTICS
                ++m_statistics.gl_instanced_models_calls_count;
                m_statistics.total_instances_gpu_size += static_cast<int64_t>(range.count * buffer.model.instances.instance_size_bytes());
#endif // ENABLE_GCODE_VIEWER_STATISTICS
            }
        }
    };

#if ENABLE_GCODE_VIEWER_STATISTICS
    auto render_as_batched_model = [this](TBuffer& buffer, GLShaderProgram& shader, int position_id, int normal_id) {
#else
    auto render_as_batched_model = [](TBuffer& buffer, GLShaderProgram& shader, int position_id, int normal_id) {
#endif // ENABLE_GCODE_VIEWER_STATISTICS

        struct Range
        {
            unsigned int first;
            unsigned int last;
            bool intersects(const Range& other) const { return (other.last < first || other.first > last) ? false : true; }
        };
        Range buffer_range = { 0, 0 };
        size_t indices_per_instance = buffer.model.data.indices_count();

        for (size_t j = 0; j < buffer.indices.size(); ++j) {
            const IBuffer& i_buffer = buffer.indices[j];
            buffer_range.last = buffer_range.first + i_buffer.count / indices_per_instance;
            glsafe(::glBindBuffer(GL_ARRAY_BUFFER, i_buffer.vbo));
            if (position_id != -1) {
                glsafe(::glVertexAttribPointer(position_id, buffer.vertices.position_size_floats(), GL_FLOAT, GL_FALSE, buffer.vertices.vertex_size_bytes(), (const void*)buffer.vertices.position_offset_bytes()));
                glsafe(::glEnableVertexAttribArray(position_id));
            }
            bool has_normals = buffer.vertices.normal_size_floats() > 0;
            if (has_normals) {
                if (normal_id != -1) {
                    glsafe(::glVertexAttribPointer(normal_id, buffer.vertices.normal_size_floats(), GL_FLOAT, GL_FALSE, buffer.vertices.vertex_size_bytes(), (const void*)buffer.vertices.normal_offset_bytes()));
                    glsafe(::glEnableVertexAttribArray(normal_id));
                }
            }

            glsafe(::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, i_buffer.ibo));

            for (auto& range : buffer.model.instances.render_ranges.ranges) {
                Range range_range = { range.offset, range.offset + range.count };
                if (range_range.intersects(buffer_range)) {
                    shader.set_uniform("uniform_color", range.color);
                    unsigned int offset = (range_range.first > buffer_range.first) ? range_range.first - buffer_range.first : 0;
                    size_t offset_bytes = static_cast<size_t>(offset) * indices_per_instance * sizeof(IBufferType);
                    Range render_range = { std::max(range_range.first, buffer_range.first), std::min(range_range.last, buffer_range.last) };
                    size_t count = static_cast<size_t>(render_range.last - render_range.first) * indices_per_instance;
                    if (count > 0) {
                        glsafe(::glDrawElements(GL_TRIANGLES, (GLsizei)count, GL_UNSIGNED_SHORT, (const void*)offset_bytes));
#if ENABLE_GCODE_VIEWER_STATISTICS
                        ++m_statistics.gl_batched_models_calls_count;
#endif // ENABLE_GCODE_VIEWER_STATISTICS
                    }
                }
            }

            glsafe(::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0));
            
            if (normal_id != -1)
                glsafe(::glDisableVertexAttribArray(normal_id));
            if (position_id != -1)
                glsafe(::glDisableVertexAttribArray(position_id));
            glsafe(::glBindBuffer(GL_ARRAY_BUFFER, 0));

            buffer_range.first = buffer_range.last;
        }
    };

    unsigned char begin_id = buffer_id(EMoveType::Retract);
    unsigned char end_id = buffer_id(EMoveType::Count);

    BOOST_LOG_TRIVIAL(info) << boost::format("render_calibration_thumbnail: begin_id %1%, end_id %2%")%begin_id %end_id;
    for (unsigned char i = begin_id; i < end_id; ++i) {
        TBuffer& buffer = m_buffers[i];
        if (!buffer.visible || !buffer.has_data())
            continue;

        GLShaderProgram* shader = opengl_manager.get_shader("flat");
        if (shader != nullptr) {
            shader->start_using();

            shader->set_uniform("view_model_matrix", camera.get_view_matrix());
            shader->set_uniform("projection_matrix", camera.get_projection_matrix());
            int position_id = shader->get_attrib_location("v_position");
            int normal_id   = shader->get_attrib_location("v_normal");

            if (buffer.render_primitive_type == TBuffer::ERenderPrimitiveType::InstancedModel) {
                //shader->set_uniform("emission_factor", 0.25f);
                render_as_instanced_model(buffer, *shader);
                //shader->set_uniform("emission_factor", 0.0f);
            }
            else if (buffer.render_primitive_type == TBuffer::ERenderPrimitiveType::BatchedModel) {
                //shader->set_uniform("emission_factor", 0.25f);
                render_as_batched_model(buffer, *shader, position_id, normal_id);
                //shader->set_uniform("emission_factor", 0.0f);
            }
            else {
                switch (buffer.render_primitive_type) {
                default: break;
                }
                int uniform_color = shader->get_uniform_location("uniform_color");
                auto it_path = buffer.render_paths.begin();
                for (unsigned int ibuffer_id = 0; ibuffer_id < static_cast<unsigned int>(buffer.indices.size()); ++ibuffer_id) {
                    const IBuffer& i_buffer = buffer.indices[ibuffer_id];
                    // Skip all paths with ibuffer_id < ibuffer_id.
                    for (; it_path != buffer.render_paths.end() && it_path->ibuffer_id < ibuffer_id; ++it_path);
                    if (it_path == buffer.render_paths.end() || it_path->ibuffer_id > ibuffer_id)
                        // Not found. This shall not happen.
                        continue;

                    glsafe(::glBindBuffer(GL_ARRAY_BUFFER, i_buffer.vbo));
                    if (position_id != -1) {
                        glsafe(::glVertexAttribPointer(position_id, buffer.vertices.position_size_floats(), GL_FLOAT, GL_FALSE, buffer.vertices.vertex_size_bytes(), (const void*)buffer.vertices.position_offset_bytes()));
                        glsafe(::glEnableVertexAttribArray(position_id));
                    }
                    bool has_normals = false;// buffer.vertices.normal_size_floats() > 0;
                    if (has_normals) {
                        if (normal_id != -1) {
                            glsafe(::glVertexAttribPointer(normal_id, buffer.vertices.normal_size_floats(), GL_FLOAT, GL_FALSE, buffer.vertices.vertex_size_bytes(), (const void*)buffer.vertices.normal_offset_bytes()));
                            glsafe(::glEnableVertexAttribArray(normal_id));
                        }
                    }

                    glsafe(::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, i_buffer.ibo));

                    // Render all elements with it_path->ibuffer_id == ibuffer_id, possible with varying colors.
                    switch (buffer.render_primitive_type)
                    {
                    case TBuffer::ERenderPrimitiveType::Triangle: {
                        render_as_triangles(buffer, it_path, buffer.render_paths.end(), *shader, uniform_color);
                        break;
                    }
                    default: { break; }
                    }

                    glsafe(::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0));
                    
                    if (normal_id != -1)
                        glsafe(::glDisableVertexAttribArray(normal_id));
                    if (position_id != -1)
                        glsafe(::glDisableVertexAttribArray(position_id));

                    glsafe(::glBindBuffer(GL_ARRAY_BUFFER, 0));
                }
            }

            shader->stop_using();
        }
        else {
            BOOST_LOG_TRIVIAL(info) << boost::format("render_calibration_thumbnail: can not find shader");
        }
    }
#endif
    BOOST_LOG_TRIVIAL(info) << boost::format("render_calibration_thumbnail: exit");

}

void GCodeViewer::_render_calibration_thumbnail_framebuffer(ThumbnailData& thumbnail_data, unsigned int w, unsigned int h, const ThumbnailsParams& thumbnail_params, PartPlateList& partplate_list, OpenGLManager& opengl_manager)
{
    BOOST_LOG_TRIVIAL(info) << boost::format("render_calibration_thumbnail prepare: width %1%, height %2%")%w %h;
    thumbnail_data.set(w, h);
    if (!thumbnail_data.is_valid())
        return;

    //TODO bool multisample = m_multisample_allowed;
    bool multisample = OpenGLManager::can_multisample();
    //if (!multisample)
    //    glsafe(::glEnable(GL_MULTISAMPLE));

    GLint max_samples;
    glsafe(::glGetIntegerv(GL_MAX_SAMPLES, &max_samples));
    GLsizei num_samples = max_samples / 2;

    GLuint render_fbo;
    glsafe(::glGenFramebuffers(1, &render_fbo));
    glsafe(::glBindFramebuffer(GL_FRAMEBUFFER, render_fbo));
    BOOST_LOG_TRIVIAL(info) << boost::format("render_calibration_thumbnail prepare: max_samples %1%, multisample %2%, render_fbo %3%")%max_samples %multisample %render_fbo;

    GLuint render_tex = 0;
    GLuint render_tex_buffer = 0;
    if (multisample) {
        // use renderbuffer instead of texture to avoid the need to use glTexImage2DMultisample which is available only since OpenGL 3.2
        glsafe(::glGenRenderbuffers(1, &render_tex_buffer));
        glsafe(::glBindRenderbuffer(GL_RENDERBUFFER, render_tex_buffer));
        glsafe(::glRenderbufferStorageMultisample(GL_RENDERBUFFER, num_samples, GL_RGBA8, w, h));
        glsafe(::glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, render_tex_buffer));
    }
    else {
        glsafe(::glGenTextures(1, &render_tex));
        glsafe(::glBindTexture(GL_TEXTURE_2D, render_tex));
        glsafe(::glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr));
        glsafe(::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR));
        glsafe(::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR));
        glsafe(::glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, render_tex, 0));
    }

    GLuint render_depth;
    glsafe(::glGenRenderbuffers(1, &render_depth));
    glsafe(::glBindRenderbuffer(GL_RENDERBUFFER, render_depth));
    if (multisample)
        glsafe(::glRenderbufferStorageMultisample(GL_RENDERBUFFER, num_samples, GL_DEPTH_COMPONENT24, w, h));
    else
        glsafe(::glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT, w, h));

    glsafe(::glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, render_depth));

    GLenum drawBufs[] = { GL_COLOR_ATTACHMENT0 };
    glsafe(::glDrawBuffers(1, drawBufs));


    if (::glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE) {
        _render_calibration_thumbnail_internal(thumbnail_data, thumbnail_params, partplate_list, opengl_manager);

        if (multisample) {
            GLuint resolve_fbo;
            glsafe(::glGenFramebuffers(1, &resolve_fbo));
            glsafe(::glBindFramebuffer(GL_FRAMEBUFFER, resolve_fbo));

            GLuint resolve_tex;
            glsafe(::glGenTextures(1, &resolve_tex));
            glsafe(::glBindTexture(GL_TEXTURE_2D, resolve_tex));
            glsafe(::glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr));
            glsafe(::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR));
            glsafe(::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR));
            glsafe(::glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, resolve_tex, 0));

            glsafe(::glDrawBuffers(1, drawBufs));

            if (::glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE) {
                glsafe(::glBindFramebuffer(GL_READ_FRAMEBUFFER, render_fbo));
                glsafe(::glBindFramebuffer(GL_DRAW_FRAMEBUFFER, resolve_fbo));
                glsafe(::glBlitFramebuffer(0, 0, w, h, 0, 0, w, h, GL_COLOR_BUFFER_BIT, GL_LINEAR));

                glsafe(::glBindFramebuffer(GL_READ_FRAMEBUFFER, resolve_fbo));
                glsafe(::glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, (void*)thumbnail_data.pixels.data()));
            }

            glsafe(::glDeleteTextures(1, &resolve_tex));
            glsafe(::glDeleteFramebuffers(1, &resolve_fbo));
        }
        else
            glsafe(::glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, (void*)thumbnail_data.pixels.data()));
    }
#if ENABLE_CALIBRATION_THUMBNAIL_OUTPUT
     debug_calibration_output_thumbnail(thumbnail_data);
#endif

     glsafe(::glBindFramebuffer(GL_FRAMEBUFFER, 0));
     glsafe(::glDeleteRenderbuffers(1, &render_depth));
     if (render_tex_buffer != 0)
         glsafe(::glDeleteRenderbuffers(1, &render_tex_buffer));
     if (render_tex != 0)
         glsafe(::glDeleteTextures(1, &render_tex));
     glsafe(::glDeleteFramebuffers(1, &render_fbo));

    //if (!multisample)
    //    glsafe(::glDisable(GL_MULTISAMPLE));
    BOOST_LOG_TRIVIAL(info) << boost::format("render_calibration_thumbnail prepare: exit");
}

//BBS
void GCodeViewer::render_calibration_thumbnail(ThumbnailData& thumbnail_data, unsigned int w, unsigned int h, const ThumbnailsParams& thumbnail_params, PartPlateList& partplate_list, OpenGLManager& opengl_manager)
{
    // reset values and refresh render
    int       last_view_type_sel = m_view_type_sel;
    EViewType last_view_type     = m_view_type;
    unsigned int last_role_visibility_flags = m_extrusions.role_visibility_flags;
    // set color scheme to FilamentId
    for (int i = 0; i < view_type_items.size(); i++) {
        if (view_type_items[i] == EViewType::FilamentId) {
            m_view_type_sel = i;
            break;
        }
    }
    set_view_type(EViewType::FilamentId, false);
    // set m_layers_z_range to 0, 1;
    // To be safe, we include both layers here although layer 1 seems enough
    // layer 0: custom extrusions such as flow calibration etc.
    // layer 1: the real first layer of object
    std::array<unsigned int, 2> tmp_layers_z_range = m_layers_z_range;
    m_layers_z_range = {0, 1};
    // BBS exclude feature types
    m_extrusions.role_visibility_flags = m_extrusions.role_visibility_flags & ~(1 << erSkirt);
    m_extrusions.role_visibility_flags = m_extrusions.role_visibility_flags & ~(1 << erCustom);
    // BBS include feature types
    m_extrusions.role_visibility_flags = m_extrusions.role_visibility_flags | (1 << erWipeTower);
    m_extrusions.role_visibility_flags = m_extrusions.role_visibility_flags | (1 << erPerimeter);
    m_extrusions.role_visibility_flags = m_extrusions.role_visibility_flags | (1 << erExternalPerimeter);
    m_extrusions.role_visibility_flags = m_extrusions.role_visibility_flags | (1 << erOverhangPerimeter);
    m_extrusions.role_visibility_flags = m_extrusions.role_visibility_flags | (1 << erSolidInfill);
    m_extrusions.role_visibility_flags = m_extrusions.role_visibility_flags | (1 << erTopSolidInfill);
    m_extrusions.role_visibility_flags = m_extrusions.role_visibility_flags | (1 << erInternalInfill);
    m_extrusions.role_visibility_flags = m_extrusions.role_visibility_flags | (1 << erBottomSurface);

    refresh_render_paths(false, false);

    _render_calibration_thumbnail_framebuffer(thumbnail_data, w, h, thumbnail_params, partplate_list, opengl_manager);

    // restore values and refresh render
    // reset m_layers_z_range and view type
    m_view_type_sel = last_view_type_sel;
    set_view_type(last_view_type, false);
    m_layers_z_range = tmp_layers_z_range;
    m_extrusions.role_visibility_flags = last_role_visibility_flags;
    refresh_render_paths(false, false);
}

bool GCodeViewer::can_export_toolpaths() const
{
    return has_data() && m_buffers[buffer_id(EMoveType::Extrude)].render_primitive_type == TBuffer::ERenderPrimitiveType::Triangle;
}

void GCodeViewer::update_sequential_view_current(unsigned int first, unsigned int last)
{
    auto is_visible = [this](unsigned int id) {
        for (const TBuffer &buffer : m_buffers) {
            if (buffer.visible) {
                for (const Path &path : buffer.paths) {
                    if (path.sub_paths.front().first.s_id <= id && id <= path.sub_paths.back().last.s_id) return true;
                }
            }
        }
        return false;
    };

    const int first_diff = static_cast<int>(first) - static_cast<int>(m_sequential_view.last_current.first);
    const int last_diff  = static_cast<int>(last) - static_cast<int>(m_sequential_view.last_current.last);

    unsigned int new_first = first;
    unsigned int new_last  = last;

    if (m_sequential_view.skip_invisible_moves) {
        while (!is_visible(new_first)) {
            if (first_diff > 0)
                ++new_first;
            else
                --new_first;
        }

        while (!is_visible(new_last)) {
            if (last_diff > 0)
                ++new_last;
            else
                --new_last;
        }
    }

    m_sequential_view.current.first = new_first;
    m_sequential_view.current.last  = new_last;
    m_sequential_view.last_current  = m_sequential_view.current;

    refresh_render_paths(true, true);

    if (new_first != first || new_last != last) {
        update_moves_slider();
    }
}

Slic3r::GUI::IMSlider* GCodeViewer::get_moves_slider()
{
    return m_moves_slider;
}

Slic3r::GUI::IMSlider* GCodeViewer::get_layers_slider()
{
    return m_layers_slider;
}

Slic3r::GUI::IMSlider* GCodeViewer::get_cliper_slider()
{
    return m_cliper_slider;
}

void GCodeViewer::enable_moves_slider(bool enable) const
{
    bool render_as_disabled = !enable;
    if (m_moves_slider != nullptr && m_moves_slider->is_rendering_as_disabled() != render_as_disabled) {
        m_moves_slider->set_render_as_disabled(render_as_disabled);
        m_moves_slider->set_as_dirty();
    }
}

void GCodeViewer::update_moves_slider(bool set_to_max)
{
    const GCodeViewer::SequentialView &view = get_sequential_view();
    // this should not be needed, but it is here to try to prevent rambling crashes on Mac Asan
    if (view.endpoints.last < view.endpoints.first) return;

    std::vector<double> values(view.endpoints.last - view.endpoints.first + 1);
    std::vector<double> alternate_values(view.endpoints.last - view.endpoints.first + 1);
    unsigned int        count = 0;
    for (unsigned int i = view.endpoints.first; i <= view.endpoints.last; ++i) {
        values[count] = static_cast<double>(i + 1);
        if (view.gcode_ids[i] > 0) alternate_values[count] = static_cast<double>(view.gcode_ids[i]);
        ++count;
    }

    bool keep_min = m_moves_slider->GetActiveValue() == m_moves_slider->GetMinValue();

    m_moves_slider->SetSliderValues(values);
    m_moves_slider->SetSliderAlternateValues(alternate_values);
    m_moves_slider->SetMaxValue(view.endpoints.last - view.endpoints.first);
    m_moves_slider->SetSelectionSpan(view.current.first - view.endpoints.first, view.current.last - view.endpoints.first);
    if (set_to_max)
        m_moves_slider->SetHigherValue(keep_min ? m_moves_slider->GetMinValue() : m_moves_slider->GetMaxValue());
}

void GCodeViewer::update_layers_slider_mode()
{
    //    true  -> single-extruder printer profile OR
    //             multi-extruder printer profile , but whole model is printed by only one extruder
    //    false -> multi-extruder printer profile , and model is printed by several extruders
    bool one_extruder_printed_model = true;

    // extruder used for whole model for multi-extruder printer profile
    int only_extruder = -1;

    // BBS
    if (wxGetApp().filaments_cnt() > 1) {
        const ModelObjectPtrs &objects = wxGetApp().plater()->model().objects;

        // check if whole model uses just only one extruder
        if (!objects.empty()) {
            const int extruder = objects[0]->config.has("extruder") ? objects[0]->config.option("extruder")->getInt() : 0;

            auto is_one_extruder_printed_model = [objects, extruder]() {
                for (ModelObject *object : objects) {
                    if (object->config.has("extruder") && object->config.option("extruder")->getInt() != extruder) return false;

                    for (ModelVolume *volume : object->volumes)
                        if ((volume->config.has("extruder") && volume->config.option("extruder")->getInt() != extruder) || !volume->mmu_segmentation_facets.empty()) return false;

                    for (const auto &range : object->layer_config_ranges)
                        if (range.second.has("extruder") && range.second.option("extruder")->getInt() != extruder) return false;
                }
                return true;
            };

            if (is_one_extruder_printed_model())
                only_extruder = extruder;
            else
                one_extruder_printed_model = false;
        }
    }

    // TODO m_layers_slider->SetModeAndOnlyExtruder(one_extruder_printed_model, only_extruder);
}

void GCodeViewer::update_marker_curr_move() {
#if 0
    if ((int)m_last_result_id != -1) {
        auto it = std::find_if(m_gcode_result->moves.begin(), m_gcode_result->moves.end(), [this](auto move) {
                if (m_sequential_view.current.last < m_sequential_view.gcode_ids.size() && m_sequential_view.current.last >= 0) {
                    return move.gcode_id == static_cast<uint64_t>(m_sequential_view.gcode_ids[m_sequential_view.current.last]);
                }
                return false;
            });
        if (it != m_gcode_result->moves.end())
            marker.update_curr_move(*it);
    }
#else
    if ((int) m_last_result_id == -1 || m_sequential_view.current.last >= m_sid_to_moveid.size())
        return;

    // Direct O(1) lookup using pre-built s_id -> move_id mapping
    // This replaces the original O(N) linear search through all moves
    const size_t move_id = m_sid_to_moveid[m_sequential_view.current.last];
    if (move_id < m_gcode_result->moves.size()) 
        marker.update_curr_move(m_gcode_result->moves[move_id]);
#endif
}

bool GCodeViewer::is_toolpath_move_type_visible(EMoveType type) const
{
    size_t id = static_cast<size_t>(buffer_id(type));
    return (id < m_buffers.size()) ? m_buffers[id].visible : false;
}

void GCodeViewer::set_toolpath_move_type_visible(EMoveType type, bool visible)
{
    size_t id = static_cast<size_t>(buffer_id(type));
    if (id < m_buffers.size())
        m_buffers[id].visible = visible;
}

unsigned int GCodeViewer::get_options_visibility_flags() const
{
    auto set_flag = [](unsigned int flags, unsigned int flag, bool active) {
        return active ? (flags | (1 << flag)) : flags;
    };

    unsigned int flags = 0;
    flags = set_flag(flags, static_cast<unsigned int>(Preview::OptionType::Travel), is_toolpath_move_type_visible(EMoveType::Travel));
    flags = set_flag(flags, static_cast<unsigned int>(Preview::OptionType::Wipe), is_toolpath_move_type_visible(EMoveType::Wipe));
    flags = set_flag(flags, static_cast<unsigned int>(Preview::OptionType::Retractions), is_toolpath_move_type_visible(EMoveType::Retract));
    flags = set_flag(flags, static_cast<unsigned int>(Preview::OptionType::Unretractions), is_toolpath_move_type_visible(EMoveType::Unretract));
    flags = set_flag(flags, static_cast<unsigned int>(Preview::OptionType::Seams), is_toolpath_move_type_visible(EMoveType::Seam));
    flags = set_flag(flags, static_cast<unsigned int>(Preview::OptionType::ToolChanges), is_toolpath_move_type_visible(EMoveType::Tool_change));
    flags = set_flag(flags, static_cast<unsigned int>(Preview::OptionType::ColorChanges), is_toolpath_move_type_visible(EMoveType::Color_change));
    flags = set_flag(flags, static_cast<unsigned int>(Preview::OptionType::PausePrints), is_toolpath_move_type_visible(EMoveType::Pause_Print));
    flags = set_flag(flags, static_cast<unsigned int>(Preview::OptionType::CustomGCodes), is_toolpath_move_type_visible(EMoveType::Custom_GCode));
    flags = set_flag(flags, static_cast<unsigned int>(Preview::OptionType::Shells), m_shells.visible);
    flags = set_flag(flags, static_cast<unsigned int>(Preview::OptionType::ToolMarker), marker.is_visible());
    flags = set_flag(flags, static_cast<unsigned int>(Preview::OptionType::Legend), is_legend_enabled());
    return flags;
}

void GCodeViewer::set_options_visibility_from_flags(unsigned int flags)
{
    auto is_flag_set = [flags](unsigned int flag) {
        return (flags & (1 << flag)) != 0;
    };

    set_toolpath_move_type_visible(EMoveType::Travel, is_flag_set(static_cast<unsigned int>(Preview::OptionType::Travel)));
    set_toolpath_move_type_visible(EMoveType::Wipe, is_flag_set(static_cast<unsigned int>(Preview::OptionType::Wipe)));
    set_toolpath_move_type_visible(EMoveType::Retract, is_flag_set(static_cast<unsigned int>(Preview::OptionType::Retractions)));
    set_toolpath_move_type_visible(EMoveType::Unretract, is_flag_set(static_cast<unsigned int>(Preview::OptionType::Unretractions)));
    set_toolpath_move_type_visible(EMoveType::Seam, is_flag_set(static_cast<unsigned int>(Preview::OptionType::Seams)));
    set_toolpath_move_type_visible(EMoveType::Tool_change, is_flag_set(static_cast<unsigned int>(Preview::OptionType::ToolChanges)));
    set_toolpath_move_type_visible(EMoveType::Color_change, is_flag_set(static_cast<unsigned int>(Preview::OptionType::ColorChanges)));
    set_toolpath_move_type_visible(EMoveType::Pause_Print, is_flag_set(static_cast<unsigned int>(Preview::OptionType::PausePrints)));
    set_toolpath_move_type_visible(EMoveType::Custom_GCode, is_flag_set(static_cast<unsigned int>(Preview::OptionType::CustomGCodes)));
    m_shells.visible = is_flag_set(static_cast<unsigned int>(Preview::OptionType::Shells));
    marker.set_visible(is_flag_set(static_cast<unsigned int>(Preview::OptionType::ToolMarker)));
    enable_legend(is_flag_set(static_cast<unsigned int>(Preview::OptionType::Legend)));
}

void GCodeViewer::set_layers_z_range(const std::array<unsigned int, 2>& layers_z_range)
{
    bool keep_sequential_current_first = layers_z_range[0] >= m_layers_z_range[0];
    bool keep_sequential_current_last = layers_z_range[1] <= m_layers_z_range[1];
    m_layers_z_range = layers_z_range;
    refresh_render_paths(keep_sequential_current_first, keep_sequential_current_last);
    update_moves_slider(true);
}

void GCodeViewer::export_toolpaths_to_obj(const char* filename) const
{
#if 0
    if (filename == nullptr)
        return;

    if (!has_data())
        return;

    wxBusyCursor busy;

    // the data needed is contained into the Extrude TBuffer
    const TBuffer& t_buffer = m_buffers[buffer_id(EMoveType::Extrude)];
    if (!t_buffer.has_data())
        return;

    if (t_buffer.render_primitive_type != TBuffer::ERenderPrimitiveType::Triangle)
        return;

    // collect color information to generate materials
    std::vector<ColorRGBA> colors;
    for (const RenderPath& path : t_buffer.render_paths) {
        colors.push_back(path.color);
    }
    sort_remove_duplicates(colors);

    // save materials file
    boost::filesystem::path mat_filename(filename);
    mat_filename.replace_extension("mtl");

    CNumericLocalesSetter locales_setter;

    FILE* fp = boost::nowide::fopen(mat_filename.string().c_str(), "w");
    if (fp == nullptr) {
        BOOST_LOG_TRIVIAL(error) << "GCodeViewer::export_toolpaths_to_obj: Couldn't open " << mat_filename.string().c_str() << " for writing";
        return;
    }

    fprintf(fp, "# G-Code Toolpaths Materials\n");
    fprintf(fp, "# Generated by %s-%s based on Slic3r\n", SLIC3R_APP_NAME, SANITYPRINT_VERSION);

    unsigned int colors_count = 1;
    for (const ColorRGBA& color : colors) {
        fprintf(fp, "\nnewmtl material_%d\n", colors_count++);
        fprintf(fp, "Ka 1 1 1\n");
        fprintf(fp, "Kd %g %g %g\n", color.r(), color.g(), color.b());
        fprintf(fp, "Ks 0 0 0\n");
    }

    fclose(fp);

    // save geometry file
    fp = boost::nowide::fopen(filename, "w");
    if (fp == nullptr) {
        BOOST_LOG_TRIVIAL(error) << "GCodeViewer::export_toolpaths_to_obj: Couldn't open " << filename << " for writing";
        return;
    }

    fprintf(fp, "# G-Code Toolpaths\n");
    fprintf(fp, "# Generated by %s-%s based on Slic3r\n", SLIC3R_APP_NAME, SANITYPRINT_VERSION);
    fprintf(fp, "\nmtllib ./%s\n", mat_filename.filename().string().c_str());

    const size_t floats_per_vertex = t_buffer.vertices.vertex_size_floats();

    std::vector<Vec3f> out_vertices;
    std::vector<Vec3f> out_normals;

    struct VerticesOffset
    {
        unsigned int vbo;
        size_t offset;
    };
    std::vector<VerticesOffset> vertices_offsets;
    vertices_offsets.push_back({ t_buffer.vertices.vbos.front(), 0 });

    // get vertices/normals data from vertex buffers on gpu
    for (size_t i = 0; i < t_buffer.vertices.vbos.size(); ++i) {
        const size_t floats_count = t_buffer.vertices.sizes[i] / sizeof(float);
        VertexBuffer vertices(floats_count);
        glsafe(::glBindBuffer(GL_ARRAY_BUFFER, t_buffer.vertices.vbos[i]));
        glsafe(::glGetBufferSubData(GL_ARRAY_BUFFER, 0, static_cast<GLsizeiptr>(t_buffer.vertices.sizes[i]), static_cast<void*>(vertices.data())));
        glsafe(::glBindBuffer(GL_ARRAY_BUFFER, 0));
        const size_t vertices_count = floats_count / floats_per_vertex;
        for (size_t j = 0; j < vertices_count; ++j) {
            const size_t base = j * floats_per_vertex;
            out_vertices.push_back({ vertices[base + 0], vertices[base + 1], vertices[base + 2] });
            out_normals.push_back({ vertices[base + 3], vertices[base + 4], vertices[base + 5] });
        }

        if (i < t_buffer.vertices.vbos.size() - 1)
            vertices_offsets.push_back({ t_buffer.vertices.vbos[i + 1], vertices_offsets.back().offset + vertices_count });
    }

    // save vertices to file
    fprintf(fp, "\n# vertices\n");
    for (const Vec3f& v : out_vertices) {
        fprintf(fp, "v %g %g %g\n", v.x(), v.y(), v.z());
    }

    // save normals to file
    fprintf(fp, "\n# normals\n");
    for (const Vec3f& n : out_normals) {
        fprintf(fp, "vn %g %g %g\n", n.x(), n.y(), n.z());
    }

    size_t i = 0;
    for (const ColorRGBA& color : colors) {
        // save material triangles to file
        fprintf(fp, "\nusemtl material_%zu\n", i + 1);
        fprintf(fp, "# triangles material %zu\n", i + 1);

        for (const RenderPath& render_path : t_buffer.render_paths) {
            if (render_path.color != color)
                continue;

            const IBuffer& ibuffer = t_buffer.indices[render_path.ibuffer_id];
            size_t vertices_offset = 0;
            for (size_t j = 0; j < vertices_offsets.size(); ++j) {
                const VerticesOffset& offset = vertices_offsets[j];
                if (offset.vbo == ibuffer.vbo) {
                    vertices_offset = offset.offset;
                    break;
                }
            }

            // get indices data from index buffer on gpu
            glsafe(::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibuffer.ibo));
            for (size_t j = 0; j < render_path.sizes.size(); ++j) {
                IndexBuffer indices(render_path.sizes[j]);
                glsafe(::glGetBufferSubData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLintptr>(render_path.offsets[j]),
                    static_cast<GLsizeiptr>(render_path.sizes[j] * sizeof(IBufferType)), static_cast<void*>(indices.data())));

                const size_t triangles_count = render_path.sizes[j] / 3;
                for (size_t k = 0; k < triangles_count; ++k) {
                    const size_t base = k * 3;
                    const size_t v1 = 1 + static_cast<size_t>(indices[base + 0]) + vertices_offset;
                    const size_t v2 = 1 + static_cast<size_t>(indices[base + 1]) + vertices_offset;
                    const size_t v3 = 1 + static_cast<size_t>(indices[base + 2]) + vertices_offset;
                    if (v1 != v2)
                        // do not export dummy triangles
                        fprintf(fp, "f %zu//%zu %zu//%zu %zu//%zu\n", v1, v1, v2, v2, v3, v3);
                }
            }
            glsafe(::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0));
        }
        ++i;
    }

    fclose(fp);
#endif
}

void GCodeViewer::load_toolpaths(const GCodeProcessorResult& gcode_result, const BuildVolume& build_volume, const std::vector<BoundingBoxf3>& exclude_bounding_box)
{
    MARKING_DIALOG("STARTING_#1");

    BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << " start memory info " << log_memory_info();
    system_memory_stats(__FUNCTION__);

	m_is_mem_optim = should_enable_memory_optimize(gcode_result);
    const bool enable_mem_compress = m_is_mem_optim;
    // max index buffer size, in bytes
    static const size_t IBUFFER_THRESHOLD_BYTES = 64 * 1024 * 1024;

    //BOOST_LOG_TRIVIAL(info) << __FUNCTION__<< boost::format(",build_volume center{%1%, %2%}, moves count %3%\n")%build_volume.bed_center().x() % build_volume.bed_center().y() %gcode_result.moves.size();
    auto log_memory_usage = [this](const std::string& label, const std::vector<MultiVertexBuffer>& vertices, const std::vector<MultiIndexBuffer>& indices) {
        int64_t vertices_size = 0;
        for (const MultiVertexBuffer& buffers : vertices) {
            for (const VertexBuffer& buffer : buffers) {
                vertices_size += SLIC3R_STDVEC_MEMSIZE(buffer, float);
            }
            //BOOST_LOG_TRIVIAL(info) << __FUNCTION__<< boost::format("vertices count %1%\n")%buffers.size();
        }
        int64_t indices_size = 0;
        for (const MultiIndexBuffer& buffers : indices) {
            for (const IndexBuffer& buffer : buffers) {
                indices_size += SLIC3R_STDVEC_MEMSIZE(buffer, IBufferType);
            }
            //BOOST_LOG_TRIVIAL(info) << __FUNCTION__<< boost::format("indices count %1%\n")%buffers.size();
        }
        log_memory_used(label, vertices_size + indices_size);
    };

    // format data into the buffers to be rendered as lines
    auto add_vertices_as_line = [this, enable_mem_compress](const GCodeProcessorResult::MoveVertex& prev, const GCodeProcessorResult::MoveVertex& curr, VertexBuffer& vertices) {
       
        auto add_vertex = [this, &vertices, enable_mem_compress](const Vec3f& position) {
            if (enable_mem_compress) {
                Vec3f en_position = encode_position(position);
                vertices.push_back(en_position.x());
                vertices.push_back(en_position.y());
                vertices.push_back(en_position.z());
            } else {
                
                vertices.push_back(position.x());
                vertices.push_back(position.y());
                vertices.push_back(position.z());
            }
        };

        // x component of the normal to the current segment (the normal is parallel to the XY plane)
        //BBS: Has modified a lot for this function to support arc move
        size_t loop_num = curr.is_arc_move_with_interpolation_points() ? curr.interpolation_points.size() : 0;
        for (size_t i = 0; i < loop_num + 1; i++) {
            const Vec3f &previous = (i == 0? prev.position : curr.interpolation_points[i-1]);
            const Vec3f &current = (i == loop_num? curr.position : curr.interpolation_points[i]);
            // add previous vertex
            add_vertex(previous);
            // add current vertex
            add_vertex(current);
        }
    };
    //BBS: modify a lot to support arc travel
    auto add_indices_as_line = [](const GCodeProcessorResult::MoveVertex& prev, const GCodeProcessorResult::MoveVertex& curr, TBuffer& buffer,
        size_t& vbuffer_size, unsigned int ibuffer_id, IndexBuffer& indices, size_t move_id) {

            if (buffer.paths.empty() || prev.type != curr.type || !buffer.paths.back().matches(curr)) {
                buffer.add_path(curr, ibuffer_id, indices.size(), move_id - 1);
                buffer.paths.back().sub_paths.front().first.position = prev.position;
            }

            Path& last_path = buffer.paths.back();
            size_t loop_num = curr.is_arc_move_with_interpolation_points() ? curr.interpolation_points.size() : 0;
            for (size_t i = 0; i < loop_num + 1; i++) {
                //BBS: add previous index
                indices.push_back(static_cast<IBufferType>(indices.size()));
                //BBS: add current index
                indices.push_back(static_cast<IBufferType>(indices.size()));
                vbuffer_size += buffer.max_vertices_per_segment();
            }
            last_path.sub_paths.back().last = { ibuffer_id, indices.size() - 1, move_id, curr.position };
    };

    // format data into the buffers to be rendered as solid.
    auto add_vertices_as_solid = [this, enable_mem_compress](const GCodeProcessorResult::MoveVertex& prev, const GCodeProcessorResult::MoveVertex& curr, TBuffer& buffer, unsigned int vbuffer_id, VertexBuffer& vertices, size_t move_id) {
        auto store_vertex = [this, enable_mem_compress](VertexBuffer& vertices, const Vec3f& position, const Vec3f& normal) {
			if (enable_mem_compress) {
                // append position
                Vec3f en_position = encode_position(position);
                vertices.push_back(en_position.x());
                vertices.push_back(en_position.y());
                vertices.push_back(en_position.z());
                // append normal
                vertices.push_back(normal.x() * SHORT_TYPE_NORMAL_SCALE);
                vertices.push_back(normal.y() * SHORT_TYPE_NORMAL_SCALE);
                vertices.push_back(normal.z() * SHORT_TYPE_NORMAL_SCALE);

			} else {
                // append position
                vertices.push_back(position.x());
                vertices.push_back(position.y());
                vertices.push_back(position.z());
                // append normal
                vertices.push_back(normal.x());
                vertices.push_back(normal.y());
                vertices.push_back(normal.z());

			}
            
        };

        if (buffer.paths.empty() || prev.type != curr.type || !buffer.paths.back().matches(curr)) {
            buffer.add_path(curr, vbuffer_id, vertices.size(), move_id - 1);
            buffer.paths.back().sub_paths.back().first.position = prev.position;
        }

        Path& last_path = buffer.paths.back();
        //BBS: Has modified a lot for this function to support arc move
        size_t loop_num = curr.is_arc_move_with_interpolation_points() ? curr.interpolation_points.size() : 0;
        for (size_t i = 0; i < loop_num + 1; i++) {
            const Vec3f &prev_position = (i == 0? prev.position : curr.interpolation_points[i-1]);
            const Vec3f &curr_position = (i == loop_num? curr.position : curr.interpolation_points[i]);

            Vec3f dir = (curr_position - prev_position).normalized();
            // In some cases (such as when the z-offset is set to -0.1 and previewing model slices with surface boundaries), 
            // the z-value of interpolated coordinates may be abnormal, and this situation should be corrected.
            if (curr.position.z() == prev.position.z())
                dir[2] = 0.0f;

            const Vec3f right = Vec3f(dir.y(), -dir.x(), 0.0f).normalized();
            const Vec3f left = -right;
            const Vec3f up = right.cross(dir).normalized();
            const Vec3f down = -up;
            const float half_width = 0.5f * last_path.width;
            //const float half_height = 0.5f * last_path.height;
            const float half_height = (enable_mem_compress && 0.5f * last_path.height < SHORT_TYPE_POSITION_SCALE_INVERSE) ?
                                          SHORT_TYPE_POSITION_SCALE_INVERSE * 1.50:
                                          0.5f * last_path.height;
            /*const Vec3f prev_pos = prev_position - half_height * up;
            const Vec3f curr_pos = curr_position - half_height * up;*/
            Vec3f prev_pos = prev_position - half_height * up;
			Vec3f curr_pos = curr_position - half_height * up;
            if (enable_mem_compress && curr.extrusion_role == ExtrusionRole::erIroning) {
                prev_pos += Vec3f(0.0f, 0.0f, SHORT_TYPE_POSITION_SCALE_INVERSE * 1.50);
                curr_pos += Vec3f(0.0f, 0.0f, SHORT_TYPE_POSITION_SCALE_INVERSE * 1.50);
            }
            const Vec3f d_up = half_height * up;
            const Vec3f d_down = -half_height * up;
            const Vec3f d_right = half_width * right;
            const Vec3f d_left = -half_width * right;

            if ((last_path.vertices_count() == 1 || vertices.empty()) && i == 0) {
                store_vertex(vertices, prev_pos + d_up, up);
                store_vertex(vertices, prev_pos + d_right, right);
                store_vertex(vertices, prev_pos + d_down, down);
                store_vertex(vertices, prev_pos + d_left, left);
            } else {
                store_vertex(vertices, prev_pos + d_right, right);
                store_vertex(vertices, prev_pos + d_left, left);
            }

            store_vertex(vertices, curr_pos + d_up, up);
            store_vertex(vertices, curr_pos + d_right, right);
            store_vertex(vertices, curr_pos + d_down, down);
            store_vertex(vertices, curr_pos + d_left, left);
        }

        last_path.sub_paths.back().last = { vbuffer_id, vertices.size(), move_id, curr.position };
    };
    auto add_indices_as_solid = [&](const GCodeProcessorResult::MoveVertex& prev, const GCodeProcessorResult::MoveVertex& curr, const GCodeProcessorResult::MoveVertex* next,
        TBuffer& buffer, size_t& vbuffer_size, unsigned int ibuffer_id, IndexBuffer& indices, size_t move_id) {
            static Vec3f prev_dir;
            static Vec3f prev_up;
            static float sq_prev_length;
            auto store_triangle = [](IndexBuffer& indices, IBufferType i1, IBufferType i2, IBufferType i3) {
                indices.push_back(i1);
                indices.push_back(i2);
                indices.push_back(i3);
            };
            auto append_dummy_cap = [store_triangle](IndexBuffer& indices, IBufferType id) {
                store_triangle(indices, id, id, id);
                store_triangle(indices, id, id, id);
            };
            auto convert_vertices_offset = [](size_t vbuffer_size, const std::array<int, 8>& v_offsets) {
                std::array<IBufferType, 8> ret = {
                    static_cast<IBufferType>(static_cast<int>(vbuffer_size) + v_offsets[0]),
                    static_cast<IBufferType>(static_cast<int>(vbuffer_size) + v_offsets[1]),
                    static_cast<IBufferType>(static_cast<int>(vbuffer_size) + v_offsets[2]),
                    static_cast<IBufferType>(static_cast<int>(vbuffer_size) + v_offsets[3]),
                    static_cast<IBufferType>(static_cast<int>(vbuffer_size) + v_offsets[4]),
                    static_cast<IBufferType>(static_cast<int>(vbuffer_size) + v_offsets[5]),
                    static_cast<IBufferType>(static_cast<int>(vbuffer_size) + v_offsets[6]),
                    static_cast<IBufferType>(static_cast<int>(vbuffer_size) + v_offsets[7])
                };
                return ret;
            };
            auto append_starting_cap_triangles = [&](IndexBuffer& indices, const std::array<IBufferType, 8>& v_offsets) {
                store_triangle(indices, v_offsets[0], v_offsets[2], v_offsets[1]);
                store_triangle(indices, v_offsets[0], v_offsets[3], v_offsets[2]);
            };
            auto append_stem_triangles = [&](IndexBuffer& indices, const std::array<IBufferType, 8>& v_offsets) {
                store_triangle(indices, v_offsets[0], v_offsets[1], v_offsets[4]);
                store_triangle(indices, v_offsets[1], v_offsets[5], v_offsets[4]);
                store_triangle(indices, v_offsets[1], v_offsets[2], v_offsets[5]);
                store_triangle(indices, v_offsets[2], v_offsets[6], v_offsets[5]);
                store_triangle(indices, v_offsets[2], v_offsets[3], v_offsets[6]);
                store_triangle(indices, v_offsets[3], v_offsets[7], v_offsets[6]);
                store_triangle(indices, v_offsets[3], v_offsets[0], v_offsets[7]);
                store_triangle(indices, v_offsets[0], v_offsets[4], v_offsets[7]);
            };
            auto append_ending_cap_triangles = [&](IndexBuffer& indices, const std::array<IBufferType, 8>& v_offsets) {
                store_triangle(indices, v_offsets[4], v_offsets[6], v_offsets[7]);
                store_triangle(indices, v_offsets[4], v_offsets[5], v_offsets[6]);
            };

            if (buffer.paths.empty() || prev.type != curr.type || !buffer.paths.back().matches(curr)) {
                buffer.add_path(curr, ibuffer_id, indices.size(), move_id - 1);
                buffer.paths.back().sub_paths.back().first.position = prev.position;
            }

            Path& last_path = buffer.paths.back();
            bool is_first_segment = (last_path.vertices_count() == 1);
            //BBS: has modified a lot for this function to support arc move
            std::array<IBufferType, 8> first_seg_v_offsets = convert_vertices_offset(vbuffer_size, { 0, 1, 2, 3, 4, 5, 6, 7 });
            std::array<IBufferType, 8> non_first_seg_v_offsets = convert_vertices_offset(vbuffer_size, { -4, 0, -2, 1, 2, 3, 4, 5 });

            size_t loop_num = curr.is_arc_move_with_interpolation_points() ? curr.interpolation_points.size() : 0;
            for (size_t i = 0; i < loop_num + 1; i++) {
                const Vec3f &prev_position = (i == 0? prev.position : curr.interpolation_points[i-1]);
                const Vec3f &curr_position = (i == loop_num? curr.position : curr.interpolation_points[i]);

                const Vec3f dir = (curr_position - prev_position).normalized();
                const Vec3f right = Vec3f(dir.y(), -dir.x(), 0.0f).normalized();
                const Vec3f up = right.cross(dir);
                const float sq_length = (curr_position - prev_position).squaredNorm();

                if ((is_first_segment || vbuffer_size == 0) && i == 0) {
                    if (is_first_segment && i == 0)
                        // starting cap triangles
                        append_starting_cap_triangles(indices, first_seg_v_offsets);
                    // dummy triangles outer corner cap
                    append_dummy_cap(indices, vbuffer_size);
                    // stem triangles
                    append_stem_triangles(indices, first_seg_v_offsets);

                    vbuffer_size += 8;
                } else {
                    float displacement = 0.0f;
                    float cos_dir = prev_dir.dot(dir);
                    if (cos_dir > -0.9998477f) {
                        // if the angle between adjacent segments is smaller than 179 degrees
                        const Vec3f med_dir = (prev_dir + dir).normalized();
                        const float half_width = 0.5f * last_path.width;
                        displacement = half_width * ::tan(::acos(std::clamp(dir.dot(med_dir), -1.0f, 1.0f)));
                    }

                    float sq_displacement = sqr(displacement);
                    bool can_displace = displacement > 0.0f && sq_displacement < sq_prev_length&& sq_displacement < sq_length;

                    bool is_right_turn = prev_up.dot(prev_dir.cross(dir)) <= 0.0f;
                    // whether the angle between adjacent segments is greater than 45 degrees
                    bool is_sharp = cos_dir < 0.7071068f;

                    bool right_displaced = false;
                    bool left_displaced = false;

                    if (!is_sharp && can_displace) {
                        if (is_right_turn)
                            left_displaced = true;
                        else
                            right_displaced = true;
                    }

                    // triangles outer corner cap
                    if (is_right_turn) {
                        if (left_displaced)
                            // dummy triangles
                            append_dummy_cap(indices, vbuffer_size);
                        else {
                            store_triangle(indices, vbuffer_size - 4, vbuffer_size + 1, vbuffer_size - 1);
                            store_triangle(indices, vbuffer_size + 1, vbuffer_size - 2, vbuffer_size - 1);
                        }
                    }
                    else {
                        if (right_displaced)
                            // dummy triangles
                            append_dummy_cap(indices, vbuffer_size);
                        else {
                            store_triangle(indices, vbuffer_size - 4, vbuffer_size - 3, vbuffer_size + 0);
                            store_triangle(indices, vbuffer_size - 3, vbuffer_size - 2, vbuffer_size + 0);
                        }
                    }
                    // stem triangles
                    non_first_seg_v_offsets = convert_vertices_offset(vbuffer_size, { -4, 0, -2, 1, 2, 3, 4, 5 });
                    append_stem_triangles(indices, non_first_seg_v_offsets);
                    vbuffer_size += 6;
                }
                prev_dir = dir;
                prev_up = up;
                sq_prev_length = sq_length;
            }

            if (next != nullptr && (curr.type != next->type || !last_path.matches(*next)))
                // ending cap triangles
                append_ending_cap_triangles(indices, (is_first_segment && !curr.is_arc_move_with_interpolation_points()) ? first_seg_v_offsets : non_first_seg_v_offsets);

            last_path.sub_paths.back().last = { ibuffer_id, indices.size() - 1, move_id, curr.position };
    };

    // format data into the buffers to be rendered as instanced model
    auto add_model_instance = [](const GCodeProcessorResult::MoveVertex& curr, InstanceBuffer& instances, InstanceIdBuffer& instances_ids, size_t move_id) {
        // append position
        instances.push_back(curr.position.x());
        instances.push_back(curr.position.y());
        instances.push_back(curr.position.z());
        // append width
        instances.push_back(curr.width);
        // append height
        instances.push_back(curr.height);

        // append id
        instances_ids.push_back(move_id);
    };

    // format data into the buffers to be rendered as batched model
    auto add_vertices_as_model_batch = [this, enable_mem_compress](const GCodeProcessorResult::MoveVertex& curr, const GLModel::Geometry& data, VertexBuffer& vertices, InstanceBuffer& instances, InstanceIdBuffer& instances_ids, size_t move_id) {
        const double width = static_cast<double>(1.5f * curr.width);
        const double height = static_cast<double>(1.5f * curr.height);

        const Transform3d trafo = Geometry::assemble_transform((curr.position - 0.5f * curr.height * Vec3f::UnitZ()).cast<double>(), Vec3d::Zero(), { width, width, height });
        const Eigen::Matrix<double, 3, 3, Eigen::DontAlign> normal_matrix = trafo.matrix().template block<3, 3>(0, 0).inverse().transpose();

        // append vertices
        const size_t vertices_count = data.vertices_count();
		
		if (!enable_mem_compress) {

			for (size_t i = 0; i < vertices_count; ++i) {
				// append position
				const Vec3d position = trafo * data.extract_position_3(i).cast<double>();
				vertices.push_back(float(position.x()));
				vertices.push_back(float(position.y()));
				vertices.push_back(float(position.z()));

				// append normal
				const Vec3d normal = normal_matrix * data.extract_normal_3(i).cast<double>();
				vertices.push_back(float(normal.x()));
				vertices.push_back(float(normal.y()));
				vertices.push_back(float(normal.z()));
			}
        
		} else {
        
			for (size_t i = 0; i < vertices_count; ++i) {
                // append position
                const Vec3d position = trafo * data.extract_position_3(i).cast<double>();
                const Vec3f en_position = encode_position(position.cast<float>());
                vertices.push_back(en_position.x());
                vertices.push_back(en_position.y());
                vertices.push_back(en_position.z());

                // append normal
                const Vec3d normal = (normal_matrix * data.extract_normal_3(i).cast<double>()).normalized();
                vertices.push_back(normal.x() * SHORT_TYPE_NORMAL_SCALE);
                vertices.push_back(normal.y() * SHORT_TYPE_NORMAL_SCALE);
                vertices.push_back(normal.z() * SHORT_TYPE_NORMAL_SCALE);
            }
		}

        // append instance position
        instances.push_back(curr.position.x());
        instances.push_back(curr.position.y());
        instances.push_back(curr.position.z());
        // append instance id
        instances_ids.push_back(move_id);
    };

    auto add_indices_as_model_batch = [](const GLModel::Geometry& data, IndexBuffer& indices, IBufferType base_index) {
        const size_t indices_count = data.indices_count();
        for (size_t i = 0; i < indices_count; ++i) {
            indices.push_back(static_cast<IBufferType>(data.extract_index(i) + base_index));
        }
    };

#if ENABLE_GCODE_VIEWER_STATISTICS
    auto start_time = std::chrono::high_resolution_clock::now();
    m_statistics.results_size = SLIC3R_STDVEC_MEMSIZE(gcode_result.moves, GCodeProcessorResult::MoveVertex);
    m_statistics.results_time = gcode_result.time;
#endif // ENABLE_GCODE_VIEWER_STATISTICS

    m_moves_count = gcode_result.moves.size();
    if (m_moves_count == 0)
        return;

    m_extruders_count = gcode_result.extruders_count;

    // Import gcode file, preview using normal mode (no lite mode)
    bool is_lite_mode_cfg = (wxGetApp().app_config->get_bool("gcode_preview_lite_mode") && (!m_only_gcode_in_preview));

    // Auto enable lite mode on Linux if memory is tight or preview is huge.
#if defined(__linux__)
    {
        size_t avail_bytes = Slic3r::available_physical_memory();
        size_t total_bytes = Slic3r::total_physical_memory();
        const size_t moves_count_local = gcode_result.moves.size();
        const size_t avail_threshold_bytes = size_t(2) * size_t(1024) * size_t(1024) * size_t(1024); // 2 GB
        const bool low_available = (avail_bytes > 0 && avail_bytes < avail_threshold_bytes);
        const bool low_ratio     = (total_bytes > 0 && avail_bytes > 0 && (double)avail_bytes / (double)total_bytes < 0.125);
        const bool huge_preview  = (moves_count_local > 2000000);
        if (!m_only_gcode_in_preview && (low_available || low_ratio || huge_preview))
            is_lite_mode_cfg = true;
    }
#endif

    const bool is_lite_mode = m_is_lite_mode = is_lite_mode_cfg;

    unsigned int progress_count = 0;
    unsigned int progress_threshold = 1;
    if (m_moves_count >= 100) {
        progress_threshold = m_moves_count / 100;
    }
        
    //BBS: add only gcode mode
    //ProgressDialog *          progress_dialog    = m_only_gcode_in_preview ?
    //    new ProgressDialog(_L("Loading G-codes"), "...",
    //        100, wxGetApp().mainframe, wxPD_AUTO_HIDE | wxPD_APP_MODAL) : nullptr;

    // in only gcode mode, the UI update event would cause exception
    ProgressDialog *          progress_dialog = nullptr;

    wxBusyCursor busy;

    PartPlateList& partplate_list = wxGetApp().plater()->get_partplate_list();
    PartPlate* current_plate = partplate_list.get_curr_plate();

    m_contained_in_bed = (current_plate->get_slice_result()->toolpath_outside == false);
    m_paths_bounding_box = current_plate->get_gcode_path_bounding_box();

    // set approximate max bounding box (take in account also the tool marker)
    m_max_bounding_box = m_paths_bounding_box;
    m_max_bounding_box.merge(m_paths_bounding_box.max + marker.get_bounding_box().size().z() * Vec3d::UnitZ());

    // move the path bounding box calculation to partplate after slicing process complete
    /*
    //BBS: use convex_hull for toolpath outside check
    Points pts;

    // extract approximate paths bounding box from result
    //BBS: add only gcode mode
    for (const GCodeProcessorResult::MoveVertex& move : gcode_result.moves) {
        //if (wxGetApp().is_gcode_viewer()) {
        //if (m_only_gcode_in_preview) {
            // for the gcode viewer we need to take in account all moves to correctly size the printbed
        //    m_paths_bounding_box.merge(move.position.cast<double>());
        //}
        //else {
            if (move.type == EMoveType::Extrude && move.extrusion_role != erCustom && move.width != 0.0f && move.height != 0.0f) {
                m_paths_bounding_box.merge(move.position.cast<double>());
                //BBS: use convex_hull for toolpath outside check
                pts.emplace_back(Point(scale_(move.position.x()), scale_(move.position.y())));
            }
        //}
    }

    // BBS: also merge the point on arc to bounding box
    for (const GCodeProcessorResult::MoveVertex& move : gcode_result.moves) {
        // continue if not arc path
        if (!move.is_arc_move_with_interpolation_points())
            continue;

        //if (wxGetApp().is_gcode_viewer())
        //if (m_only_gcode_in_preview)
        //    for (int i = 0; i < move.interpolation_points.size(); i++)
        //        m_paths_bounding_box.merge(move.interpolation_points[i].cast<double>());
        //else {
            if (move.type == EMoveType::Extrude && move.width != 0.0f && move.height != 0.0f)
                for (int i = 0; i < move.interpolation_points.size(); i++) {
                    m_paths_bounding_box.merge(move.interpolation_points[i].cast<double>());
                    //BBS: use convex_hull for toolpath outside check
                    pts.emplace_back(Point(scale_(move.interpolation_points[i].x()), scale_(move.interpolation_points[i].y())));
                }
        //}
    }

    // set approximate max bounding box (take in account also the tool marker)
    m_max_bounding_box = m_paths_bounding_box;
    m_max_bounding_box.merge(m_paths_bounding_box.max + marker.get_bounding_box().size().z() * Vec3d::UnitZ());

    BOOST_LOG_TRIVIAL(info) << __FUNCTION__<< boost::format(",m_paths_bounding_box {%1%, %2%}-{%3%, %4%}\n")
        %m_paths_bounding_box.min.x() %m_paths_bounding_box.min.y() %m_paths_bounding_box.max.x() %m_paths_bounding_box.max.y();

    //if (wxGetApp().is_editor())
    {
        //BBS: use convex_hull for toolpath outside check
        m_contained_in_bed = build_volume.all_paths_inside(gcode_result, m_paths_bounding_box);
        if (m_contained_in_bed) {
            //PartPlateList& partplate_list = wxGetApp().plater()->get_partplate_list();
            //PartPlate* plate = partplate_list.get_curr_plate();
            //const std::vector<BoundingBoxf3>& exclude_bounding_box = plate->get_exclude_areas();
            if (exclude_bounding_box.size() > 0)
            {
                int index;
                Slic3r::Polygon convex_hull_2d = Slic3r::Geometry::convex_hull(std::move(pts));
                for (index = 0; index < exclude_bounding_box.size(); index ++)
                {
                    Slic3r::Polygon p = exclude_bounding_box[index].polygon(true);  // instance convex hull is scaled, so we need to scale here
                    if (intersection({ p }, { convex_hull_2d }).empty() == false)
                    {
                        m_contained_in_bed = false;
                        break;
                    }
                }
            }
        }
        (const_cast<GCodeProcessorResult&>(gcode_result)).toolpath_outside = !m_contained_in_bed;
    }
    */

    m_sequential_view.gcode_ids.clear();
    m_sid_to_moveid.clear();
    for (size_t i = 0; i < gcode_result.moves.size(); ++i) {
        const GCodeProcessorResult::MoveVertex& move = gcode_result.moves[i];
        if (move.type != EMoveType::Seam) {
            m_sequential_view.gcode_ids.push_back(move.gcode_id);
			m_sid_to_moveid.push_back(i);
        }
    }
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__<< boost::format(",m_contained_in_bed %1%\n")%m_contained_in_bed;

    std::vector<MultiVertexBuffer> vertices(m_buffers.size());
    std::vector<MultiIndexBuffer> indices(m_buffers.size());
    std::vector<InstanceBuffer> instances(m_buffers.size());
    std::vector<InstanceIdBuffer> instances_ids(m_buffers.size());
    std::vector<InstancesOffsets> instances_offsets(m_buffers.size());
    std::vector<float> options_zs;

    size_t seams_count = 0;
    std::vector<size_t> biased_seams_ids;

     BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << " load indices start";
    // toolpaths data -> extract vertices from result
    for (size_t i = 0; i < m_moves_count; ++i) {
        const GCodeProcessorResult::MoveVertex& curr = gcode_result.moves[i];
        if (curr.type == EMoveType::Seam) {
            ++seams_count;
            biased_seams_ids.push_back(i - biased_seams_ids.size() - 1);
        }

        size_t move_id = i - seams_count;

        // skip first vertex
        if (i == 0)
            continue;

        if (is_lite_mode && !(curr.type == EMoveType::Extrude || curr.type == EMoveType::Seam || curr.type == EMoveType::Unretract || curr.type == EMoveType::Pause_Print))
            continue;

        const GCodeProcessorResult::MoveVertex& prev = gcode_result.moves[i - 1];

        // update progress dialog
        ++progress_count;
        if (progress_dialog != nullptr && progress_count % progress_threshold == 0) {
            progress_dialog->Update(int(100.0f * float(i) / (2.0f * float(m_moves_count))),
                _L("Generating geometry vertex data") + ": " + wxNumberFormatter::ToString(100.0 * double(i) / double(m_moves_count), 0, wxNumberFormatter::Style_None) + "%");
            progress_dialog->Fit();
            progress_count = 0;
        }

        const unsigned char id = buffer_id(curr.type);
        TBuffer& t_buffer = m_buffers[id];
        MultiVertexBuffer& v_multibuffer = vertices[id];
        InstanceBuffer& inst_buffer = instances[id];
        InstanceIdBuffer& inst_id_buffer = instances_ids[id];
        InstancesOffsets& inst_offsets = instances_offsets[id];

        /*if (i%1000 == 1) {
            BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(":i=%1%, buffer_id %2% render_type %3%, gcode_id %4%\n")
                %i %(int)id %(int)t_buffer.render_primitive_type %curr.gcode_id;
        }*/

        // ensure there is at least one vertex buffer
        if (v_multibuffer.empty()) {
            BOOST_LOG_TRIVIAL(warning) << "v_multibuffer.empty(),  v_multibuffer.push_back(VertexBuffer())";
            v_multibuffer.push_back(VertexBuffer(enable_mem_compress));        
        }


        // if adding the vertices for the current segment exceeds the threshold size of the current vertex buffer
        // add another vertex buffer
        // BBS: get the point number and then judge whether the remaining buffer is enough
        size_t points_num = curr.is_arc_move_with_interpolation_points() ? curr.interpolation_points.size() + 1 : 1;
        size_t vertices_size_to_add = (t_buffer.render_primitive_type == TBuffer::ERenderPrimitiveType::BatchedModel) ? t_buffer.model.data.vertices_size_bytes() : points_num * t_buffer.max_vertices_per_segment_size_bytes();
        if (v_multibuffer.empty()) { // <--- 只在这里添加日志逻辑
            // --- 检测到致命错误：即将访问空 vector 的 back() ---
            std::ostringstream error_msg;
            // 获取尽可能多的相关上下文信息
            const GCodeProcessorResult::MoveVertex& prev = gcode_result.moves[i - 1]; // 确保 i > 0
            error_msg << "\n!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n"
                      << __FUNCTION__ << ": IMMINENT CRASH DETECTED!"<< ")\n"
                      << "Reason: v_multibuffer is unexpectedly EMPTY right before accessing back().size()!\n"
                      << "This contradicts earlier logic that should ensure it's not empty.\n"
                      << "Suspect memory corruption or subtle logic/optimization error.\n"
                      << "Allowing crash to proceed for reporting purposes after logging.\n"
                      << "---------------- Context Information -----------------\n"
                      << "Loop Index (i): " << i
                      << "\n"
                      // << "Move ID (move_id): " << move_id << "\n" // 如果 move_id 在此作用域可用
                      << "Buffer ID (id): " << (int) id << "\n"
                      << "Overall State: vertices.size()=" << vertices.size() << ", m_buffers.size()=" << m_buffers.size()
                      << "\n"
                      // << "Target TBuffer Info: render_primitive_type=" << static_cast<int>(t_buffer.render_primitive_type) << "\n" //
                      // 如果 t_buffer 可用
                      // << "Calculated vertices_size_to_add: " << vertices_size_to_add << "\n" // 如果 vertices_size_to_add 可用
                      << "--- Current Move (curr) ---\n"
                      << "  Type: " << static_cast<int>(curr.type) << " (" << buffer_id(curr.type) << ")\n"
                      << "  Position: (" << curr.position.x() << ", " << curr.position.y() << ", " << curr.position.z() << ")\n"
                      << "  Extruder ID: " << curr.extruder_id << ", G-code Line ID: " << curr.gcode_id << "\n"
                      << "--- Previous Move (prev) ---\n"
                      << "  Type: " << static_cast<int>(prev.type) << " (" << buffer_id(prev.type) << ")\n"
                      << "  Position: (" << prev.position.x() << ", " << prev.position.y() << ", " << prev.position.z() << ")\n"
                      << "!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n";

            // 1. 使用日志库记录错误 (FATAL 级别)
            BOOST_LOG_TRIVIAL(error) << error_msg.str();

            // 2. 强制刷新日志库缓冲区 (关键步骤)
            BOOST_LOG_TRIVIAL(warning) << "Attempting to flush logs before expected crash...";
            try {
                boost::log::core::get()->flush();
                BOOST_LOG_TRIVIAL(warning) << "Log flush initiated successfully.";
            } catch (const std::exception& e) {
                BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << ": FAILED TO INITIATE LOG FLUSH! Error: " << e.what();
            } catch (...) {
                BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << ": FAILED TO INITIATE LOG FLUSH! Unknown exception.";
            }

            // 3. 短暂延迟，给日志写入留出时间 (不阻塞后台日志线程)
            const int delay_ms = 1000;
            BOOST_LOG_TRIVIAL(info) << "Introducing " << delay_ms << "ms delay to aid log writing...";
            std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
            BOOST_LOG_TRIVIAL(info) << "Delay finished. Proceeding to expected crash point...";
           
            BOOST_LOG_TRIVIAL(warning) << "Attempting to flush logs before expected crash...";
            try {
                boost::log::core::get()->flush();
                BOOST_LOG_TRIVIAL(warning) << "Log flush initiated successfully.";
            } catch (const std::exception& e) {
                BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << ": FAILED TO INITIATE LOG FLUSH! Error: " << e.what();
            } catch (...) {
                BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << ": FAILED TO INITIATE LOG FLUSH! Unknown exception.";
            }

            // 4. 不做任何阻止，让代码自然执行到下一行导致崩溃
            BOOST_LOG_TRIVIAL(error) << ">>> Now executing the line expected to crash <<<";

        }   
        
        if (v_multibuffer.back().size() * sizeof(float) > t_buffer.vertices.max_size_bytes() - vertices_size_to_add) {
            v_multibuffer.push_back(VertexBuffer(enable_mem_compress));
            if (t_buffer.render_primitive_type == TBuffer::ERenderPrimitiveType::Triangle) {
                Path& last_path = t_buffer.paths.back();
                if (prev.type == curr.type && last_path.matches(curr))
                    last_path.add_sub_path(prev, static_cast<unsigned int>(v_multibuffer.size()) - 1, 0, move_id - 1);
            }
        }

        VertexBuffer& v_buffer = v_multibuffer.back();

        switch (t_buffer.render_primitive_type)
        {
        case TBuffer::ERenderPrimitiveType::Line:     { add_vertices_as_line(prev, curr, v_buffer); break; }
        case TBuffer::ERenderPrimitiveType::Triangle: { add_vertices_as_solid(prev, curr, t_buffer, static_cast<unsigned int>(v_multibuffer.size()) - 1, v_buffer, move_id); break; }
        case TBuffer::ERenderPrimitiveType::InstancedModel:
        {
            add_model_instance(curr, inst_buffer, inst_id_buffer, move_id);
            inst_offsets.push_back(prev.position - curr.position);
#if ENABLE_GCODE_VIEWER_STATISTICS
            ++m_statistics.instances_count;
#endif // ENABLE_GCODE_VIEWER_STATISTICS
            break;
        }
        case TBuffer::ERenderPrimitiveType::BatchedModel:
        {
            add_vertices_as_model_batch(curr, t_buffer.model.data, v_buffer, inst_buffer, inst_id_buffer, move_id);
            inst_offsets.push_back(prev.position - curr.position);
#if ENABLE_GCODE_VIEWER_STATISTICS
            ++m_statistics.batched_count;
#endif // ENABLE_GCODE_VIEWER_STATISTICS
            break;
        }
        }

        // collect options zs for later use
        if (curr.type == EMoveType::Pause_Print || curr.type == EMoveType::Custom_GCode) {
            const float* const last_z = options_zs.empty() ? nullptr : &options_zs.back();
            if (last_z == nullptr || curr.position[2] < *last_z - EPSILON || *last_z + EPSILON < curr.position[2])
                options_zs.emplace_back(curr.position[2]);
        }
    }

    system_memory_stats("Gcode parsing completed");

    BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << " load indices end";

    /*for (size_t b = 0; b < vertices.size(); ++b) {
        MultiVertexBuffer& v_multibuffer = vertices[b];
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(":b=%1%, vertex buffer count %2%\n")
            %b %v_multibuffer.size();
    }*/
    auto extract_move_id = [&biased_seams_ids](size_t id) {
        size_t new_id = size_t(-1);
        auto it = std::lower_bound(biased_seams_ids.begin(), biased_seams_ids.end(), id);
        if (it == biased_seams_ids.end())
            new_id = id + biased_seams_ids.size();
        else {
            if (it == biased_seams_ids.begin() && *it < id)
                new_id = id;
            else if (it != biased_seams_ids.begin())
                new_id = id + std::distance(biased_seams_ids.begin(), it);
        }
        return (new_id == size_t(-1)) ? id : new_id;
    };
    //BBS: generate map from ssid to move id in advance to reduce computation
    m_ssid_to_moveid_map.clear();
    m_ssid_to_moveid_map.reserve( m_moves_count - biased_seams_ids.size());
    for (size_t i = 0; i < m_moves_count - biased_seams_ids.size(); i++)
        m_ssid_to_moveid_map.push_back(extract_move_id(i));

    // Build arc-interpolation-points prefix-sum for O(1) range queries in refresh_render_paths.
    // m_ssid_arc_extra_segments[i] = cumulative extra arc segments for all s_ids in [0, i].
    {
        const size_t ssid_count = m_ssid_to_moveid_map.size();
        m_ssid_arc_extra_segments.resize(ssid_count, 0u);
        unsigned int running_sum = 0;
        for (size_t i = 0; i < ssid_count; ++i) {
            const size_t move_id = m_ssid_to_moveid_map[i];
            if (move_id < gcode_result.moves.size()) {
                const auto& mv = gcode_result.moves[move_id];
                if (mv.is_arc_move())
                    running_sum += static_cast<unsigned int>(mv.interpolation_points.size());
            }
            m_ssid_arc_extra_segments[i] = running_sum;
        }
    }

	// dismiss, no more needed
    std::vector<size_t>().swap(biased_seams_ids);

    const float position_scale = enable_mem_compress ? SHORT_TYPE_POSITION_SCALE : 1.0;

    //BBS: smooth toolpaths corners for the given TBuffer using triangles
    auto smooth_triangle_toolpaths_corners = [&gcode_result, this, position_scale](const TBuffer& t_buffer, MultiVertexBuffer& v_multibuffer) {
        auto extract_position_at = [](const VertexBuffer& vertices, size_t offset) {
            return Vec3f(vertices[offset + 0], vertices[offset + 1], vertices[offset + 2]);
        };
        auto update_position_at = [](VertexBuffer& vertices, size_t offset, const Vec3f& position) {
            /*vertices[offset + 0] = position.x();
            vertices[offset + 1] = position.y();
            vertices[offset + 2] = position.z();*/
            vertices.store_data_at_index(offset + 0, position.x());
            vertices.store_data_at_index(offset + 1, position.y());
            vertices.store_data_at_index(offset + 2, position.z());
        };
        auto match_right_vertices_with_internal_point = [&](const Path::Sub_Path& prev_sub_path, const Path::Sub_Path& next_sub_path,
            size_t curr_s_id, bool is_internal_point, size_t interpolation_point_id, size_t vertex_size_floats, const Vec3f& displacement_vec) {
            if (&prev_sub_path == &next_sub_path || is_internal_point) { // previous and next segment are both contained into to the same vertex buffer
                VertexBuffer& vbuffer = v_multibuffer[prev_sub_path.first.b_id];
                // offset into the vertex buffer of the next segment 1st vertex
                size_t temp_offset = prev_sub_path.last.s_id - curr_s_id;
                for (size_t i = prev_sub_path.last.s_id; i > curr_s_id; i--) {
                    size_t move_id = m_ssid_to_moveid_map[i];
                    temp_offset += (gcode_result.moves[move_id].is_arc_move() ? gcode_result.moves[move_id].interpolation_points.size() : 0);
                }
                if (is_internal_point) {
                    size_t move_id = m_ssid_to_moveid_map[curr_s_id];
                    temp_offset += (gcode_result.moves[move_id].interpolation_points.size() - interpolation_point_id);
                }
                const size_t next_1st_offset = temp_offset * 6 * vertex_size_floats;
                // offset into the vertex buffer of the right vertex of the previous segment
                const size_t prev_right_offset = prev_sub_path.last.i_id - next_1st_offset - 3 * vertex_size_floats;
                // new position of the right vertices
                const Vec3f shared_vertex = extract_position_at(vbuffer, prev_right_offset) + displacement_vec;
                // update previous segment
                update_position_at(vbuffer, prev_right_offset, shared_vertex);
                // offset into the vertex buffer of the right vertex of the next segment
                const size_t next_right_offset = prev_sub_path.last.i_id - next_1st_offset;
                // update next segment
                update_position_at(vbuffer, next_right_offset, shared_vertex);
            }
            else { // previous and next segment are contained into different vertex buffers
                VertexBuffer& prev_vbuffer = v_multibuffer[prev_sub_path.first.b_id];
                VertexBuffer& next_vbuffer = v_multibuffer[next_sub_path.first.b_id];
                // offset into the previous vertex buffer of the right vertex of the previous segment
                const size_t prev_right_offset = prev_sub_path.last.i_id - 3 * vertex_size_floats;
                // new position of the right vertices
                const Vec3f shared_vertex = extract_position_at(prev_vbuffer, prev_right_offset) + displacement_vec;
                // update previous segment
                update_position_at(prev_vbuffer, prev_right_offset, shared_vertex);
                // offset into the next vertex buffer of the right vertex of the next segment
                const size_t next_right_offset = next_sub_path.first.i_id + 1 * vertex_size_floats;
                // update next segment
                update_position_at(next_vbuffer, next_right_offset, shared_vertex);
            }
        };
        //BBS: modify a lot of this function to support arc move
        auto match_left_vertices_with_internal_point = [&](const Path::Sub_Path& prev_sub_path, const Path::Sub_Path& next_sub_path,
            size_t curr_s_id, bool is_internal_point, size_t interpolation_point_id, size_t vertex_size_floats, const Vec3f& displacement_vec) {
            if (&prev_sub_path == &next_sub_path || is_internal_point) { // previous and next segment are both contained into to the same vertex buffer
                VertexBuffer& vbuffer = v_multibuffer[prev_sub_path.first.b_id];
                // offset into the vertex buffer of the next segment 1st vertex
                size_t temp_offset = prev_sub_path.last.s_id - curr_s_id;
                for (size_t i = prev_sub_path.last.s_id; i > curr_s_id; i--) {
                    size_t move_id = m_ssid_to_moveid_map[i];
                    temp_offset += (gcode_result.moves[move_id].is_arc_move() ? gcode_result.moves[move_id].interpolation_points.size() : 0);
                }
                if (is_internal_point) {
                    size_t move_id = m_ssid_to_moveid_map[curr_s_id];
                    temp_offset += (gcode_result.moves[move_id].interpolation_points.size() - interpolation_point_id);
                }
                const size_t next_1st_offset = temp_offset * 6 * vertex_size_floats;
                // offset into the vertex buffer of the left vertex of the previous segment
                const size_t prev_left_offset = prev_sub_path.last.i_id - next_1st_offset - 1 * vertex_size_floats;
                // new position of the left vertices
                const Vec3f shared_vertex = extract_position_at(vbuffer, prev_left_offset) + displacement_vec;
                // update previous segment
                update_position_at(vbuffer, prev_left_offset, shared_vertex);
                // offset into the vertex buffer of the left vertex of the next segment
                const size_t next_left_offset = prev_sub_path.last.i_id - next_1st_offset + 1 * vertex_size_floats;
                // update next segment
                update_position_at(vbuffer, next_left_offset, shared_vertex);
            }
            else { // previous and next segment are contained into different vertex buffers
                VertexBuffer& prev_vbuffer = v_multibuffer[prev_sub_path.first.b_id];
                VertexBuffer& next_vbuffer = v_multibuffer[next_sub_path.first.b_id];
                // offset into the previous vertex buffer of the left vertex of the previous segment
                const size_t prev_left_offset = prev_sub_path.last.i_id - 1 * vertex_size_floats;
                // new position of the left vertices
                const Vec3f shared_vertex = extract_position_at(prev_vbuffer, prev_left_offset) + displacement_vec;
                // update previous segment
                update_position_at(prev_vbuffer, prev_left_offset, shared_vertex);
                // offset into the next vertex buffer of the left vertex of the next segment
                const size_t next_left_offset = next_sub_path.first.i_id + 3 * vertex_size_floats;
                // update next segment
                update_position_at(next_vbuffer, next_left_offset, shared_vertex);
            }
        };

        size_t vertex_size_floats = t_buffer.vertices.vertex_size_floats();
        for (const Path& path : t_buffer.paths) {
            //BBS: the two segments of the path sharing the current vertex may belong
            //to two different vertex buffers
            size_t prev_sub_path_id = 0;
            size_t next_sub_path_id = 0;
            const size_t path_vertices_count = path.vertices_count();
            const float half_width = 0.5f * path.width;
            // BBS: modify a lot to support arc move which has internal points
            for (size_t j = 1; j < path_vertices_count; ++j) {
                size_t curr_s_id = path.sub_paths.front().first.s_id + j;
                size_t move_id = m_ssid_to_moveid_map[curr_s_id];
                int interpolation_points_num = gcode_result.moves[move_id].is_arc_move_with_interpolation_points()?
                                                    gcode_result.moves[move_id].interpolation_points.size() : 0;
                int loop_num = interpolation_points_num;
                //BBS: select the subpaths which contains the previous/next segments
                if (!path.sub_paths[prev_sub_path_id].contains(curr_s_id))
                    ++prev_sub_path_id;
                if (j == path_vertices_count - 1) {
                    if (!gcode_result.moves[move_id].is_arc_move_with_interpolation_points())
                        break;   // BBS: the last move has no internal point.
                    loop_num--;  //BBS: don't need to handle the endpoint of the last arc move of path
                    next_sub_path_id = prev_sub_path_id;
                } else {
                    if (!path.sub_paths[next_sub_path_id].contains(curr_s_id + 1))
                        ++next_sub_path_id;
                }
                const Path::Sub_Path& prev_sub_path = path.sub_paths[prev_sub_path_id];
                const Path::Sub_Path& next_sub_path = path.sub_paths[next_sub_path_id];

                // BBS: smooth triangle toolpaths corners including arc move which has internal interpolation point
                for (int k = 0; k <= loop_num; k++) {
                    const Vec3f& prev = k==0?
                                        gcode_result.moves[move_id - 1].position :
                                        gcode_result.moves[move_id].interpolation_points[k-1];
                    const Vec3f& curr = k==interpolation_points_num?
                                        gcode_result.moves[move_id].position :
                                        gcode_result.moves[move_id].interpolation_points[k];
                    const Vec3f& next = k < interpolation_points_num - 1?
                                        gcode_result.moves[move_id].interpolation_points[k+1]:
                                        (k == interpolation_points_num - 1? gcode_result.moves[move_id].position :
                                        (gcode_result.moves[move_id + 1].is_arc_move_with_interpolation_points()?
                                        gcode_result.moves[move_id + 1].interpolation_points[0] :
                                        gcode_result.moves[move_id + 1].position));

                    const Vec3f prev_dir = (curr - prev).normalized();
                    const Vec3f prev_right = Vec3f(prev_dir.y(), -prev_dir.x(), 0.0f).normalized();
                    const Vec3f prev_up = prev_right.cross(prev_dir);

                    const Vec3f next_dir = (next - curr).normalized();

                    const bool is_right_turn = prev_up.dot(prev_dir.cross(next_dir)) <= 0.0f;
                    const float cos_dir = prev_dir.dot(next_dir);
                    // whether the angle between adjacent segments is greater than 45 degrees
                    const bool is_sharp = cos_dir < 0.7071068f;

                    float displacement = 0.0f;
                    if (cos_dir > -0.9998477f) {
                        // if the angle between adjacent segments is smaller than 179 degrees
                        Vec3f med_dir = (prev_dir + next_dir).normalized();
                        displacement = half_width * ::tan(::acos(std::clamp(next_dir.dot(med_dir), -1.0f, 1.0f)));
                    }

                    const float sq_prev_length = (curr - prev).squaredNorm();
                    const float sq_next_length = (next - curr).squaredNorm();
                    const float sq_displacement = sqr(displacement);
                    const bool can_displace = displacement > 0.0f && sq_displacement < sq_prev_length&& sq_displacement < sq_next_length;
                    bool is_internal_point = interpolation_points_num > k;

                    if (can_displace) {
                        // displacement to apply to the vertices to match
                        Vec3f displacement_vec = displacement * prev_dir * position_scale;
                        // matches inner corner vertices
                        if (is_right_turn)
                            match_right_vertices_with_internal_point(prev_sub_path, next_sub_path, curr_s_id, is_internal_point, k, vertex_size_floats, -displacement_vec);
                        else
                            match_left_vertices_with_internal_point(prev_sub_path, next_sub_path, curr_s_id, is_internal_point, k, vertex_size_floats, -displacement_vec);

                        if (!is_sharp) {
                            //BBS: matches outer corner vertices
                            if (is_right_turn)
                                match_left_vertices_with_internal_point(prev_sub_path, next_sub_path, curr_s_id, is_internal_point, k, vertex_size_floats, displacement_vec);
                            else
                                match_right_vertices_with_internal_point(prev_sub_path, next_sub_path, curr_s_id, is_internal_point, k, vertex_size_floats, displacement_vec);
                        }
                    }
                }
            }
        }
    };

#if ENABLE_GCODE_VIEWER_STATISTICS
    auto load_vertices_time = std::chrono::high_resolution_clock::now();
    m_statistics.load_vertices = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - start_time).count();
#endif // ENABLE_GCODE_VIEWER_STATISTICS

    // smooth toolpaths corners for TBuffers using triangles
    for (size_t i = 0; i < m_buffers.size(); ++i) {
        const TBuffer& t_buffer = m_buffers[i];
        if (t_buffer.render_primitive_type == TBuffer::ERenderPrimitiveType::Triangle) {
            smooth_triangle_toolpaths_corners(t_buffer, vertices[i]);
        }
    }
    system_memory_stats("smooth_triangle_toolpaths_corners end");

    for (MultiVertexBuffer& v_multibuffer : vertices) {
        for (VertexBuffer& v_buffer : v_multibuffer) {
            v_buffer.shrink_to_fit();
        }
    }

#define USE_PARALLEL 1

    // move the wipe toolpaths half height up to render them on proper position
    MultiVertexBuffer& wipe_vertices = vertices[buffer_id(EMoveType::Wipe)];
    for (VertexBuffer& v_buffer : wipe_vertices) {
#if USE_PARALLEL
        tbb::parallel_for(tbb::blocked_range<size_t>(0, v_buffer.size() / 3), [&v_buffer, position_scale](const tbb::blocked_range<size_t>& range) {
            for (size_t i = range.begin(); i != range.end(); ++i) {
                float v = v_buffer[i * 3 + 2] + 0.5f * GCodeProcessor::Wipe_Height * position_scale;
                v_buffer.store_data_at_index(i * 3 + 2, v);
            }
        });
#else
        for (size_t i = 2; i < v_buffer.size(); i += 3) {
            //v_buffer[i] += 0.5f * GCodeProcessor::Wipe_Height;
            float v = v_buffer[i * 3 + 2] + 0.5f * GCodeProcessor::Wipe_Height;
            v_buffer.store_data_at_index(i * 3 + 2, v);
        }
#endif // USE_PARALLEL
    }

    // send vertices data to gpu, where needed
    BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << " send vertices data to gpu, where needed start";

    for (size_t i = 0; i < m_buffers.size(); ++i) {
        
        if (is_lite_mode && !(i == buffer_id(EMoveType::Extrude) || i == buffer_id(EMoveType::Seam) || i == buffer_id(EMoveType::Unretract) || i == buffer_id(EMoveType::Pause_Print)))
            continue;
        
        TBuffer& t_buffer = m_buffers[i];
        if (t_buffer.render_primitive_type == TBuffer::ERenderPrimitiveType::InstancedModel) {
            const InstanceBuffer& inst_buffer = instances[i];
            if (!inst_buffer.empty()) {
                t_buffer.model.instances.buffer = inst_buffer;
                t_buffer.model.instances.s_ids = instances_ids[i];
                t_buffer.model.instances.offsets = instances_offsets[i];
            }
        }
        else {
            if (t_buffer.render_primitive_type == TBuffer::ERenderPrimitiveType::BatchedModel) {
                const InstanceBuffer& inst_buffer = instances[i];
                if (!inst_buffer.empty()) {
                    t_buffer.model.instances.buffer = inst_buffer;
                    t_buffer.model.instances.s_ids = instances_ids[i];
                    t_buffer.model.instances.offsets = instances_offsets[i];
                }
            }
            const MultiVertexBuffer& v_multibuffer = vertices[i];
            for (const VertexBuffer& v_buffer : v_multibuffer) {
                const size_t size_elements = v_buffer.size();
                const size_t size_bytes     = size_elements * v_buffer.size_of_element_type();
                const size_t vertices_count = size_elements / (t_buffer.vertices.vertex_size_floats() * v_buffer.size_of_element_type());
                t_buffer.vertices.count += vertices_count;

#if ENABLE_GCODE_VIEWER_STATISTICS
                m_statistics.total_vertices_gpu_size += static_cast<int64_t>(size_bytes);
                m_statistics.max_vbuffer_gpu_size = std::max(m_statistics.max_vbuffer_gpu_size, static_cast<int64_t>(size_bytes));
                ++m_statistics.vbuffers_count;
#endif // ENABLE_GCODE_VIEWER_STATISTICS

                GLuint id = 0;
                glsafe(::glGenBuffers(1, &id));
                glsafe(::glBindBuffer(GL_ARRAY_BUFFER, id));
                glsafe(::glBufferData(GL_ARRAY_BUFFER, size_bytes, v_buffer.data(), GL_STATIC_DRAW));
                glsafe(::glBindBuffer(GL_ARRAY_BUFFER, 0));

                t_buffer.vertices.vbos.push_back(static_cast<unsigned int>(id));
                t_buffer.vertices.sizes.push_back(size_bytes);
            }
        }
    }

    system_memory_stats("send vertices data to gpu end");

    BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << " send vertices data to gpu, where needed end";

    MARKING_DIALOG("Vertices_Peak_#2");

#if ENABLE_GCODE_VIEWER_STATISTICS
    auto smooth_vertices_time = std::chrono::high_resolution_clock::now();
    m_statistics.smooth_vertices = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - load_vertices_time).count();
#endif // ENABLE_GCODE_VIEWER_STATISTICS
    log_memory_usage("Loaded G-code generated vertex buffers ", vertices, indices);

    // dismiss vertices data, no more needed
    std::vector<MultiVertexBuffer>().swap(vertices);
    std::vector<InstanceBuffer>().swap(instances);
    std::vector<InstanceIdBuffer>().swap(instances_ids);
    std::vector<InstancesOffsets>().swap(instances_offsets);
    
    // toolpaths data -> extract indices from result
    // paths may have been filled while extracting vertices,
    // so reset them, they will be filled again while extracting indices
    for (TBuffer& buffer : m_buffers) {
        buffer.paths.clear();
    }

	MARKING_DIALOG("Vertices_Ending_#3");

    // variable used to keep track of the current vertex buffers index and size
    using CurrVertexBuffer = std::pair<unsigned int, size_t>;
    std::vector<CurrVertexBuffer> curr_vertex_buffers(m_buffers.size(), { 0, 0 });

    // variable used to keep track of the vertex buffers ids
    using VboIndexList = std::vector<unsigned int>;
    std::vector<VboIndexList> vbo_indices(m_buffers.size());


    seams_count = 0;

    for (size_t i = 0; i < m_moves_count; ++i) {
        const GCodeProcessorResult::MoveVertex& curr = gcode_result.moves[i];
        if (curr.type == EMoveType::Seam)
            ++seams_count;

        size_t move_id = i - seams_count;

        // skip first vertex
        if (i == 0)
            continue;

        if (is_lite_mode && !(curr.type == EMoveType::Extrude || curr.type == EMoveType::Seam || curr.type == EMoveType::Unretract || curr.type == EMoveType::Pause_Print))
            continue;

        const GCodeProcessorResult::MoveVertex& prev = gcode_result.moves[i - 1];
        const GCodeProcessorResult::MoveVertex* next = nullptr;
        if (i < m_moves_count - 1)
            next = &gcode_result.moves[i + 1];

        ++progress_count;
        if (progress_dialog != nullptr && progress_count % progress_threshold == 0) {
            progress_dialog->Update(int(100.0f * float(m_moves_count + i) / (2.0f * float(m_moves_count))),
                _L("Generating geometry index data") + ": " + wxNumberFormatter::ToString(100.0 * double(i) / double(m_moves_count), 0, wxNumberFormatter::Style_None) + "%");
            progress_dialog->Fit();
            progress_count = 0;
        }

        const unsigned char id = buffer_id(curr.type);
        TBuffer& t_buffer = m_buffers[id];
        MultiIndexBuffer& i_multibuffer = indices[id];
        CurrVertexBuffer& curr_vertex_buffer = curr_vertex_buffers[id];
        VboIndexList& vbo_index_list = vbo_indices[id];

        // ensure there is at least one index buffer
        if (i_multibuffer.empty()) {
#if ENABLE_VECTOR_HYBRID_BACKEND
            i_multibuffer.push_back(IndexBuffer(enable_mem_compress));
#else
            i_multibuffer.push_back(IndexBuffer());
#endif
            if (!t_buffer.vertices.vbos.empty())
                vbo_index_list.push_back(t_buffer.vertices.vbos[curr_vertex_buffer.first]);
        }

        // if adding the indices for the current segment exceeds the threshold size of the current index buffer
        // create another index buffer
        // BBS: get the point number and then judge whether the remaining buffer is enough
        size_t points_num = curr.is_arc_move_with_interpolation_points() ? curr.interpolation_points.size() + 1 : 1;
        size_t indiced_size_to_add = (t_buffer.render_primitive_type == TBuffer::ERenderPrimitiveType::BatchedModel) ? t_buffer.model.data.indices_size_bytes() : points_num * t_buffer.max_indices_per_segment_size_bytes();
        if (i_multibuffer.back().size() * sizeof(IBufferType) >= IBUFFER_THRESHOLD_BYTES - indiced_size_to_add) {

#if ENABLE_VECTOR_HYBRID_BACKEND

            if (!i_multibuffer.empty()) {
                i_multibuffer.back().flush_memory_and_close();
            }
            i_multibuffer.push_back(IndexBuffer(enable_mem_compress));
#else
            i_multibuffer.push_back(IndexBuffer());
#endif
            vbo_index_list.push_back(t_buffer.vertices.vbos[curr_vertex_buffer.first]);
            //if (t_buffer.render_primitive_type != TBuffer::ERenderPrimitiveType::BatchedModel) {
            //    Path& last_path = t_buffer.paths.back();
            //    last_path.add_sub_path(prev, static_cast<unsigned int>(i_multibuffer.size()) - 1, 0, move_id - 1);
            //}
            if (t_buffer.render_primitive_type == TBuffer::ERenderPrimitiveType::Triangle) {
                Path& last_path = t_buffer.paths.back();
                if (prev.type == curr.type && last_path.matches(curr))
                    last_path.add_sub_path(prev, static_cast<unsigned int>(i_multibuffer.size()) - 1, 0, move_id - 1);
            }
        }

        // if adding the vertices for the current segment exceeds the threshold size of the current vertex buffer
        // create another index buffer
        // BBS: support multi points in one MoveVertice, should multiply point number
        size_t vertices_size_to_add = (t_buffer.render_primitive_type == TBuffer::ERenderPrimitiveType::BatchedModel) ? t_buffer.model.data.vertices_size_bytes() : points_num * t_buffer.max_vertices_per_segment_size_bytes();
        if (curr_vertex_buffer.second * t_buffer.vertices.vertex_size_bytes() > t_buffer.vertices.max_size_bytes() - vertices_size_to_add) {
#if ENABLE_VECTOR_HYBRID_BACKEND
            if (!i_multibuffer.empty()) {
                i_multibuffer.back().flush_memory_and_close();
            }
            i_multibuffer.push_back(IndexBuffer(enable_mem_compress));
#else
            i_multibuffer.push_back(IndexBuffer());
#endif

            ++curr_vertex_buffer.first;
            curr_vertex_buffer.second = 0;
            vbo_index_list.push_back(t_buffer.vertices.vbos[curr_vertex_buffer.first]);
            //if (t_buffer.render_primitive_type != TBuffer::ERenderPrimitiveType::BatchedModel) {
            //     Path& last_path = t_buffer.paths.back();
            //    last_path.add_sub_path(prev, static_cast<unsigned int>(i_multibuffer.size()) - 1, 0, move_id - 1);
            //}
            if (t_buffer.render_primitive_type == TBuffer::ERenderPrimitiveType::Triangle) {
                Path& last_path = t_buffer.paths.back();
                if (prev.type == curr.type && last_path.matches(curr))
                    last_path.add_sub_path(prev, static_cast<unsigned int>(i_multibuffer.size()) - 1, 0, move_id - 1);
            }
        }

        IndexBuffer& i_buffer = i_multibuffer.back();

        switch (t_buffer.render_primitive_type)
        {
        case TBuffer::ERenderPrimitiveType::Line: {
            add_indices_as_line(prev, curr, t_buffer, curr_vertex_buffer.second, static_cast<unsigned int>(i_multibuffer.size()) - 1, i_buffer, move_id);
            break;
        }
        case TBuffer::ERenderPrimitiveType::Triangle: {
            add_indices_as_solid(prev, curr, next, t_buffer, curr_vertex_buffer.second, static_cast<unsigned int>(i_multibuffer.size()) - 1, i_buffer, move_id);
            break;
        }
        case TBuffer::ERenderPrimitiveType::BatchedModel: {
            add_indices_as_model_batch(t_buffer.model.data, i_buffer, curr_vertex_buffer.second);
            curr_vertex_buffer.second += t_buffer.model.data.vertices_count();
            break;
        }
        default: { break; }
        }
    }

    for (MultiIndexBuffer& i_multibuffer : indices) {
        for (IndexBuffer& i_buffer : i_multibuffer) {
            i_buffer.shrink_to_fit();
        }
    }

#if ENABLE_VECTOR_HYBRID_BACKEND
    std::vector<unsigned short> tmp_index_buffer;
#endif
    // toolpaths data -> send indices data to gpu
    for (size_t i = 0; i < m_buffers.size(); ++i) {

        if (is_lite_mode && !(i == buffer_id(EMoveType::Extrude) || i == buffer_id(EMoveType::Seam) || i == buffer_id(EMoveType::Unretract) || i == buffer_id(EMoveType::Pause_Print)))
            continue;

        TBuffer& t_buffer = m_buffers[i];
        if (t_buffer.render_primitive_type != TBuffer::ERenderPrimitiveType::InstancedModel) {
            MultiIndexBuffer& i_multibuffer = indices[i];
            for (IndexBuffer& i_buffer : i_multibuffer) {
                const size_t size_elements = i_buffer.size();
                const size_t size_bytes = size_elements * sizeof(IBufferType);

                // stores index buffer informations into TBuffer
                t_buffer.indices.push_back(IBuffer());
                IBuffer& ibuf = t_buffer.indices.back();
                ibuf.count = size_elements;
                ibuf.vbo = vbo_indices[i][t_buffer.indices.size() - 1];

#if ENABLE_GCODE_VIEWER_STATISTICS
                m_statistics.total_indices_gpu_size += static_cast<int64_t>(size_bytes);
                m_statistics.max_ibuffer_gpu_size = std::max(m_statistics.max_ibuffer_gpu_size, static_cast<int64_t>(size_bytes));
                ++m_statistics.ibuffers_count;
#endif // ENABLE_GCODE_VIEWER_STATISTICS

                glsafe(::glGenBuffers(1, &ibuf.ibo));
                glsafe(::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibuf.ibo));

#if ENABLE_VECTOR_HYBRID_BACKEND
                if (enable_mem_compress) {
#if 0
                    glsafe(::glBufferData(GL_ELEMENT_ARRAY_BUFFER, size_bytes, nullptr, GL_STATIC_DRAW));

                    Slic3r::ChunkCallback callback = [](const std::vector<unsigned short>& chunk, const int offset,
                                                               const int chunk_size) {
                        glsafe(::glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, offset * sizeof(unsigned short),
                                                chunk_size * sizeof(unsigned short),
                                          chunk.data()));
                    };
                    bool result = i_buffer.loop_load_chunks(callback);
                    if (result == false) {
						//
					}
#endif
					size_t loaded_size = i_buffer.load_all_data(tmp_index_buffer);
					if (loaded_size == size_elements) {
                        glsafe(::glBufferData(GL_ELEMENT_ARRAY_BUFFER, size_bytes, tmp_index_buffer.data(), GL_STATIC_DRAW));
                    } else {
						// something error happen;
					}
					
				} else {
                    glsafe(::glBufferData(GL_ELEMENT_ARRAY_BUFFER, size_bytes, i_buffer.data(), GL_STATIC_DRAW));
                }
#else
                
				glsafe(::glBufferData(GL_ELEMENT_ARRAY_BUFFER, size_bytes, i_buffer.data(), GL_STATIC_DRAW));

#endif // ENABLE_VECTOR_HYBRID_BACKEND
                
				glsafe(::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0));
            }
        }
    }

#if ENABLE_VECTOR_HYBRID_BACKEND
    std::vector<unsigned short>().swap(tmp_index_buffer);
#endif

    if (progress_dialog != nullptr) {
        progress_dialog->Update(100, "");
        progress_dialog->Fit();
    }

#if ENABLE_GCODE_VIEWER_STATISTICS
    for (const TBuffer& buffer : m_buffers) {
        m_statistics.paths_size += SLIC3R_STDVEC_MEMSIZE(buffer.paths, Path);
    }

    auto update_segments_count = [&](EMoveType type, int64_t& count) {
        unsigned int id = buffer_id(type);
        const MultiIndexBuffer& buffers = indices[id];
        int64_t indices_count = 0;
        for (const IndexBuffer& buffer : buffers) {
            indices_count += buffer.size();
        }
        const TBuffer& t_buffer = m_buffers[id];
        if (t_buffer.render_primitive_type == TBuffer::ERenderPrimitiveType::Triangle)
            indices_count -= static_cast<int64_t>(12 * t_buffer.paths.size()); // remove the starting + ending caps = 4 triangles

        count += indices_count / t_buffer.indices_per_segment();
    };

    update_segments_count(EMoveType::Travel, m_statistics.travel_segments_count);
    update_segments_count(EMoveType::Wipe, m_statistics.wipe_segments_count);
    update_segments_count(EMoveType::Extrude, m_statistics.extrude_segments_count);

    m_statistics.load_indices = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - smooth_vertices_time).count();
#endif // ENABLE_GCODE_VIEWER_STATISTICS

    log_memory_usage("Loaded G-code generated indices buffers ", vertices, indices);

	MARKING_DIALOG("Indices_Peak_#4");
    // dismiss indices data, no more needed
    std::vector<MultiIndexBuffer>().swap(indices);

	MARKING_DIALOG("END_#5");

    // layers zs / roles / extruder ids -> extract from result
    size_t last_travel_s_id = 0;
    seams_count = 0;
    std::vector<size_t> move_ids_mapper;
    //std::vector<const GCodeProcessorResult::MoveVertex*> vertices_mapper;
    system_memory_stats("move_ids_mapper.resize ");
    if(m_moves_count > 0)
        move_ids_mapper.resize(m_moves_count, -1);

    for (size_t i = 0; i < m_moves_count; ++i) {
        const GCodeProcessorResult::MoveVertex& move = gcode_result.moves[i];
        if (move.type == EMoveType::Seam)
            ++seams_count;
        //else
            //vertices_mapper.push_back(&move);

        size_t move_id = i - seams_count;
        if(move_ids_mapper[move_id] == -1)
            move_ids_mapper[move_id] = i;
        if (move.type == EMoveType::Extrude || move.type == EMoveType::Extrude_Alter) {
        //if (move.type == EMoveType::Extrude) {
            // layers zs
            const double* const last_z = m_layers.empty() ? nullptr : &m_layers.get_zs().back();
            const double z = static_cast<double>(move.position.z());
            if (last_z == nullptr || z < *last_z - EPSILON || *last_z + EPSILON < z)
                m_layers.append(z, { last_travel_s_id, move_id });
            else
                m_layers.get_endpoints().back().last = move_id;
            // extruder ids
            m_extruder_ids.emplace_back(move.extruder_id);
            // roles
            if (i > 0)
                m_roles.emplace_back(move.extrusion_role);
        }
        else if (move.type == EMoveType::Travel) {
            if (move_id - last_travel_s_id > 1 && !m_layers.empty())
                m_layers.get_endpoints().back().last = move_id;

            last_travel_s_id = move_id;
        }
    }

    // anoob , modify layer endpoint range
    // layer is composite of extrude, push continuous traval/retract/unretract/wipe before extrude to the same layer
    {
        std::vector<Layers::Endpoints>& endpoints = m_layers.get_endpoints();
        for (size_t i = 0; i < endpoints.size(); ++i)
        {
            Layers::Endpoints& endpoint = endpoints[i];
            if(i == 0)
                endpoint.first = 0;

            if(i == endpoints.size() - 1)
                endpoint.last = m_moves_count - 1 - seams_count;

            if(i >= 0 && i < endpoints.size() - 1)
            {
                Layers::Endpoints& next_endpoint = endpoints[i + 1];
                for(size_t j = endpoint.last; j > endpoint.first; --j)
                {
                    if(gcode_result.moves[move_ids_mapper[j]].type == EMoveType::Extrude)
                    {
                        endpoint.last = j;
                        next_endpoint.first = j + 1;
                        break;
                    }
                }
            }
        }
    }


    // roles -> remove duplicates
    sort_remove_duplicates(m_roles);
    m_roles.shrink_to_fit();

    // extruder ids -> remove duplicates
    sort_remove_duplicates(m_extruder_ids);
    m_extruder_ids.shrink_to_fit();

    std::vector<int> plater_extruder;
	for (auto mid : m_extruder_ids){
        int eid = mid;
        plater_extruder.push_back(++eid);
	}
    m_plater_extruder = plater_extruder;

    // replace layers for spiral vase mode
    if (!gcode_result.spiral_vase_layers.empty()) {
        m_layers.reset();
        for (const auto& layer : gcode_result.spiral_vase_layers) {
            m_layers.append(layer.first, { layer.second.first, layer.second.second });
        }
    }

    // set layers z range
    if (!m_layers.empty())
        m_layers_z_range = { 0, static_cast<unsigned int>(m_layers.size() - 1) };

    // change color of paths whose layer contains option points
    if (!options_zs.empty()) {
        TBuffer& extrude_buffer = m_buffers[buffer_id(EMoveType::Extrude)];

#if USE_PARALLEL
        tbb::parallel_for(tbb::blocked_range<size_t>(0, extrude_buffer.paths.size()), [&extrude_buffer, &options_zs](const tbb::blocked_range<size_t>& range) {
            for (size_t i = range.begin(); i != range.end(); ++i) {
                Path& path = extrude_buffer.paths[i];
                const float z = path.sub_paths.front().first.position.z();
                if (std::find_if(options_zs.begin(), options_zs.end(), [z](float f) { return f - EPSILON <= z && z <= f + EPSILON; }) !=
                    options_zs.end())
                    path.cp_color_id = 255 - path.cp_color_id;
            }
        });
#else
        for (Path& path : extrude_buffer.paths) {
            const float z = path.sub_paths.front().first.position.z();
            if (std::find_if(options_zs.begin(), options_zs.end(), [z](float f) { return f - EPSILON <= z && z <= f + EPSILON; }) != options_zs.end())
                path.cp_color_id = 255 - path.cp_color_id;
        }
#endif // USE_PARALLEL

    }

#if ENABLE_GCODE_VIEWER_STATISTICS
    m_statistics.load_time = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - start_time).count();
#endif // ENABLE_GCODE_VIEWER_STATISTICS

    if (progress_dialog != nullptr)
        progress_dialog->Destroy();

    BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << " end memory info " << log_memory_info();
    boost::log::core::get()->flush();
    system_memory_stats(__FUNCTION__);
}

void GCodeViewer::load_shells(const Print& print, bool initialized, bool force_previewing)
{
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(": initialized=%1%, force_previewing=%2%")%initialized %force_previewing;
    if ((print.id().id == m_shells.print_id)&&(print.get_modified_count() == m_shells.print_modify_count)) {
        //BBS: update force previewing logic
        if (force_previewing)
            m_shells.previewing = force_previewing;
        //already loaded
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << boost::format(": already loaded, print=%1% print_id=%2%, print_modify_count=%3%, force_previewing %4%")%(&print) %m_shells.print_id %m_shells.print_modify_count %force_previewing;
        return;
    }

    //reset shell firstly
    reset_shell();

    //BBS: move behind of reset_shell, to clear previous shell for empty plate
    if (print.objects().empty()) {
        // no shells, return
        return;
    }
    // adds objects' volumes
    // BBS: fix the issue that object_idx is not assigned as index of Model.objects array
    int object_count = 0;
    const ModelObjectPtrs& model_objs = wxGetApp().model().objects;
	bool  enable_lod   = false;
    for (const PrintObject* obj : print.objects()) {
        const ModelObject* model_obj = obj->model_object();

        int object_idx = -1;
        for (int idx = 0; idx < model_objs.size(); idx++) {
            if (model_objs[idx]->id() == model_obj->id()) {
                object_idx = idx;
                break;
            }
        }

        // BBS: object may be deleted when this method is called when deleting an object
        if (object_idx == -1)
            continue;

        std::vector<int> instance_ids(model_obj->instances.size());
        //BBS: only add the printable instance
        int instance_index = 0;
        for (int i = 0; i < (int)model_obj->instances.size(); ++i) {
            //BBS: only add the printable instance
            if (model_obj->instances[i]->is_printable())
                instance_ids[instance_index++] = i;
        }
        instance_ids.resize(instance_index);

        size_t current_volumes_count = m_shells.volumes.volumes.size();
        m_shells.volumes.load_object(model_obj, object_idx, instance_ids, "object", initialized, enable_lod);

        // adjust shells' z if raft is present
        bool machine_is_belt = false;
        if (wxGetApp().preset_bundle) {
            machine_is_belt = wxGetApp().preset_bundle->machine_is_belt();
        }
        const SlicingParameters& slicing_parameters = obj->slicing_parameters();
        if (slicing_parameters.object_print_z_min != 0.0) {
            const Vec3d z_offset = slicing_parameters.object_print_z_min * (machine_is_belt ? Vec3d{0.0f, 0.0f, 0.0f} : Vec3d::UnitZ());
            for (size_t i = current_volumes_count; i < m_shells.volumes.volumes.size(); ++i) {
                GLVolume* v = m_shells.volumes.volumes[i];
                v->set_volume_offset(v->get_volume_offset() + z_offset);
            }
        }

        object_count++;
    }

    // Orca: disable wipe tower shell
    // if (wxGetApp().preset_bundle->printers.get_edited_preset().printer_technology() == ptFFF) {
        //     // BBS: adds wipe tower's volume
        //     std::vector<unsigned int> print_extruders = print.extruders(true);
        //     int extruders_count = print_extruders.size();

        //     const double max_z = print.objects()[0]->model_object()->get_model()->bounding_box().max(2);
        //     const PrintConfig& config = print.config();
        //     if (config.enable_prime_tower &&
            //         (print.enable_timelapse_print() || (extruders_count > 1 && (config.print_sequence == PrintSequence::ByLayer)))) {
            //         const float depth = print.wipe_tower_data(extruders_count).depth;
            //         const float brim_width = print.wipe_tower_data(extruders_count).brim_width;

            //         int plate_idx = print.get_plate_index();
            //         Vec3d plate_origin = print.get_plate_origin();
            //         double wipe_tower_x = config.wipe_tower_x.get_at(plate_idx) + plate_origin(0);
            //         double wipe_tower_y = config.wipe_tower_y.get_at(plate_idx) + plate_origin(1);
            //         m_shells.volumes.load_wipe_tower_preview(1000, wipe_tower_x, wipe_tower_y, config.prime_tower_width, depth, max_z, config.wipe_tower_rotation_angle,
                //             !print.is_step_done(psWipeTower), brim_width, initialized);
        //     }
    // }

    // remove modifiers
    while (true) {
        GLVolumePtrs::iterator it = std::find_if(m_shells.volumes.volumes.begin(), m_shells.volumes.volumes.end(), [](GLVolume* volume) { return volume->is_modifier; });
        if (it != m_shells.volumes.volumes.end()) {
            m_shells.volumes.release_volume(*it);
            delete (*it);
            m_shells.volumes.volumes.erase(it);
        }
        else
            break;
    }

    for (GLVolume* volume : m_shells.volumes.volumes) {
        volume->zoom_to_volumes = false;
        volume->color.a(0.5f);
        volume->force_native_color = true;
        volume->set_render_color();
        //BBS: add shell bounding box logic
        m_shell_bounding_box.merge(volume->transformed_bounding_box());
    }

    //BBS: always load shell when preview
    m_shells.print_id = print.id().id;
    m_shells.print_modify_count = print.get_modified_count();
    m_shells.previewing = true;
    BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << boost::format(": shell loaded, id change to %1%, modify_count %2%, object count %3%, glvolume count %4%")
        % m_shells.print_id % m_shells.print_modify_count % object_count %m_shells.volumes.volumes.size();
}

#if ENABLE_AUE_CUSTOM_PREVIEW
void GCodeViewer::update_custom_interest_regions() const
{
    if (m_gcode_result == nullptr) {
        m_custom_interest_cache_result_id = 0;
        m_custom_interest_by_ssid.clear();
        return;
    }

    if (m_custom_interest_cache_result_id == m_gcode_result->id &&
        m_custom_interest_by_ssid.size() == m_ssid_to_moveid_map.size())
        return;

    m_custom_interest_cache_result_id = m_gcode_result->id;
    m_custom_interest_by_ssid.clear();

    if (m_ssid_to_moveid_map.size() < 2)
        return;

    m_custom_interest_by_ssid.assign(m_ssid_to_moveid_map.size(), 0);

    // Scheme 2: ROI exists only when slicing from a 3mf and is cached into GCodeProcessorResult.
    // When opening a gcode file directly, do not compute ROI on the fly.
    if (m_only_gcode_in_preview || m_gcode_result->custom_interest_by_move_id.empty())
        return;

    for (size_t end_ssid = 0; end_ssid < m_ssid_to_moveid_map.size(); ++end_ssid) {
        const size_t move_id = m_ssid_to_moveid_map[end_ssid];
        if (move_id < m_gcode_result->custom_interest_by_move_id.size())
            m_custom_interest_by_ssid[end_ssid] = m_gcode_result->custom_interest_by_move_id[move_id];
    }
}

#endif // ENABLE_AUE_CUSTOM_PREVIEW

void GCodeViewer::refresh_render_paths(bool keep_sequential_current_first, bool keep_sequential_current_last) const
{
    if (!m_bLoaded){
        return;
    }

    if (!m_gcode_result) {
        return;
    }

#if ENABLE_AUE_CUSTOM_PREVIEW
    if (m_view_type == EViewType::Custom)
        update_custom_interest_regions();
#endif // ENABLE_AUE_CUSTOM_PREVIEW

#if ENABLE_GCODE_VIEWER_STATISTICS
    auto start_time = std::chrono::high_resolution_clock::now();
#endif // ENABLE_GCODE_VIEWER_STATISTICS

    BOOST_LOG_TRIVIAL(debug) << __FUNCTION__ << boost::format(": enter, m_buffers size %1%!")%m_buffers.size();
    auto extrusion_color = [this](const Path& path) {
        ColorRGBA color;
        switch (m_view_type)
        {
        case EViewType::FeatureType:    { color = Extrusion_Role_Colors[static_cast<unsigned int>(path.role)]; break; }
        case EViewType::Height:         { color = m_extrusions.ranges.height.get_color_at(path.height); break; }
        case EViewType::Width:          { color = m_extrusions.ranges.width.get_color_at(path.width); break; }
        case EViewType::Feedrate:       { color = m_extrusions.ranges.feedrate.get_color_at(path.feedrate); break; }
        case EViewType::FanSpeed:       { color = m_extrusions.ranges.fan_speed.get_color_at(path.fan_speed); break; }
        case EViewType::Temperature:    { color = m_extrusions.ranges.temperature.get_color_at(path.temperature); break; }
        case EViewType::LayerTime:      { color = m_extrusions.ranges.layer_duration.get_color_at(path.layer_time); break; }
        case EViewType::LayerTimeLog:   { color = m_extrusions.ranges.layer_duration_log.get_color_at(path.layer_time); break; }
        case EViewType::VolumetricRate: { color = m_extrusions.ranges.volumetric_rate.get_color_at(path.volumetric_rate); break; }
        case EViewType::Acceleration:   { color = m_extrusions.ranges.acceleration.get_color_at(path.acceleration); break; }
        case EViewType::Tool:           { color = m_tools.m_tool_colors[path.extruder_id]; break; }
        case EViewType::Custom:         { color = m_tools.m_tool_colors[path.extruder_id]; break; }
        case EViewType::ColorPrint:     {
            if (path.cp_color_id >= static_cast<unsigned char>(m_tools.m_tool_colors.size()))
                color = ColorRGBA::GRAY();
            else {
                color = m_tools.m_tool_colors[path.cp_color_id];
                color = adjust_color_for_rendering(color);
            }
            break;
        }
        case EViewType::FilamentId: {
            float id = float(path.extruder_id)/256;
            float role = float(path.role) / 256;
            color      = {id, role, id, 1.0f};
            break;
        }
        default: { color = ColorRGBA::WHITE(); break; }
        }

        return color;
    };

    auto travel_color = [](const Path& path) {
        return (path.delta_extruder < 0.0f) ? Travel_Colors[2] /* Retract */ :
            ((path.delta_extruder > 0.0f) ? Travel_Colors[1] /* Extrude */ :
                Travel_Colors[0] /* Move */);
    };

    auto is_in_layers_range = [this](const Path& path, size_t min_id, size_t max_id) {
        auto in_layers_range = [this, min_id, max_id](size_t id) {
            return m_layers.get_endpoints_at(min_id).first <= id && id <= m_layers.get_endpoints_at(max_id).last;
        };

        return in_layers_range(path.sub_paths.front().first.s_id) && in_layers_range(path.sub_paths.back().last.s_id);
    };

    //BBS
    auto is_extruder_in_layer_range = [this](const Path& path, size_t extruder_id) {
        return path.extruder_id == extruder_id;
    };


    auto is_travel_in_layers_range = [this](size_t path_id, size_t min_id, size_t max_id) {
        const TBuffer& buffer = m_buffers[buffer_id(EMoveType::Travel)];
        if (path_id >= buffer.paths.size())
            return false;

        Path path = buffer.paths[path_id];
        //size_t first = path_id;
        //size_t last = path_id;

        // check adjacent paths
        // while (first > 0 && path.sub_paths.front().first.position.isApprox(buffer.paths[first - 1].sub_paths.back().last.position)) {
        //     --first;
        //     path.sub_paths.front().first = buffer.paths[first].sub_paths.front().first;
        // }
        // while (last < buffer.paths.size() - 1 && path.sub_paths.back().last.position.isApprox(buffer.paths[last + 1].sub_paths.front().first.position)) {
        //     ++last;
        //     path.sub_paths.back().last = buffer.paths[last].sub_paths.back().last;
        // }

        const size_t min_s_id = m_layers.get_endpoints_at(min_id).first;
        const size_t max_s_id = m_layers.get_endpoints_at(max_id).last;

        size_t ffs = path.sub_paths.front().first.s_id;
        size_t bls = path.sub_paths.back().last.s_id;
        return (min_s_id <= ffs && ffs <= max_s_id) || (min_s_id <= bls && bls <= max_s_id);
    };

#if ENABLE_GCODE_VIEWER_STATISTICS
    Statistics* statistics = const_cast<Statistics*>(&m_statistics);
    statistics->render_paths_size = 0;
    statistics->models_instances_size = 0;
#endif // ENABLE_GCODE_VIEWER_STATISTICS

    const bool top_layer_only = true;
    const bool enable_preview_lod = GUI::wxGetApp().app_config->get_bool("enable_preview_lod") && m_gcode_result->should_enable_preview_lod;
    const_cast<bool&>(m_is_lod) = enable_preview_lod;
    //BBS
    SequentialView::Endpoints global_endpoints = { m_sequential_view.gcode_ids.size() , 0 };
    SequentialView::Endpoints top_layer_endpoints = global_endpoints;
    SequentialView* sequential_view = const_cast<SequentialView*>(&m_sequential_view);
    if (top_layer_only || !keep_sequential_current_first) sequential_view->current.first = 0;
    //BBS
    if (!keep_sequential_current_last) sequential_view->current.last = m_sequential_view.gcode_ids.size();

    bool show_surface = show_gcode_surface();

    // first pass: collect visible paths and update sequential view data
    // Store layer index as signed int to preserve sentinel values (e.g. -1) and avoid narrowing later.
    std::vector<std::tuple<unsigned char, unsigned int, unsigned int, unsigned int, int>> paths;

    // Pre-compute s_id boundaries for the current layer range once.
    // This avoids repeated get_endpoints_at() calls (struct returned by value + bounds check)
    // inside the hot inner loop that may iterate over hundreds of thousands of paths.
    const size_t s_range_min = m_layers.get_endpoints_at(m_layers_z_range[0]).first;
    const size_t s_range_max = m_layers.get_endpoints_at(m_layers_z_range[1]).last;
    const size_t s_top_min   = m_layers.get_endpoints_at(m_layers_z_range[1]).first; // s_top_max == s_range_max

	std::set<int>& special_surface_layer = const_cast<std::set<int>&>(m_top_surface_layer);
    special_surface_layer.clear();
    
    for (size_t b = 0; b < m_buffers.size(); ++b) {
        TBuffer& buffer = const_cast<TBuffer&>(m_buffers[b]);
        // reset render paths
        buffer.render_paths.clear();

        if (!buffer.visible)
            continue;

        if (buffer.render_primitive_type == TBuffer::ERenderPrimitiveType::InstancedModel ||
            buffer.render_primitive_type == TBuffer::ERenderPrimitiveType::BatchedModel) {
            for (size_t id : buffer.model.instances.s_ids) {
                if (id < s_range_min || s_range_max < id)
                    continue;

                global_endpoints.first = std::min(global_endpoints.first, id);
                global_endpoints.last = std::max(global_endpoints.last, id);

                if (top_layer_only) {
                    if (id < s_top_min || s_range_max < id)
                        continue;

                    top_layer_endpoints.first = std::min(top_layer_endpoints.first, id);
                    top_layer_endpoints.last = std::max(top_layer_endpoints.last, id);
                }
            }
        }
        else {
            int ref_layer_idx = m_layers_z_range[0];
            
			for (size_t i = 0; i < buffer.paths.size(); ++i) {
                const Path& path = buffer.paths[i];
                const size_t path_front = path.sub_paths.front().first.s_id;
                const size_t path_back  = path.sub_paths.back().last.s_id;

                if (path.type == EMoveType::Travel) {
                    // Travel: accept if either endpoint falls within the layer range (path may span layers).
                    if (!((s_range_min <= path_front && path_front <= s_range_max) ||
                          (s_range_min <= path_back  && path_back  <= s_range_max)))
                        continue;
                }
                else if (path_front < s_range_min || path_back > s_range_max)
                    continue;

                if (path.type == EMoveType::Extrude && !is_visible(path))
                    continue;

                if (m_tools.m_tool_visibles.size() <= path.extruder_id)
                    continue;

                if (m_view_type == EViewType::ColorPrint && !m_tools.m_tool_visibles[path.extruder_id])
                    continue;

				if (show_surface && role_been_filtered_in_lite_mode(path.role))
                    continue;


                if (enable_preview_lod) {
                    Layers::Endpoints endpoints = m_layers.get_endpoints_at(ref_layer_idx);

                    int layer_idx = -1; // get path layer index
                    if (endpoints.first <= path_front && path_back <= endpoints.last) {
                        layer_idx = ref_layer_idx;
                    } else {
                        layer_idx     = get_layer_index(path);
                        ref_layer_idx = layer_idx;
                    }

                    /*if (layer_idx > -1 && stride > 0 && should_be_filtered_of_layer(stride, layer_idx))
                        continue;*/

                    if (path.role == ExtrusionRole::erTopSolidInfill || path.role == ExtrusionRole::erBrim) {
                        special_surface_layer.insert(ref_layer_idx);
                    }
				}

                // store valid path
                for (size_t j = 0; j < path.sub_paths.size(); ++j) {
                    paths.emplace_back(static_cast<unsigned char>(b), path.sub_paths[j].first.b_id, static_cast<unsigned int>(i), static_cast<unsigned int>(j), ref_layer_idx);
                }

                global_endpoints.first = std::min(global_endpoints.first, path_front);
                global_endpoints.last = std::max(global_endpoints.last, path_back);

                if (top_layer_only) {
                    if (path.type == EMoveType::Travel) {
                        // Travel top: at least one endpoint in the top layer range.
                        if ((s_top_min <= path_front && path_front <= s_range_max) ||
                            (s_top_min <= path_back  && path_back  <= s_range_max)) {
                            top_layer_endpoints.first = std::min(top_layer_endpoints.first, path_front);
                            top_layer_endpoints.last = std::max(top_layer_endpoints.last, path_back);
                        }
                    }
                    else if (path_front >= s_top_min && path_back <= s_range_max) {
                        top_layer_endpoints.first = std::min(top_layer_endpoints.first, path_front);
                        top_layer_endpoints.last = std::max(top_layer_endpoints.last, path_back);
                    }
                }
            }
        }
    }

	for (const int& x : m_gcode_result->wipe_tower_tool_changes_layers) {
        special_surface_layer.insert(x);
	}

    // update current sequential position
    sequential_view->current.first = !top_layer_only && keep_sequential_current_first ? std::clamp(sequential_view->current.first, global_endpoints.first, global_endpoints.last) : global_endpoints.first;
    if (global_endpoints.last == 0) {
m_no_render_path = true;
        sequential_view->current.last = global_endpoints.last;
    } else {
m_no_render_path = false;
        sequential_view->current.last = keep_sequential_current_last ? std::clamp(sequential_view->current.last, global_endpoints.first, global_endpoints.last) : global_endpoints.last;
    }

    // get the world position from the vertex buffer
    bool found = false;
    for (const TBuffer& buffer : m_buffers) {
        if (buffer.render_primitive_type == TBuffer::ERenderPrimitiveType::InstancedModel ||
            buffer.render_primitive_type == TBuffer::ERenderPrimitiveType::BatchedModel) {
            for (size_t i = 0; i < buffer.model.instances.s_ids.size(); ++i) {
                if (buffer.model.instances.s_ids[i] == m_sequential_view.current.last) {
                    size_t offset = i * buffer.model.instances.instance_size_floats();
                    sequential_view->current_position.x() = buffer.model.instances.buffer[offset + 0];
                    sequential_view->current_position.y() = buffer.model.instances.buffer[offset + 1];
                    sequential_view->current_position.z() = buffer.model.instances.buffer[offset + 2];
                    sequential_view->current_offset = buffer.model.instances.offsets[i];
                    found = true;
                    break;
                }
            }
        }
        else {
            // searches the path containing the current position
            for (const Path& path : buffer.paths) {
                if (path.contains(m_sequential_view.current.last)) {
                    const int sub_path_id = path.get_id_of_sub_path_containing(m_sequential_view.current.last);
                    if (sub_path_id != -1) {
#if 0
                        const Path::Sub_Path& sub_path = path.sub_paths[sub_path_id];
                        unsigned int offset = static_cast<unsigned int>(m_sequential_view.current.last - sub_path.first.s_id);
                        if (offset > 0) {
                            if (buffer.render_primitive_type == TBuffer::ERenderPrimitiveType::Line) {
                                for (size_t i = sub_path.first.s_id + 1; i < m_sequential_view.current.last + 1; i++) {
                                    size_t move_id = m_ssid_to_moveid_map[i];
                                    const GCodeProcessorResult::MoveVertex& curr = m_gcode_result->moves[move_id];
                                    if (curr.is_arc_move()) {
                                        offset += curr.interpolation_points.size();
                                    }
                                }
                                offset = 2 * offset - 1;
                            }
                            else if (buffer.render_primitive_type == TBuffer::ERenderPrimitiveType::Triangle) {
                                unsigned int indices_count = buffer.indices_per_segment();
                                // BBS: modify to support moves which has internal point
                                for (size_t i = sub_path.first.s_id + 1; i < m_sequential_view.current.last + 1; i++) {
                                    size_t move_id = m_ssid_to_moveid_map[i];
                                    const GCodeProcessorResult::MoveVertex& curr = m_gcode_result->moves[move_id];
                                    if (curr.is_arc_move()) {
                                        offset += curr.interpolation_points.size();
                                    }
                                }
                                offset = indices_count * (offset - 1) + (indices_count - 2);
                                if (sub_path_id == 0)
                                    offset += 6; // add 2 triangles for starting cap
                            }
                        }
                        offset += static_cast<unsigned int>(sub_path.first.i_id);

                        // gets the vertex index from the index buffer on gpu
                        const IBuffer& i_buffer = buffer.indices[sub_path.first.b_id];
                        unsigned int index = 0;
                        glsafe(::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, i_buffer.ibo));
                        glsafe(::glGetBufferSubData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLintptr>(offset * sizeof(IBufferType)), static_cast<GLsizeiptr>(sizeof(IBufferType)), static_cast<void*>(&index)));
                        glsafe(::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0));

                        // gets the position from the vertices buffer on gpu
                        glsafe(::glBindBuffer(GL_ARRAY_BUFFER, i_buffer.vbo));
                        glsafe(::glGetBufferSubData(GL_ARRAY_BUFFER, static_cast<GLintptr>(index * buffer.vertices.vertex_size_bytes()), static_cast<GLsizeiptr>(3 * sizeof(float)), static_cast<void*>(sequential_view->current_position.data())));
                        glsafe(::glBindBuffer(GL_ARRAY_BUFFER, 0));

#else                
						size_t move_id = m_ssid_to_moveid_map[m_sequential_view.current.last];
                        Vec3f position = m_gcode_result->moves[move_id].position;
                        sequential_view->current_position = position;
#endif
                        sequential_view->current_offset = Vec3f::Zero();
                        found = true;
                        break;
                    }
                }
            }
        }

        if (found)
            break;
    }

    // second pass: filter paths by sequential data and collect them by color
    RenderPath* render_path = nullptr;
    for (const auto& [tbuffer_id, ibuffer_id, path_id, sub_path_id, layer_idx] : paths) {
        TBuffer& buffer = const_cast<TBuffer&>(m_buffers[tbuffer_id]);
        const Path& path = buffer.paths[path_id];
        const Path::Sub_Path& sub_path = path.sub_paths[sub_path_id];
        if (m_sequential_view.current.last < sub_path.first.s_id || sub_path.last.s_id < m_sequential_view.current.first)
            continue;
        
        // Creality:for appearance shortage
#if ENABLE_AUE_CUSTOM_PREVIEW
        if (path.type == EMoveType::Extrude && m_view_type == EViewType::Custom &&
            buffer.render_primitive_type == TBuffer::ERenderPrimitiveType::Triangle) {
            const bool can_highlight = (!top_layer_only ||
                m_sequential_view.current.last == global_endpoints.last ||
                is_in_layers_range(path, m_layers_z_range[1], m_layers_z_range[1]));
            // Keep the same per-layer filtering semantics as the original preview:
            // when other layers are neutralized, the ROI highlight must not "leak" through.
            if (can_highlight) {
                const ColorRGBA base_color = extrusion_color(path);
                const ColorRGBA filament_color = (path.extruder_id < m_tools.m_tool_colors.size()) ? m_tools.m_tool_colors[path.extruder_id] : ColorRGBA::GRAY();
                const auto highlight_colors = pick_interest_region_highlight_colors(filament_color);
                const ColorRGBA& trigger_color = highlight_colors.first;
                const ColorRGBA& defect_color  = highlight_colors.second;

                const size_t draw_min = std::max(m_sequential_view.current.first, sub_path.first.s_id);
                const size_t draw_max = std::min(m_sequential_view.current.last, sub_path.last.s_id);
                if (draw_max <= draw_min)
                    continue;

                auto interest_tag = [this](size_t end_ssid) -> unsigned char {
                    return (end_ssid < m_custom_interest_by_ssid.size()) ? m_custom_interest_by_ssid[end_ssid] : 0;
                };

                auto color_for_tag = [&](unsigned char tag) -> ColorRGBA {
                    // InterestRegion::SegmentTag::Trigger == 1, Defect == 2.
                    if (tag == 1)
                        return trigger_color;
                    if (tag == 2)
                        return defect_color;
                    return base_color;
                };

                auto append_chunk = [&](size_t chunk_min_s_id, size_t chunk_max_s_id, const ColorRGBA& chunk_color, bool skip_start_corner_cap) {
                    RenderPath key{ tbuffer_id, chunk_color, static_cast<unsigned int>(ibuffer_id), path_id, layer_idx };
                    if (enable_preview_lod) {
                        if (render_path == nullptr || !RenderPathPropertyEqual_WithLayerNo()(*render_path, key)) {
                            buffer.render_paths.emplace_back(key);
                            render_path = const_cast<RenderPath*>(&buffer.render_paths.back());
                        }
                    } else {
                        if (render_path == nullptr || !RenderPathPropertyEqual()(*render_path, key)) {
                            buffer.render_paths.emplace_back(key);
                            render_path = const_cast<RenderPath*>(&buffer.render_paths.back());
                        }
                    }

                    unsigned int size_in_indices = 0;
                    // BBS: modify to support moves which has internal point
                    const size_t max_s_id = chunk_max_s_id;
                    const size_t min_s_id = chunk_min_s_id;
                    unsigned int segments_count = static_cast<unsigned int>(max_s_id - min_s_id);
#if 0
                    for (size_t i = min_s_id + 1; i < max_s_id + 1; ++i) {
                        const size_t move_id = m_ssid_to_moveid_map[i];
                        const GCodeProcessorResult::MoveVertex& curr = m_gcode_result->moves[move_id];
                        if (curr.is_arc_move())
                            segments_count += static_cast<unsigned int>(curr.interpolation_points.size());
                    }
#else
                    // O(1) prefix-sum query replaces the former O(N) inner loop over arc moves
                    if (max_s_id < m_ssid_arc_extra_segments.size() && min_s_id < m_ssid_arc_extra_segments.size())
                        segments_count += m_ssid_arc_extra_segments[max_s_id] - m_ssid_arc_extra_segments[min_s_id];
#endif
                    size_in_indices = buffer.indices_per_segment() * segments_count;

                    if (size_in_indices == 0)
                        return;

                    if (sub_path_id == 0 && chunk_min_s_id == sub_path.first.s_id)
                        size_in_indices += 6; // add 2 triangles for starting cap
                    if (sub_path_id == path.sub_paths.size() - 1 && chunk_max_s_id == path.sub_paths.back().last.s_id && path.sub_paths.back().last.s_id <= m_sequential_view.current.last)
                        size_in_indices += 6; // add 2 triangles for ending cap
                    if (skip_start_corner_cap && chunk_min_s_id > sub_path.first.s_id)
                        size_in_indices -= 6; // remove 2 triangles for corner cap

                    render_path->sizes.push_back(size_in_indices);

                    unsigned int delta_1st = static_cast<unsigned int>(chunk_min_s_id - sub_path.first.s_id);
                    delta_1st *= buffer.indices_per_segment();
                    if (delta_1st > 0) {
                        if (sub_path_id == 0)
                            delta_1st += 6; // skip 2 triangles for starting cap
                        if (skip_start_corner_cap)
                            delta_1st += 6; // skip 2 triangles for corner cap
                    }

                    render_path->offsets.push_back(static_cast<size_t>((sub_path.first.i_id + delta_1st) * sizeof(IBufferType)));
                };

                const bool draw_is_trimmed_start = (draw_min > sub_path.first.s_id);
                size_t block_first_end = draw_min + 1;
                unsigned char block_tag = interest_tag(block_first_end);
                for (size_t end_ssid = draw_min + 1; end_ssid <= draw_max; ++end_ssid) {
                    const unsigned char tag = interest_tag(end_ssid);
                    if (tag != block_tag) {
                        const size_t chunk_min = block_first_end - 1;
                        const size_t chunk_max = end_ssid - 1;
                        const bool skip_corner = draw_is_trimmed_start && (chunk_min == draw_min);
                        append_chunk(chunk_min, chunk_max, color_for_tag(block_tag), skip_corner);
                        block_first_end = end_ssid;
                        block_tag = tag;
                    }
                }

                {
                    const size_t chunk_min = block_first_end - 1;
                    const size_t chunk_max = draw_max;
                    const bool skip_corner = draw_is_trimmed_start && (chunk_min == draw_min);
                    append_chunk(chunk_min, chunk_max, color_for_tag(block_tag), skip_corner);
                }

                continue;
            }
        }
#endif // ENABLE_AUE_CUSTOM_PREVIEW

        ColorRGBA color;
        switch (path.type)
        {
        case EMoveType::Tool_change:
        case EMoveType::Color_change:
        case EMoveType::Pause_Print:
        case EMoveType::Custom_GCode:
        case EMoveType::Retract:
        case EMoveType::Unretract:
        case EMoveType::Seam: { color = option_color(path.type); break; }
        case EMoveType::Extrude: {
            if (!top_layer_only ||
                m_sequential_view.current.last == global_endpoints.last ||
                is_in_layers_range(path, m_layers_z_range[1], m_layers_z_range[1]))
                color = extrusion_color(path);
            else
                color = Neutral_Color;

            break;
        }
        case EMoveType::Travel: {
            if (!top_layer_only || m_sequential_view.current.last == global_endpoints.last || is_travel_in_layers_range(path_id, m_layers_z_range[1], m_layers_z_range[1]))
#if ENABLE_AUE_CUSTOM_PREVIEW
                color = (m_view_type == EViewType::Feedrate || m_view_type == EViewType::Tool || m_view_type == EViewType::Custom) ? extrusion_color(path) : travel_color(path);
#else
                color = (m_view_type == EViewType::Feedrate || m_view_type == EViewType::Tool) ? extrusion_color(path) : travel_color(path);
#endif // ENABLE_AUE_CUSTOM_PREVIEW
            else
                color = Neutral_Color;

            break;
        }
        case EMoveType::Wipe: { color = Wipe_Color; break; }
        default: { color = { 0.0f, 0.0f, 0.0f, 1.0f }; break; }
        }

        RenderPath key{ tbuffer_id, color, static_cast<unsigned int>(ibuffer_id), path_id, layer_idx};
        if (enable_preview_lod) {
            if (render_path == nullptr || !RenderPathPropertyEqual_WithLayerNo()(*render_path, key)) {
                buffer.render_paths.emplace_back(key);
                render_path = const_cast<RenderPath*>(&buffer.render_paths.back());
            }
        } else {
            if (render_path == nullptr || !RenderPathPropertyEqual()(*render_path, key)) {
                buffer.render_paths.emplace_back(key);
                render_path = const_cast<RenderPath*>(&buffer.render_paths.back());
            }
		}
		
        unsigned int delta_1st = 0;
        if (sub_path.first.s_id < m_sequential_view.current.first && m_sequential_view.current.first <= sub_path.last.s_id)
            delta_1st = static_cast<unsigned int>(m_sequential_view.current.first - sub_path.first.s_id);

        unsigned int size_in_indices = 0;
        switch (buffer.render_primitive_type)
        {
        case TBuffer::ERenderPrimitiveType::Line:
        case TBuffer::ERenderPrimitiveType::Triangle: {
            // BBS: modify to support moves which has internal point
            size_t max_s_id = std::min(m_sequential_view.current.last, sub_path.last.s_id);
            size_t min_s_id = std::max(m_sequential_view.current.first, sub_path.first.s_id);
            unsigned int segments_count = max_s_id - min_s_id;
#if 0
            for (size_t i = min_s_id + 1; i < max_s_id + 1; i++) {
                size_t move_id = m_ssid_to_moveid_map[i];
                const GCodeProcessorResult::MoveVertex& curr = m_gcode_result->moves[move_id];
                if (curr.is_arc_move()) {
                    segments_count += curr.interpolation_points.size();
                }
            }
#else 
            // O(1) prefix-sum query replaces the former O(N) inner loop over arc moves
            if (max_s_id < m_ssid_arc_extra_segments.size() && min_s_id < m_ssid_arc_extra_segments.size())
                segments_count += m_ssid_arc_extra_segments[max_s_id] - m_ssid_arc_extra_segments[min_s_id];
#endif
            size_in_indices = buffer.indices_per_segment() * segments_count;
            break;
        }
        default: { break; }
        }

        if (size_in_indices == 0)
            continue;

        if (buffer.render_primitive_type == TBuffer::ERenderPrimitiveType::Triangle) {
            if (sub_path_id == 0 && delta_1st == 0)
                size_in_indices += 6; // add 2 triangles for starting cap
            if (sub_path_id == path.sub_paths.size() - 1 && path.sub_paths.back().last.s_id <= m_sequential_view.current.last)
                size_in_indices += 6; // add 2 triangles for ending cap
            if (delta_1st > 0)
                size_in_indices -= 6; // remove 2 triangles for corner cap
        }

        render_path->sizes.push_back(size_in_indices);

        if (buffer.render_primitive_type == TBuffer::ERenderPrimitiveType::Triangle) {
            delta_1st *= buffer.indices_per_segment();
            if (delta_1st > 0) {
                delta_1st += 6; // skip 2 triangles for corner cap
                if (sub_path_id == 0)
                    delta_1st += 6; // skip 2 triangles for starting cap
            }
        }

        render_path->offsets.push_back(static_cast<size_t>((sub_path.first.i_id + delta_1st) * sizeof(IBufferType)));

#if 0
        // check sizes and offsets against index buffer size on gpu
        GLint buffer_size;
        glsafe(::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, buffer->indices[render_path->ibuffer_id].ibo));
        glsafe(::glGetBufferParameteriv(GL_ELEMENT_ARRAY_BUFFER, GL_BUFFER_SIZE, &buffer_size));
        glsafe(::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0));
        if (render_path->offsets.back() + render_path->sizes.back() * sizeof(IBufferType) > buffer_size)
            BOOST_LOG_TRIVIAL(error) << "GCodeViewer::refresh_render_paths: Invalid render path data";
#endif
    }

    // Removes empty render paths and sort.
    for (size_t b = 0; b < m_buffers.size(); ++b) {
        TBuffer* buffer = const_cast<TBuffer*>(&m_buffers[b]);
        buffer->render_paths.erase(std::remove_if(buffer->render_paths.begin(), buffer->render_paths.end(),
            [](const auto &path){ return path.sizes.empty() || path.offsets.empty(); }),
            buffer->render_paths.end());
    }

    // second pass: for buffers using instanced and batched models, update the instances render ranges
    for (size_t b = 0; b < m_buffers.size(); ++b) {
        TBuffer& buffer = const_cast<TBuffer&>(m_buffers[b]);
        if (buffer.render_primitive_type != TBuffer::ERenderPrimitiveType::InstancedModel &&
            buffer.render_primitive_type != TBuffer::ERenderPrimitiveType::BatchedModel)
            continue;

        buffer.model.instances.render_ranges.reset();

        if (!buffer.visible || buffer.model.instances.s_ids.empty())
            continue;

        buffer.model.instances.render_ranges.ranges.push_back({ 0, 0, 0, buffer.model.color });
        bool has_second_range = top_layer_only && m_sequential_view.current.last != m_sequential_view.global.last;
        if (has_second_range)
            buffer.model.instances.render_ranges.ranges.push_back({ 0, 0, 0, Neutral_Color });

        if (m_sequential_view.current.first <= buffer.model.instances.s_ids.back() && buffer.model.instances.s_ids.front() <= m_sequential_view.current.last) {
            for (size_t id : buffer.model.instances.s_ids) {
                if (has_second_range) {
                    if (id < m_sequential_view.endpoints.first) {
                        ++buffer.model.instances.render_ranges.ranges.front().offset;
                        if (id <= m_sequential_view.current.first)
                            ++buffer.model.instances.render_ranges.ranges.back().offset;
                        else
                            ++buffer.model.instances.render_ranges.ranges.back().count;
                    }
                    else if (id <= m_sequential_view.current.last)
                        ++buffer.model.instances.render_ranges.ranges.front().count;
                    else
                        break;
                }
                else {
                    if (id <= m_sequential_view.current.first)
                        ++buffer.model.instances.render_ranges.ranges.front().offset;
                    else if (id <= m_sequential_view.current.last)
                        ++buffer.model.instances.render_ranges.ranges.front().count;
                    else
                        break;
                }
            }
        }
    }

    // set sequential data to their final value
    sequential_view->endpoints = top_layer_only ? top_layer_endpoints : global_endpoints;
    sequential_view->current.first = !top_layer_only && keep_sequential_current_first ? std::clamp(sequential_view->current.first, sequential_view->endpoints.first, sequential_view->endpoints.last) : sequential_view->endpoints.first;
    sequential_view->global = global_endpoints;

    // updates sequential range caps
    std::array<SequentialRangeCap, 2>* sequential_range_caps = const_cast<std::array<SequentialRangeCap, 2>*>(&m_sequential_range_caps);
    (*sequential_range_caps)[0].reset();
    (*sequential_range_caps)[1].reset();

    if (m_sequential_view.current.first != m_sequential_view.current.last) {
        for (const auto& [tbuffer_id, ibuffer_id, path_id, sub_path_id, layer_idx] : paths) {
            TBuffer& buffer = const_cast<TBuffer&>(m_buffers[tbuffer_id]);
            if (buffer.render_primitive_type != TBuffer::ERenderPrimitiveType::Triangle)
                continue;

            const Path& path = buffer.paths[path_id];
            const Path::Sub_Path& sub_path = path.sub_paths[sub_path_id];
            if (m_sequential_view.current.last <= sub_path.first.s_id || sub_path.last.s_id <= m_sequential_view.current.first)
                continue;

            // update cap for first endpoint of current range
            if (m_sequential_view.current.first > sub_path.first.s_id) {
                SequentialRangeCap& cap = (*sequential_range_caps)[0];
                const IBuffer& i_buffer = buffer.indices[ibuffer_id];
                cap.buffer = &buffer;
                cap.vbo = i_buffer.vbo;

                // calculate offset into the index buffer
                unsigned int offset = sub_path.first.i_id;
                offset += 6; // add 2 triangles for corner cap
                offset += static_cast<unsigned int>(m_sequential_view.current.first - sub_path.first.s_id) * buffer.indices_per_segment();
                if (sub_path_id == 0)
                    offset += 6; // add 2 triangles for starting cap

                // extract indices from index buffer
                std::array<IBufferType, 6> indices{ 0, 0, 0, 0, 0, 0 };
                glsafe(::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, i_buffer.ibo));
                glsafe(::glGetBufferSubData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLintptr>((offset + 0) * sizeof(IBufferType)), static_cast<GLsizeiptr>(sizeof(IBufferType)), static_cast<void*>(&indices[0])));
                glsafe(::glGetBufferSubData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLintptr>((offset + 7) * sizeof(IBufferType)), static_cast<GLsizeiptr>(sizeof(IBufferType)), static_cast<void*>(&indices[1])));
                glsafe(::glGetBufferSubData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLintptr>((offset + 1) * sizeof(IBufferType)), static_cast<GLsizeiptr>(sizeof(IBufferType)), static_cast<void*>(&indices[2])));
                glsafe(::glGetBufferSubData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLintptr>((offset + 13) * sizeof(IBufferType)), static_cast<GLsizeiptr>(sizeof(IBufferType)), static_cast<void*>(&indices[4])));
                glsafe(::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0));
                indices[3] = indices[0];
                indices[5] = indices[1];

                // send indices to gpu
                glsafe(::glGenBuffers(1, &cap.ibo));
                glsafe(::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, cap.ibo));
                glsafe(::glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(IBufferType), indices.data(), GL_STATIC_DRAW));
                glsafe(::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0));

                // extract color from render path
                size_t offset_bytes = offset * sizeof(IBufferType);
                for (const RenderPath& render_path : buffer.render_paths) {
                    if (render_path.ibuffer_id == ibuffer_id) {
                        for (size_t j = 0; j < render_path.offsets.size(); ++j) {
                            if (render_path.contains(offset_bytes)) {
                                cap.color = render_path.color;
                                break;
                            }
                        }
                    }
                }
            }

            // update cap for last endpoint of current range
            if (m_sequential_view.current.last < sub_path.last.s_id) {
                SequentialRangeCap& cap = (*sequential_range_caps)[1];
                const IBuffer& i_buffer = buffer.indices[ibuffer_id];
                cap.buffer = &buffer;
                cap.vbo = i_buffer.vbo;

                // calculate offset into the index buffer
                unsigned int offset = sub_path.first.i_id;
                offset += 6; // add 2 triangles for corner cap
                offset += static_cast<unsigned int>(m_sequential_view.current.last - 1 - sub_path.first.s_id) * buffer.indices_per_segment();
                if (sub_path_id == 0)
                    offset += 6; // add 2 triangles for starting cap

                // extract indices from index buffer
                std::array<IBufferType, 6> indices{ 0, 0, 0, 0, 0, 0 };
                glsafe(::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, i_buffer.ibo));
                glsafe(::glGetBufferSubData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLintptr>((offset + 2) * sizeof(IBufferType)), static_cast<GLsizeiptr>(sizeof(IBufferType)), static_cast<void*>(&indices[0])));
                glsafe(::glGetBufferSubData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLintptr>((offset + 4) * sizeof(IBufferType)), static_cast<GLsizeiptr>(sizeof(IBufferType)), static_cast<void*>(&indices[1])));
                glsafe(::glGetBufferSubData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLintptr>((offset + 10) * sizeof(IBufferType)), static_cast<GLsizeiptr>(sizeof(IBufferType)), static_cast<void*>(&indices[2])));
                glsafe(::glGetBufferSubData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLintptr>((offset + 16) * sizeof(IBufferType)), static_cast<GLsizeiptr>(sizeof(IBufferType)), static_cast<void*>(&indices[5])));
                glsafe(::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0));
                indices[3] = indices[0];
                indices[4] = indices[2];

                // send indices to gpu
                glsafe(::glGenBuffers(1, &cap.ibo));
                glsafe(::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, cap.ibo));
                glsafe(::glBufferData(GL_ELEMENT_ARRAY_BUFFER, 6 * sizeof(IBufferType), indices.data(), GL_STATIC_DRAW));
                glsafe(::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0));

                // extract color from render path
                size_t offset_bytes = offset * sizeof(IBufferType);
                for (const RenderPath& render_path : buffer.render_paths) {
                    if (render_path.ibuffer_id == ibuffer_id) {
                        for (size_t j = 0; j < render_path.offsets.size(); ++j) {
                            if (render_path.contains(offset_bytes)) {
                                cap.color = render_path.color;
                                break;
                            }
                        }
                    }
                }
            }

            if ((*sequential_range_caps)[0].is_renderable() && (*sequential_range_caps)[1].is_renderable())
                break;
        }
    }

	const_cast<FilterLayerResult &>(m_filter_layer_result).reset();

    //BBS
    enable_moves_slider(!paths.empty());

#if ENABLE_GCODE_VIEWER_STATISTICS
    for (const TBuffer& buffer : m_buffers) {
        statistics->render_paths_size += SLIC3R_STDUNORDEREDSET_MEMSIZE(buffer.render_paths, RenderPath);
        for (const RenderPath& path : buffer.render_paths) {
            statistics->render_paths_size += SLIC3R_STDVEC_MEMSIZE(path.sizes, unsigned int);
            statistics->render_paths_size += SLIC3R_STDVEC_MEMSIZE(path.offsets, size_t);
        }
        statistics->models_instances_size += SLIC3R_STDVEC_MEMSIZE(buffer.model.instances.buffer, float);
        statistics->models_instances_size += SLIC3R_STDVEC_MEMSIZE(buffer.model.instances.s_ids, size_t);
        statistics->models_instances_size += SLIC3R_STDVEC_MEMSIZE(buffer.model.instances.render_ranges.ranges, InstanceVBuffer::Ranges::Range);
    }
    statistics->refresh_paths_time = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - start_time).count();
#endif // ENABLE_GCODE_VIEWER_STATISTICS
}

void GCodeViewer::render_toolpaths()
{
    if (!m_bLoaded) {
        return;
    }

    const Camera& camera = wxGetApp().plater()->get_camera();
    const double zoom = camera.get_zoom();

    auto render_as_lines = [
#if ENABLE_GCODE_VIEWER_STATISTICS
        this
#endif // ENABLE_GCODE_VIEWER_STATISTICS
    ](std::vector<RenderPath>::iterator it_path, std::vector<RenderPath>::iterator it_end, GLShaderProgram& shader, int uniform_color) {
        for (auto it = it_path; it != it_end && it_path->ibuffer_id == it->ibuffer_id; ++it) {
            const RenderPath& path = *it;
            // Some OpenGL drivers crash on empty glMultiDrawElements, see GH #7415.
            assert(! path.sizes.empty());
            assert(! path.offsets.empty());
            shader.set_uniform(uniform_color, path.color);
            glsafe(::glMultiDrawElements(GL_LINES, (const GLsizei*)path.sizes.data(), GL_UNSIGNED_SHORT, (const void* const*)path.offsets.data(), (GLsizei)path.sizes.size()));
#if ENABLE_GCODE_VIEWER_STATISTICS
            ++m_statistics.gl_multi_lines_calls_count;
#endif // ENABLE_GCODE_VIEWER_STATISTICS
        }
    };

    auto render_as_triangles = [
#if ENABLE_GCODE_VIEWER_STATISTICS
        this
#endif // ENABLE_GCODE_VIEWER_STATISTICS
    ](std::vector<RenderPath>::iterator it_path, std::vector<RenderPath>::iterator it_end, GLShaderProgram& shader, int uniform_color) {
        for (auto it = it_path; it != it_end && it_path->ibuffer_id == it->ibuffer_id; ++it) {
            const RenderPath& path = *it;
            // Some OpenGL drivers crash on empty glMultiDrawElements, see GH #7415.
            assert(! path.sizes.empty());
            assert(! path.offsets.empty());
            shader.set_uniform(uniform_color, path.color);
            glsafe(::glMultiDrawElements(GL_TRIANGLES, (const GLsizei*)path.sizes.data(), GL_UNSIGNED_SHORT, (const void* const*)path.offsets.data(), (GLsizei)path.sizes.size()));
#if ENABLE_GCODE_VIEWER_STATISTICS
            ++m_statistics.gl_multi_triangles_calls_count;
#endif // ENABLE_GCODE_VIEWER_STATISTICS
        }
    };


	auto render_as_triangles_2 = [this, render_as_triangles](std::vector<RenderPath>::iterator it_path, std::vector<RenderPath>::iterator it_end, GLShaderProgram& shader, int uniform_color, const int dynamic_stride, const Camera& camera) {
		
		/*const std::vector<double>& z_offsets = get_filtered_layers_z_offset(dynamic_stride); 

		if (z_offsets.size() != m_layers.size()) {
            render_as_triangles(it_path, it_end, shader, uniform_color);
			return;
		}*/

		//int last_layer_no = -1;
        int last_filtered_layer = -1;
        bool last_filtered_layer_result = false;
	
        for (auto it = it_path; it != it_end && it_path->ibuffer_id == it->ibuffer_id; ++it) {
            
			const RenderPath& path = *it;
            // Some OpenGL drivers crash on empty glMultiDrawElements, see GH #7415.
            assert(!path.sizes.empty());
            assert(!path.offsets.empty());

			int current_layer = path.layer_no;
            /*if (should_be_filtered_of_layer(dynamic_stride, current_layer))
                continue;*/

			if (last_filtered_layer == current_layer) {
                if (last_filtered_layer_result)
                    continue;
            } else {
                last_filtered_layer        = current_layer;
                last_filtered_layer_result = should_be_filtered_of_layer(dynamic_stride, current_layer);
                if (last_filtered_layer_result)
                    continue;
			}
			
            shader.set_uniform(uniform_color, path.color);

			/*if (last_layer_no != current_layer && dynamic_stride > 2) {
                last_layer_no = current_layer;
                float z_offset = z_offsets[current_layer];

                const Transform3d model_matrix = Geometry::assemble_transform((z_offset * Vec3f::UnitZ()).cast<double>());
                shader.set_uniform("view_model_matrix", camera.get_view_matrix() * model_matrix);
			}*/
			
            glsafe(::glMultiDrawElements(GL_TRIANGLES, (const GLsizei*)path.sizes.data(), GL_UNSIGNED_SHORT,
                                         (const void* const*) path.offsets.data(), (GLsizei) path.sizes.size()));
#if ENABLE_GCODE_VIEWER_STATISTICS
            ++m_statistics.gl_multi_triangles_calls_count;
#endif // ENABLE_GCODE_VIEWER_STATISTICS
        }
    };

    auto render_as_instanced_model = [
#if ENABLE_GCODE_VIEWER_STATISTICS
        this
#endif // ENABLE_GCODE_VIEWER_STATISTICS
        ](TBuffer& buffer, GLShaderProgram & shader) {
        for (auto& range : buffer.model.instances.render_ranges.ranges) {
            if (range.vbo == 0 && range.count > 0) {
                glsafe(::glGenBuffers(1, &range.vbo));
                glsafe(::glBindBuffer(GL_ARRAY_BUFFER, range.vbo));
                glsafe(::glBufferData(GL_ARRAY_BUFFER, range.count * buffer.model.instances.instance_size_bytes(), (const void*)&buffer.model.instances.buffer[range.offset * buffer.model.instances.instance_size_floats()], GL_STATIC_DRAW));
                glsafe(::glBindBuffer(GL_ARRAY_BUFFER, 0));
            }

            if (range.vbo > 0) {
                buffer.model.model.set_color(range.color);
                buffer.model.model.render_instanced(range.vbo, range.count);
#if ENABLE_GCODE_VIEWER_STATISTICS
                ++m_statistics.gl_instanced_models_calls_count;
                m_statistics.total_instances_gpu_size += static_cast<int64_t>(range.count * buffer.model.instances.instance_size_bytes());
#endif // ENABLE_GCODE_VIEWER_STATISTICS
            }
        }
    };

#if ENABLE_GCODE_VIEWER_STATISTICS
        auto render_as_batched_model = [this](TBuffer& buffer, GLShaderProgram& shader, int position_id, int normal_id) {
#else
        auto render_as_batched_model = [this](TBuffer& buffer, GLShaderProgram& shader, int position_id, int normal_id) {
#endif // ENABLE_GCODE_VIEWER_STATISTICS
        struct Range
        {
            unsigned int first;
            unsigned int last;
            bool intersects(const Range& other) const { return (other.last < first || other.first > last) ? false : true; }
        };
        Range buffer_range = { 0, 0 };
        const size_t indices_per_instance = buffer.model.data.indices_count();

        for (size_t j = 0; j < buffer.indices.size(); ++j) {
            const IBuffer& i_buffer = buffer.indices[j];
            buffer_range.last = buffer_range.first + i_buffer.count / indices_per_instance;
            glsafe(::glBindBuffer(GL_ARRAY_BUFFER, i_buffer.vbo));
            if (position_id != -1) {
                if (m_is_mem_optim) {
                    glsafe(::glVertexAttribPointer(position_id, buffer.vertices.position_size_floats(), GL_SHORT, GL_FALSE,
                                                   buffer.vertices.vertex_size_floats() * sizeof(short),
                                                   (const void*) (buffer.vertices.position_offset_floats() * sizeof(short))));
				} else {
                    glsafe(::glVertexAttribPointer(position_id, buffer.vertices.position_size_floats(), GL_FLOAT, GL_FALSE,
                     buffer.vertices.vertex_size_bytes(), (const void*)buffer.vertices.position_offset_bytes()));
				}
                
                
                glsafe(::glEnableVertexAttribArray(position_id));
            }
            const bool has_normals = buffer.vertices.normal_size_floats() > 0;
            if (has_normals) {
                if (normal_id != -1) {
                    if (m_is_mem_optim) {
                        glsafe(::glVertexAttribPointer(normal_id, buffer.vertices.normal_size_floats(), GL_SHORT, GL_FALSE,
                                                       buffer.vertices.vertex_size_floats() * sizeof(short),
                                                       (const void*) (buffer.vertices.normal_offset_floats() * sizeof(short))));
                    } else {
                        glsafe(::glVertexAttribPointer(normal_id, buffer.vertices.normal_size_floats(), GL_FLOAT, GL_FALSE,
                                                       buffer.vertices.vertex_size_bytes(),
                                                       (const void*) buffer.vertices.normal_offset_bytes()));
                    }
                    
                    glsafe(::glEnableVertexAttribArray(normal_id));
                }
            }

            glsafe(::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, i_buffer.ibo));

            for (auto& range : buffer.model.instances.render_ranges.ranges) {
                const Range range_range = { range.offset, range.offset + range.count };
                if (range_range.intersects(buffer_range)) {
                    shader.set_uniform("uniform_color", range.color);
                    const unsigned int offset = (range_range.first > buffer_range.first) ? range_range.first - buffer_range.first : 0;
                    const size_t offset_bytes = static_cast<size_t>(offset) * indices_per_instance * sizeof(IBufferType);
                    const Range render_range = { std::max(range_range.first, buffer_range.first), std::min(range_range.last, buffer_range.last) };
                    const size_t count = static_cast<size_t>(render_range.last - render_range.first) * indices_per_instance;
                    if (count > 0) {
                        glsafe(::glDrawElements(GL_TRIANGLES, (GLsizei)count, GL_UNSIGNED_SHORT, (const void*)offset_bytes));
#if ENABLE_GCODE_VIEWER_STATISTICS
                        ++m_statistics.gl_batched_models_calls_count;
#endif // ENABLE_GCODE_VIEWER_STATISTICS
                    }
                }
            }

            glsafe(::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0));

            if (normal_id != -1)
                glsafe(::glDisableVertexAttribArray(normal_id));
            if (position_id != -1)
                glsafe(::glDisableVertexAttribArray(position_id));
            glsafe(::glBindBuffer(GL_ARRAY_BUFFER, 0));

            buffer_range.first = buffer_range.last;
        }
    };

    auto line_width = [](double zoom) {
        return (zoom < 5.0) ? 1.0 : (1.0 + 5.0 * (zoom - 5.0) / (100.0 - 5.0));
    };

	double pixes  = estimate_pixels_of_one_layer();
    int    stride = get_dynamic_stride(pixes);

    const unsigned char begin_id = buffer_id(EMoveType::Retract);
    const unsigned char end_id   = buffer_id(EMoveType::Count);

    Transform3d transform = wxGetApp().plater()->get_current_canvas3D()->get_preview_extra_transform();
    for (unsigned char i = begin_id; i < end_id; ++i) {

        if (m_is_lite_mode && !(i == buffer_id(EMoveType::Extrude) || i == buffer_id(EMoveType::Seam) || i == buffer_id(EMoveType::Unretract) || i == buffer_id(EMoveType::Pause_Print)))
            continue;

        TBuffer& buffer = m_buffers[i];
        if (!buffer.visible || !buffer.has_data())
            continue;

        GLShaderProgram* shader = wxGetApp().get_shader(m_is_mem_optim ? ("gcode_" + buffer.shader).c_str() : buffer.shader.c_str());
        if (shader == nullptr)
            continue;

        shader->start_using();
        shader->set_uniform("view_model_matrix", camera.get_view_matrix() * transform);
        shader->set_uniform("projection_matrix", camera.get_projection_matrix());
        shader->set_uniform("view_normal_matrix", (Matrix3d)Matrix3d::Identity());
        if (m_is_mem_optim) {
            shader->set_uniform("position_origin", m_paths_bounding_box.min);
		}
	

        if (buffer.render_primitive_type == TBuffer::ERenderPrimitiveType::InstancedModel) {
            shader->set_uniform("emission_factor", 0.25f);
            render_as_instanced_model(buffer, *shader);
            shader->set_uniform("emission_factor", 0.0f);
        }
        else if (buffer.render_primitive_type == TBuffer::ERenderPrimitiveType::BatchedModel) {
            shader->set_uniform("emission_factor", 0.25f);
            const int position_id = shader->get_attrib_location("v_position");
            const int normal_id   = shader->get_attrib_location("v_normal");
            render_as_batched_model(buffer, *shader, position_id, normal_id);
            shader->set_uniform("emission_factor", 0.0f);
        }
        else {
            const int position_id = shader->get_attrib_location("v_position");
            const int normal_id   = shader->get_attrib_location("v_normal");
            const int uniform_color = shader->get_uniform_location("uniform_color");

            auto it_path = buffer.render_paths.begin();
            for (unsigned int ibuffer_id = 0; ibuffer_id < static_cast<unsigned int>(buffer.indices.size()); ++ibuffer_id) {
                const IBuffer& i_buffer = buffer.indices[ibuffer_id];
                // Skip all paths with ibuffer_id < ibuffer_id.
                for (; it_path != buffer.render_paths.end() && it_path->ibuffer_id < ibuffer_id; ++it_path);
                if (it_path == buffer.render_paths.end() || it_path->ibuffer_id > ibuffer_id)
                    // Not found. This shall not happen.
                    continue;

                glsafe(::glBindBuffer(GL_ARRAY_BUFFER, i_buffer.vbo));
                if (position_id != -1) {
                    if (m_is_mem_optim) {
                        glsafe(::glVertexAttribPointer(position_id, buffer.vertices.position_size_floats(), GL_SHORT, GL_FALSE,
                                                       buffer.vertices.vertex_size_floats() * sizeof(short),
                                                       (const void*) (buffer.vertices.position_offset_floats() * sizeof(short))));
                    } else {
						glsafe(::glVertexAttribPointer(position_id, buffer.vertices.position_size_floats(), GL_FLOAT, GL_FALSE, buffer.vertices.vertex_size_bytes(), (const void*)buffer.vertices.position_offset_bytes()));
                    }
                    
                    glsafe(::glEnableVertexAttribArray(position_id));
                }
                const bool has_normals = buffer.vertices.normal_size_floats() > 0;
                if (has_normals) {
                    if (normal_id != -1) {
                        if (m_is_mem_optim) {
                            glsafe(::glVertexAttribPointer(normal_id, buffer.vertices.normal_size_floats(), GL_SHORT, GL_FALSE,
                                                           buffer.vertices.vertex_size_floats() * sizeof(short),
                                                           (const void*) (buffer.vertices.normal_offset_floats() * sizeof(short))));
                        } else {
                            glsafe(::glVertexAttribPointer(normal_id, buffer.vertices.normal_size_floats(), GL_FLOAT, GL_FALSE,
                                                           buffer.vertices.vertex_size_bytes(),
                                                           (const void*) buffer.vertices.normal_offset_bytes()));
                        }
                        
                        glsafe(::glEnableVertexAttribArray(normal_id));
                    }
                }

                glsafe(::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, i_buffer.ibo));

                // Render all elements with it_path->ibuffer_id == ibuffer_id, possible with varying colors.
                switch (buffer.render_primitive_type)
                {
                case TBuffer::ERenderPrimitiveType::Line: {
                    glsafe(::glLineWidth(static_cast<GLfloat>(line_width(zoom))));
                    render_as_lines(it_path, buffer.render_paths.end(), *shader, uniform_color);
                    break;
                }
                case TBuffer::ERenderPrimitiveType::Triangle: {
                    if (!m_is_lod) {
                        render_as_triangles(it_path, buffer.render_paths.end(), *shader, uniform_color);
                    } else {
                        render_as_triangles_2(it_path, buffer.render_paths.end(), *shader, uniform_color, stride, camera);
                    }
                    break;
                }
                default: { break; }
                }

                glsafe(::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0));

                if (normal_id != -1)
                    glsafe(::glDisableVertexAttribArray(normal_id));
                if (position_id != -1)
                    glsafe(::glDisableVertexAttribArray(position_id));
                glsafe(::glBindBuffer(GL_ARRAY_BUFFER, 0));
            }
        }

        shader->stop_using();
    }

#if ENABLE_GCODE_VIEWER_STATISTICS
    auto render_sequential_range_cap = [this, &camera]
#else
    auto render_sequential_range_cap = [this, &camera]
#endif // ENABLE_GCODE_VIEWER_STATISTICS
    (const SequentialRangeCap& cap) {
        const TBuffer* buffer = cap.buffer;
            GLShaderProgram* shader = wxGetApp().get_shader(m_is_mem_optim ? ("gcode_" + buffer->shader).c_str() :
                                                                         buffer->shader.c_str());
        if (shader == nullptr)
            return;

        shader->start_using();

        shader->set_uniform("view_model_matrix", camera.get_view_matrix());
        shader->set_uniform("projection_matrix", camera.get_projection_matrix());
        shader->set_uniform("view_normal_matrix", (Matrix3d)Matrix3d::Identity());

        const int position_id = shader->get_attrib_location("v_position");
        const int normal_id   = shader->get_attrib_location("v_normal");

        glsafe(::glBindBuffer(GL_ARRAY_BUFFER, cap.vbo));
        if (position_id != -1) {
            if (m_is_mem_optim) {
                glsafe(::glVertexAttribPointer(position_id, buffer->vertices.position_size_floats(), GL_SHORT, GL_FALSE,
                                               buffer->vertices.vertex_size_floats() * sizeof(short),
                                               (const void*) (buffer->vertices.position_offset_floats() * sizeof(short))));
            } else {
                glsafe(::glVertexAttribPointer(position_id, buffer->vertices.position_size_floats(), GL_FLOAT, GL_FALSE,
                                               buffer->vertices.vertex_size_bytes(),
                                               (const void*) buffer->vertices.position_offset_bytes()));
            }
            glsafe(::glEnableVertexAttribArray(position_id));
        }
        const bool has_normals = buffer->vertices.normal_size_floats() > 0;
        if (has_normals) {
            if (normal_id != -1) {
                if (m_is_mem_optim) {
                    glsafe(::glVertexAttribPointer(normal_id, buffer->vertices.normal_size_floats(), GL_SHORT, GL_FALSE,
                                                   buffer->vertices.vertex_size_floats() * sizeof(short),
                                                   (const void*) (buffer->vertices.normal_offset_floats() * sizeof(short))));

                } else {
                    glsafe(::glVertexAttribPointer(normal_id, buffer->vertices.normal_size_floats(), GL_FLOAT, GL_FALSE,
                                                   buffer->vertices.vertex_size_bytes(),
                                                   (const void*) buffer->vertices.normal_offset_bytes()));
                }
                glsafe(::glEnableVertexAttribArray(normal_id));
            }
        }

        shader->set_uniform("uniform_color", cap.color);

        glsafe(::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, cap.ibo));
        glsafe(::glDrawElements(GL_TRIANGLES, (GLsizei)cap.indices_count(), GL_UNSIGNED_SHORT, nullptr));
        glsafe(::glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0));

#if ENABLE_GCODE_VIEWER_STATISTICS
            ++m_statistics.gl_triangles_calls_count;
#endif // ENABLE_GCODE_VIEWER_STATISTICS

        if (normal_id != -1)
            glsafe(::glDisableVertexAttribArray(normal_id));
        if (position_id != -1)
            glsafe(::glDisableVertexAttribArray(position_id));

        glsafe(::glBindBuffer(GL_ARRAY_BUFFER, 0));

        shader->stop_using();
    };

    for (unsigned int i = 0; i < 2; ++i) {
        if (m_sequential_range_caps[i].is_renderable())
            render_sequential_range_cap(m_sequential_range_caps[i]);
    }
}

void GCodeViewer::render_shells(int canvas_width, int canvas_height)
{
    //BBS: add shell previewing logic
    if ((!m_shells.previewing && !m_shells.visible) || m_shells.volumes.empty())
        return;

    GLShaderProgram* shader = wxGetApp().get_shader("gouraud_preview");
    if (shader == nullptr)
        return;

    glsafe(::glDepthMask(GL_FALSE));

    shader->start_using();
    shader->set_uniform("emission_factor", 0.1f);
    Plater* plater = wxGetApp().plater();
    const Camera& camera = plater->get_camera();
    Model& model = plater->model();
    const CustomGCode::Info& info = model.plates_custom_gcodes[model.curr_plate_index];
    const Slic3r::DynamicPrintConfig* config = &wxGetApp().preset_bundle->project_config;
    if (config->has("filament_colour")) 
    {
        std::vector<std::string> filament_colors = (config->option<ConfigOptionStrings>("filament_colour"))->values;
        GLVolumeCollection::apply_custom_gcode(shader, info, filament_colors);
    }

    Transform3d transform = wxGetApp().plater()->get_current_canvas3D()->get_preview_extra_transform();
    m_shells.volumes.render(GUI::ERenderPipelineStage::Normal, GLVolumeCollection::ERenderType::Transparent, false,
                            camera.get_view_matrix() * transform, camera.get_projection_matrix());
    shader->set_uniform("emission_factor", 0.0f);
    shader->stop_using();

    glsafe(::glDepthMask(GL_TRUE));
}

void GCodeViewer::render_all_plates_stats(const std::vector<const GCodeProcessorResult*>& gcode_result_list, bool show /*= true*/) const {
#if AUTO_CONVERT_3MF
    return;
#endif

    if (!show)
        return;
    if(gcode_result_list.size() == 0)
        return;
    for (auto gcode_result : gcode_result_list) {
        if (gcode_result->moves.size() == 0)
            return;
    }
    ImGuiWrapper& imgui = *wxGetApp().imgui();

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0, 10.0 * m_scale));
    ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4(1.0f, 1.0f, 1.0f, 0.6f));
    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.00f, 0.68f, 0.26f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.00f, 0.68f, 0.26f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab, ImVec4(0.42f, 0.42f, 0.42f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabHovered, ImVec4(0.93f, 0.93f, 0.93f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabActive, ImVec4(0.93f, 0.93f, 0.93f, 1.00f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2(340.f * m_scale * imgui.scaled(1.0f / 15.0f), 0));

    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), 0, ImVec2(0.5f, 0.5f));
    ImGui::Begin(_u8L("Statistics of All Plates").c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);
    ImDrawList* draw_list = ImGui::GetWindowDrawList();

    std::vector<float> filament_diameters = gcode_result_list.front()->filament_diameters;
    std::vector<float> filament_densities = gcode_result_list.front()->filament_densities;
    std::vector<ColorRGBA> filament_colors;
    decode_colors(wxGetApp().plater()->get_extruder_colors_from_plater_config(gcode_result_list.back()), filament_colors);

    for (int i = 0; i < filament_colors.size(); i++) { 
        filament_colors[i] = adjust_color_for_rendering(filament_colors[i]);
    }

    bool imperial_units = wxGetApp().app_config->get("use_inches") == "1";
    float window_padding = 4.0f * m_scale;
    const float icon_size = ImGui::GetTextLineHeight() * 0.7;
    std::map<std::string, float> offsets;
    std::map<int, double> model_volume_of_extruders_all_plates; // map<extruder_idx, volume>
    std::map<int, double> flushed_volume_of_extruders_all_plates; // map<extruder_idx, flushed volume>
    std::map<int, double> wipe_tower_volume_of_extruders_all_plates; // map<extruder_idx, flushed volume>
    std::map<int, double> support_volume_of_extruders_all_plates; // map<extruder_idx, flushed volume>
    std::vector<double> model_used_filaments_m_all_plates;
    std::vector<double> model_used_filaments_g_all_plates;
    std::vector<double> flushed_filaments_m_all_plates;
    std::vector<double> flushed_filaments_g_all_plates;
    std::vector<double> wipe_tower_used_filaments_m_all_plates;
    std::vector<double> wipe_tower_used_filaments_g_all_plates;
    std::vector<double> support_used_filaments_m_all_plates;
    std::vector<double> support_used_filaments_g_all_plates;
    float total_time_all_plates = 0.0f;
    float total_cost_all_plates = 0.0f;
    bool show_detailed_statistics_page = false;
    std::map<int, float> plate_time; // map<plate_idx, time_s>
    struct ColumnData {
        enum {
            Model = 1,
            Flushed = 2,
            WipeTower = 4,
            Support = 1 << 3,
        };
    };
    int displayed_columns = 0;
    auto max_width = [](const std::vector<std::string>& items, const std::string& title, float extra_size = 0.0f) {
        float ret = ImGui::CalcTextSize(title.c_str()).x;
        for (const std::string& item : items) {
            ret = std::max(ret, extra_size + ImGui::CalcTextSize(item.c_str()).x);
        }
        return ret;
    };
    auto calculate_offsets = [max_width, window_padding](const std::vector<std::pair<std::string, std::vector<::string>>>& title_columns, float extra_size = 0.0f) {
        const ImGuiStyle& style = ImGui::GetStyle();
        std::vector<float> offsets;
        offsets.push_back(max_width(title_columns[0].second, title_columns[0].first, extra_size) + 3.0f * style.ItemSpacing.x + style.WindowPadding.x);
        for (size_t i = 1; i < title_columns.size() - 1; i++)
            offsets.push_back(offsets.back() + max_width(title_columns[i].second, title_columns[i].first) + style.ItemSpacing.x);
        if (title_columns.back().first == _u8L("Display"))
            offsets.back() = ImGui::GetWindowWidth() - ImGui::CalcTextSize(_u8L("Display").c_str()).x - ImGui::GetFrameHeight() / 2 - 2 * window_padding;

        float average_col_width = ImGui::GetWindowWidth() / static_cast<float>(title_columns.size());
        std::vector<float> ret;
        ret.push_back(0);
        for (size_t i = 1; i < title_columns.size(); i++) {
            ret.push_back(std::max(offsets[i - 1], i * average_col_width));
        }

        return ret;
    };
    auto append_item = [icon_size, &imgui, imperial_units, &window_padding, &draw_list, this](bool draw_icon, const ColorRGBA& color, const std::vector<std::pair<std::string, float>>& columns_offsets)
    {
        if (draw_icon) {
            // render icon
            ImVec2 pos = ImVec2(ImGui::GetCursorScreenPos().x + window_padding * 3, ImGui::GetCursorScreenPos().y);

            draw_list->AddRectFilled({ pos.x + 1.0f * m_scale, pos.y + 3.0f * m_scale }, { pos.x + icon_size - 1.0f * m_scale, pos.y + icon_size + 1.0f * m_scale },
                ImGuiWrapper::to_ImU32(color));
        }

        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(20.0 * m_scale, 6.0 * m_scale));

        // render selectable
        ImGui::Dummy({ 0.0, 0.0 });
        ImGui::SameLine();

        // render column item
        {
            const float dummy_size = draw_icon ? (ImGui::GetStyle().ItemSpacing.x + icon_size) : (window_padding * 3.0f);
            ImGui::SameLine(dummy_size);
            imgui.text(columns_offsets[0].first);

            for (size_t i = 1; i < columns_offsets.size(); i++) {
                ImGui::SameLine(columns_offsets[i].second);
                imgui.text(columns_offsets[i].first);
            }
        }

        ImGui::PopStyleVar(1);
    };
    auto append_headers = [&imgui](const std::vector<std::pair<std::string, float>>& title_offsets) {
        for (size_t i = 0; i < title_offsets.size(); i++) {
            ImGui::SameLine(title_offsets[i].second);
            imgui.bold_text(title_offsets[i].first);
        }
        ImGui::Separator();
    };
    auto get_used_filament_from_volume = [this, imperial_units, &filament_diameters, &filament_densities](double volume, int extruder_id) {
        double koef = imperial_units ? 1.0 / GizmoObjectManipulation::in_to_mm : 0.001;
        std::pair<double, double> ret = { koef * volume / (PI * sqr(0.5 * filament_diameters[extruder_id])),
                                            volume * filament_densities[extruder_id] * 0.001 };
        return ret;
    };

    ImGui::Dummy({ window_padding, window_padding });
    ImGui::SameLine();
    // title and item data
    {
        PartPlateList& plate_list = wxGetApp().plater()->get_partplate_list();
        for (auto plate : plate_list.get_nonempty_plate_list())
        {
            auto plate_print_statistics = plate->get_slice_result()->print_statistics;
            auto plate_extruders = plate->get_extruders(true);
            const bool merge_plate_flush_into_model = should_merge_flush_into_model(plate_print_statistics);
            for (size_t extruder_id : plate_extruders) {
                extruder_id -= 1;
                double model_volume = 0.0;
                if (plate_print_statistics.model_volumes_per_extruder.find(extruder_id) != plate_print_statistics.model_volumes_per_extruder.end())
                    model_volume = plate_print_statistics.model_volumes_per_extruder.at(extruder_id);

                double flushed_volume = 0.0;
                if (plate_print_statistics.flush_per_filament.find(extruder_id) != plate_print_statistics.flush_per_filament.end())
                    flushed_volume = plate_print_statistics.flush_per_filament.at(extruder_id);

                model_volume_of_extruders_all_plates[extruder_id] += model_volume + (merge_plate_flush_into_model ? flushed_volume : 0.0);
                flushed_volume_of_extruders_all_plates[extruder_id] += merge_plate_flush_into_model ? 0.0 : flushed_volume;
                if (plate_print_statistics.wipe_tower_volumes_per_extruder.find(extruder_id) == plate_print_statistics.wipe_tower_volumes_per_extruder.end())
                    wipe_tower_volume_of_extruders_all_plates[extruder_id] += 0;
                else {
                    double wipe_tower_volume = plate_print_statistics.wipe_tower_volumes_per_extruder.at(extruder_id);
                    wipe_tower_volume_of_extruders_all_plates[extruder_id] += wipe_tower_volume;
                }
                if (plate_print_statistics.support_volumes_per_extruder.find(extruder_id) == plate_print_statistics.support_volumes_per_extruder.end())
                    support_volume_of_extruders_all_plates[extruder_id] += 0;
                else {
                    double support_volume = plate_print_statistics.support_volumes_per_extruder.at(extruder_id);
                    support_volume_of_extruders_all_plates[extruder_id] += support_volume;
                }
            }
            const PrintEstimatedStatistics::Mode& plate_time_mode = plate_print_statistics.modes[static_cast<size_t>(m_time_estimate_mode)];
            plate_time[plate->get_index()] = plate_time_mode.time;
            total_time_all_plates += plate_time_mode.time;
            
            Print     *print;
            plate->get_print((PrintBase **) &print, nullptr, nullptr);
            total_cost_all_plates += print->print_statistics().total_cost;
        }
       
        for (auto it = model_volume_of_extruders_all_plates.begin(); it != model_volume_of_extruders_all_plates.end(); it++) {
            auto [model_used_filament_m, model_used_filament_g] = get_used_filament_from_volume(it->second, it->first);
            if (model_used_filament_m != 0.0 || model_used_filament_g != 0.0)
                displayed_columns |= ColumnData::Model;
            model_used_filaments_m_all_plates.push_back(model_used_filament_m);
            model_used_filaments_g_all_plates.push_back(model_used_filament_g);
        }
        for (auto it = flushed_volume_of_extruders_all_plates.begin(); it != flushed_volume_of_extruders_all_plates.end(); it++) {
            auto [flushed_filament_m, flushed_filament_g] = get_used_filament_from_volume(it->second, it->first);
            if (flushed_filament_m != 0.0 || flushed_filament_g != 0.0)
                displayed_columns |= ColumnData::Flushed;
            flushed_filaments_m_all_plates.push_back(flushed_filament_m);
            flushed_filaments_g_all_plates.push_back(flushed_filament_g);
        }
        for (auto it = wipe_tower_volume_of_extruders_all_plates.begin(); it != wipe_tower_volume_of_extruders_all_plates.end(); it++) {
            auto [wipe_tower_filament_m, wipe_tower_filament_g] = get_used_filament_from_volume(it->second, it->first);
            if (wipe_tower_filament_m != 0.0 || wipe_tower_filament_g != 0.0)
                displayed_columns |= ColumnData::WipeTower;
            wipe_tower_used_filaments_m_all_plates.push_back(wipe_tower_filament_m);
            wipe_tower_used_filaments_g_all_plates.push_back(wipe_tower_filament_g);
        }
        for (auto it = support_volume_of_extruders_all_plates.begin(); it != support_volume_of_extruders_all_plates.end(); it++) {
            auto [support_filament_m, support_filament_g] = get_used_filament_from_volume(it->second, it->first);
            if (support_filament_m != 0.0 || support_filament_g != 0.0)
                displayed_columns |= ColumnData::Support;
            support_used_filaments_m_all_plates.push_back(support_filament_m);
            support_used_filaments_g_all_plates.push_back(support_filament_g);
        }

        char buff[64];
        double longest_str = 0.0;
        for (auto i : model_used_filaments_g_all_plates) {
            if (i > longest_str)
                longest_str = i;
        }
        ::sprintf(buff, "%.2f", longest_str);

        std::vector<std::pair<std::string, std::vector<::string>>> title_columns;
        if (displayed_columns & ColumnData::Model) {
            title_columns.push_back({ _u8L("Filament"), {""} });
            title_columns.push_back({ _u8L("Model"), {buff} });
        }
        if (displayed_columns & ColumnData::Support) {
            title_columns.push_back({ _u8L("Support"), {buff} });
        }
        if (displayed_columns & ColumnData::Flushed) {
            title_columns.push_back({ _u8L("Flushed"), {buff} });
        }
        if (displayed_columns & ColumnData::WipeTower) {
            title_columns.push_back({ _u8L("Tower"), {buff} });
        }
        if ((displayed_columns & ~ColumnData::Model) > 0) {
            title_columns.push_back({ _u8L("Total"), {buff} });
        }
        auto offsets_ = calculate_offsets(title_columns, icon_size);
        std::vector<std::pair<std::string, float>> title_offsets;
        for (int i = 0; i < offsets_.size(); i++) {
            title_offsets.push_back({ title_columns[i].first, offsets_[i] });
            offsets[title_columns[i].first] = offsets_[i];
        }
        append_headers(title_offsets);
    }

    // item
    {
        size_t i = 0;
        for (auto it = model_volume_of_extruders_all_plates.begin(); it != model_volume_of_extruders_all_plates.end(); it++) {
            if (i < model_used_filaments_m_all_plates.size() && i < model_used_filaments_g_all_plates.size()) {
                std::vector<std::pair<std::string, float>> columns_offsets;
                columns_offsets.push_back({ std::to_string(it->first + 1), offsets[_u8L("Filament")]});

                char buf[64];
                double unit_conver = imperial_units ? GizmoObjectManipulation::oz_to_g : 1.0;

                float column_sum_m = 0.0f;
                float column_sum_g = 0.0f;
                if (displayed_columns & ColumnData::Model) {
                    if ((displayed_columns & ~ColumnData::Model) > 0)
                        ::sprintf(buf, imperial_units ? "%.2f in\n%.2f oz" : "%.2f m\n%.2f g", model_used_filaments_m_all_plates[i], model_used_filaments_g_all_plates[i] / unit_conver);
                    else
                        ::sprintf(buf, imperial_units ? "%.2f in    %.2f oz" : "%.2f m    %.2f g", model_used_filaments_m_all_plates[i], model_used_filaments_g_all_plates[i] / unit_conver);
                    columns_offsets.push_back({ buf, offsets[_u8L("Model")] });
                    column_sum_m += model_used_filaments_m_all_plates[i];
                    column_sum_g += model_used_filaments_g_all_plates[i];
                }
                if (displayed_columns & ColumnData::Support) {
                    ::sprintf(buf, imperial_units ? "%.2f in\n%.2f oz" : "%.2f m\n%.2f g", support_used_filaments_m_all_plates[i], support_used_filaments_g_all_plates[i] / unit_conver);
                    columns_offsets.push_back({ buf, offsets[_u8L("Support")] });
                    column_sum_m += support_used_filaments_m_all_plates[i];
                    column_sum_g += support_used_filaments_g_all_plates[i];
                }
                if (displayed_columns & ColumnData::Flushed) {
                    ::sprintf(buf, imperial_units ? "%.2f in\n%.2f oz" : "%.2f m\n%.2f g", flushed_filaments_m_all_plates[i], flushed_filaments_g_all_plates[i] / unit_conver);
                    columns_offsets.push_back({ buf, offsets[_u8L("Flushed")] });
                    column_sum_m += flushed_filaments_m_all_plates[i];
                    column_sum_g += flushed_filaments_g_all_plates[i];
                }
                if (displayed_columns & ColumnData::WipeTower) {
                    ::sprintf(buf, imperial_units ? "%.2f in\n%.2f oz" : "%.2f m\n%.2f g", wipe_tower_used_filaments_m_all_plates[i], wipe_tower_used_filaments_g_all_plates[i] / unit_conver);
                    columns_offsets.push_back({ buf, offsets[_u8L("Tower")] });
                    column_sum_m += wipe_tower_used_filaments_m_all_plates[i];
                    column_sum_g += wipe_tower_used_filaments_g_all_plates[i];
                }
                if ((displayed_columns & ~ColumnData::Model) > 0) {
                    ::sprintf(buf, imperial_units ? "%.2f in\n%.2f oz" : "%.2f m\n%.2f g", column_sum_m, column_sum_g / unit_conver);
                    columns_offsets.push_back({ buf, offsets[_u8L("Total")] });
                }

                append_item(true, filament_colors[it->first], columns_offsets);
            }
            i++;
        }

        if (!model_used_filaments_m_all_plates.empty()) {
            const double unit_conver = imperial_units ? GizmoObjectManipulation::oz_to_g : 1.0;

            double total_model_used_filament_m = 0.0;
            double total_model_used_filament_g = 0.0;
            double total_support_used_filament_m = 0.0;
            double total_support_used_filament_g = 0.0;
            double total_flushed_filament_m = 0.0;
            double total_flushed_filament_g = 0.0;
            double total_wipe_tower_used_filament_m = 0.0;
            double total_wipe_tower_used_filament_g = 0.0;

            for (double v : model_used_filaments_m_all_plates) total_model_used_filament_m += v;
            for (double v : model_used_filaments_g_all_plates) total_model_used_filament_g += v;
            for (double v : support_used_filaments_m_all_plates) total_support_used_filament_m += v;
            for (double v : support_used_filaments_g_all_plates) total_support_used_filament_g += v;
            for (double v : flushed_filaments_m_all_plates) total_flushed_filament_m += v;
            for (double v : flushed_filaments_g_all_plates) total_flushed_filament_g += v;
            for (double v : wipe_tower_used_filaments_m_all_plates) total_wipe_tower_used_filament_m += v;
            for (double v : wipe_tower_used_filaments_g_all_plates) total_wipe_tower_used_filament_g += v;

            ImGui::Separator();

            std::vector<std::pair<std::string, float>> columns_offsets;
            columns_offsets.push_back({ _u8L("Total"), offsets[_u8L("Filament")] });

            char total_buf[64];
            if (displayed_columns & ColumnData::Model) {
                if ((displayed_columns & ~ColumnData::Model) > 0)
                    ::sprintf(total_buf, imperial_units ? "%.2f in\n%.2f oz" : "%.2f m\n%.2f g", total_model_used_filament_m, total_model_used_filament_g / unit_conver);
                else
                    ::sprintf(total_buf, imperial_units ? "%.2f in    %.2f oz" : "%.2f m    %.2f g", total_model_used_filament_m, total_model_used_filament_g / unit_conver);
                columns_offsets.push_back({ total_buf, offsets[_u8L("Model")] });
            }
            if (displayed_columns & ColumnData::Support) {
                ::sprintf(total_buf, imperial_units ? "%.2f in\n%.2f oz" : "%.2f m\n%.2f g", total_support_used_filament_m, total_support_used_filament_g / unit_conver);
                columns_offsets.push_back({ total_buf, offsets[_u8L("Support")] });
            }
            if (displayed_columns & ColumnData::Flushed) {
                ::sprintf(total_buf, imperial_units ? "%.2f in\n%.2f oz" : "%.2f m\n%.2f g", total_flushed_filament_m, total_flushed_filament_g / unit_conver);
                columns_offsets.push_back({ total_buf, offsets[_u8L("Flushed")] });
            }
            if (displayed_columns & ColumnData::WipeTower) {
                ::sprintf(total_buf, imperial_units ? "%.2f in\n%.2f oz" : "%.2f m\n%.2f g", total_wipe_tower_used_filament_m, total_wipe_tower_used_filament_g / unit_conver);
                columns_offsets.push_back({ total_buf, offsets[_u8L("Tower")] });
            }
            if ((displayed_columns & ~ColumnData::Model) > 0) {
                ::sprintf(total_buf, imperial_units ? "%.2f in\n%.2f oz" : "%.2f m\n%.2f g",
                    total_model_used_filament_m + total_support_used_filament_m + total_flushed_filament_m + total_wipe_tower_used_filament_m,
                    (total_model_used_filament_g + total_support_used_filament_g + total_flushed_filament_g + total_wipe_tower_used_filament_g) / unit_conver);
                columns_offsets.push_back({ total_buf, offsets[_u8L("Total")] });
            }

            append_item(false, { 0.0f, 0.0f, 0.0f, 0.0f }, columns_offsets);
        }

        ImGui::Dummy(ImVec2(0.0f, ImGui::GetFontSize() * 0.1));
        ImGui::Dummy({ window_padding, window_padding });
        ImGui::SameLine();
        imgui.text(_u8L("Total cost") + ":");
        ImGui::SameLine();
        char buf[64];
        ::sprintf(buf, "%.2f", total_cost_all_plates);
        imgui.text(buf);

        if (!plate_time.empty()) {
            ImGui::Dummy(ImVec2(0.0f, ImGui::GetFontSize() * 0.1));
            ImGui::Dummy({ window_padding, window_padding });
            ImGui::SameLine();
            imgui.title(_u8L("Time Estimation"));

            std::vector<std::pair<std::string, std::string>> rows;
            rows.reserve(plate_time.size() + 1);
            for (const auto& it : plate_time) {
                rows.push_back({ _u8L("Plate") + " " + std::to_string(it.first + 1), short_time(get_time_dhms(it.second)) });
            }
            if (plate_time.size() > 1) {
                rows.push_back({ _u8L("Total"), short_time(get_time_dhms(total_time_all_plates)) });
            }

            float x_value = window_padding * 3.0f;
            const std::string value_column_keys[] = { _u8L("Model"), _u8L("Support"), _u8L("Flushed"), _u8L("Tower"), _u8L("Total") };
            for (const auto& key : value_column_keys) {
                auto it = offsets.find(key);
                if (it != offsets.end()) {
                    x_value = it->second;
                    break;
                }
            }

            const size_t total_row_idx = plate_time.size();
            for (size_t i = 0; i < rows.size(); i++) {
                if (plate_time.size() > 1 && i == total_row_idx) {
                    ImGui::Separator();
                }

                std::vector<std::pair<std::string, float>> columns_offsets;
                columns_offsets.reserve(2);
                columns_offsets.push_back({ rows[i].first, 0.0f });
                columns_offsets.push_back({ rows[i].second, x_value });
                append_item(false, { 0.0f, 0.0f, 0.0f, 0.0f }, columns_offsets);
            }
        }
    }
    ImGui::End();
    ImGui::PopStyleColor(6);
    ImGui::PopStyleVar(3);
    return;
}

class GcodeHelper
{
    DispConfig config;ImGuiWrapper* imgui;
    const std::vector<unsigned char>& m_extruder_ids;
    const PrintEstimatedStatistics& m_print_statistics;
    const PrintEstimatedStatistics::Mode& time_mode;
    const std::vector<float>& m_filament_diameters;
    const std::vector<float>& m_filament_densities;
    const std::vector<ExtrusionRole>& m_roles;
    const std::vector<EMoveType>& options_items;
    GCodeViewer::Extrusions& m_extrusions;
    std::vector<GCodeViewer::TBuffer>& m_buffers;
    const PrintStatistics& ps;
    bool imperial_units;char buf[512];
    float unit_conver,koef, icon_size, m_scale, window_padding;

public:
    enum EItemType
    {
        Rect,
        Circle,
        Hexagon,
        Line,
        None
    };
    enum ColumnData {
        Model = 1,
        Flushed = 2,
        WipeTower = 4,
        Support = 1 << 3,
    };

    GcodeHelper(
        float scal,int md, 
        std::vector<EMoveType>&em,
        const std::vector<ExtrusionRole>&ro,
        std::vector<GCodeViewer::TBuffer>&bf,
        GCodeViewer::Extrusions& ex,
        const PrintEstimatedStatistics& st,
        const std::vector<unsigned char>&ids,
        const std::vector<float>& di, 
        const std::vector<float>&de)
        :m_filament_diameters(di), m_filament_densities(de), m_scale(scal)
        ,m_print_statistics(st),time_mode(st.modes[md]),m_extruder_ids(ids)
        , m_roles(ro), m_buffers(bf) , m_extrusions(ex), options_items(em)
        ,ps(wxGetApp().plater()->get_partplate_list().get_current_fff_print().print_statistics()){
             imgui = wxGetApp().imgui();
             imperial_units = wxGetApp().app_config->get("use_inches") == "1";
             config = DispConfig();
             window_padding = 10 * m_scale;
             icon_size = ImGui::GetTextLineHeight();
             unit_conver = imperial_units ? GizmoObjectManipulation::oz_to_g : 1;
             koef = imperial_units ? GizmoObjectManipulation::in_to_mm : 1000.0;
    }

    DispConfig::WindowConfig prepare(bool fold, float contentWidth) {
        std::string btn_name = fold ? ">>" : "<<";
        auto wsz = config.getWindowSize(DispConfig::e_wt_gcode, wxGetApp().plater()->get_current_canvas3D()->get_scale());

        GLCanvas3D* canvas = wxGetApp().plater()->get_current_canvas3D();

        ImVec2 pos = canvas->get_printer_objects_panel_pos();
        ImVec2 size = canvas->get_printer_objects_panel_size();

        ImVec2 bias = ImVec2(pos.x + 1.0f * m_scale, pos.y + size.y + 8);

        ImGui::SetNextWindowPos(bias, ImGuiCond_Always);

        float min_width = std::max(wsz.x, 280.0f * m_scale);
        if (fold || contentWidth < (wsz.x - 2 * window_padding))
        {
            ImGui::SetNextWindowSize(ImVec2(min_width, wsz.y));
        }
        else
        {         
            float target_width = std::max(contentWidth, 300.0f);
            const float max_window_width = std::max(min_width, ImGui::GetIO().DisplaySize.x - bias.x - 2.0f * m_scale);
            target_width = std::min(target_width, max_window_width);
            auto size = ImVec2(target_width, -1.0f);
            ImGui::SetNextWindowSize(size);
        }

        DispConfig::WindowConfig wcfg;
        wcfg.padding = { window_padding,window_padding };
        wcfg.bgalpha = 0.2;
        wcfg.txt = DispConfig::e_ct_white;
        return wcfg;
    }

    void initColorData() {

        // get used filament (meters and grams) from used volume in respect to the active extruder
        auto get_used_filament_from_volume = [&](double volume, int extruder_id) {
            double koef = imperial_units ? 1.0 / GizmoObjectManipulation::in_to_mm : 0.001;
            std::pair<double, double> ret = { koef * volume / (PI * sqr(0.5 * m_filament_diameters[extruder_id])),
                                                volume * m_filament_densities[extruder_id] * 0.001 };
            return ret;
        };

        for (size_t extruder_id : m_extruder_ids) {
            if (m_print_statistics.model_volumes_per_extruder.find(extruder_id) == m_print_statistics.model_volumes_per_extruder.end()) {
                model_used_filaments_m.push_back(0.0);
                model_used_filaments_g.push_back(0.0);
            }
            else {
                double volume = m_print_statistics.model_volumes_per_extruder.at(extruder_id);
                auto [model_used_filament_m, model_used_filament_g] = get_used_filament_from_volume(volume, extruder_id);
                model_used_filaments_m.push_back(model_used_filament_m);
                model_used_filaments_g.push_back(model_used_filament_g);
                total_model_used_filament_m += model_used_filament_m;
                total_model_used_filament_g += model_used_filament_g;
                displayed_columns |= ColumnData::Model;
            }
        }

        for (size_t extruder_id : m_extruder_ids) {
            if (m_print_statistics.wipe_tower_volumes_per_extruder.find(extruder_id) == m_print_statistics.wipe_tower_volumes_per_extruder.end()) {
                wipe_tower_used_filaments_m.push_back(0.0);
                wipe_tower_used_filaments_g.push_back(0.0);
            }
            else {
                double volume = m_print_statistics.wipe_tower_volumes_per_extruder.at(extruder_id);
                auto [wipe_tower_used_filament_m, wipe_tower_used_filament_g] = get_used_filament_from_volume(volume, extruder_id);
                wipe_tower_used_filaments_m.push_back(wipe_tower_used_filament_m);
                wipe_tower_used_filaments_g.push_back(wipe_tower_used_filament_g);
                total_wipe_tower_used_filament_m += wipe_tower_used_filament_m;
                total_wipe_tower_used_filament_g += wipe_tower_used_filament_g;
                displayed_columns |= ColumnData::WipeTower;
            }
        }

        for (size_t extruder_id : m_extruder_ids) {
            if (m_print_statistics.flush_per_filament.find(extruder_id) == m_print_statistics.flush_per_filament.end()) {
                flushed_filaments_m.push_back(0.0);
                flushed_filaments_g.push_back(0.0);
            }
            else {
                double volume = m_print_statistics.flush_per_filament.at(extruder_id);
                auto [flushed_filament_m, flushed_filament_g] = get_used_filament_from_volume(volume, extruder_id);
                flushed_filaments_m.push_back(flushed_filament_m);
                flushed_filaments_g.push_back(flushed_filament_g);
                total_flushed_filament_m += flushed_filament_m;
                total_flushed_filament_g += flushed_filament_g;
                displayed_columns |= ColumnData::Flushed;
            }
        }

        for (size_t extruder_id : m_extruder_ids) {
            if (m_print_statistics.support_volumes_per_extruder.find(extruder_id) == m_print_statistics.support_volumes_per_extruder.end()) {
                support_used_filaments_m.push_back(0.0);
                support_used_filaments_g.push_back(0.0);
            }
            else {
                double volume = m_print_statistics.support_volumes_per_extruder.at(extruder_id);
                auto [used_filament_m, used_filament_g] = get_used_filament_from_volume(volume, extruder_id);
                support_used_filaments_m.push_back(used_filament_m);
                support_used_filaments_g.push_back(used_filament_g);
                total_support_used_filament_m += used_filament_m;
                total_support_used_filament_g += used_filament_g;
                displayed_columns |= ColumnData::Support;
            }
        }
    }
    
    bool showTitle(bool fold) {

        float view_scale = wxGetApp().plater()->get_current_canvas3D()->get_scale();

        std::string btn_name = fold ? ">>" : "<<";
        auto btnsz = ImGui::CalcTextSize(btn_name.c_str());

        float title_y = ImGui::GetCursorPosY();
        config.boldText(_u8L("G-code Preview"), 1.2);
        ImGui::SameLine();

        float arrow_size = 22.0f * view_scale * 1.4f;
        ImVec2 btn_size{arrow_size, arrow_size};
        float   btn_y_offset = fold ? 2.0f * view_scale : -2.0f * view_scale;

        float right       = ImGui::GetWindowWidth() - ImGui::GetStyle().WindowPadding.x;
        float btn_start_x = right - btn_size.x;
        float baseline_y  = title_y + (ImGui::GetTextLineHeight() - btn_size.y) * 0.5f + btn_y_offset;

        ImVec2 btn_pos_local = ImVec2(btn_start_x, baseline_y);
        ImGui::SetCursorPos(btn_pos_local);

        ImVec2 btn_pos = ImGui::GetCursorScreenPos();
        bool pressed = false;

        ImTextureID collapse_id;
        ImVec2 collapse_uv0, collapse_uv1;
        if (wxGetApp().obj_list()->get_collapse_icon(collapse_id, collapse_uv0, collapse_uv1, fold)) {
            pressed = ImGui::InvisibleButton("##gcode_fold_btn", btn_size);
            ImDrawList* dl = ImGui::GetWindowDrawList();
            ImVec2 p0 = btn_pos;
            ImVec2 p1 = ImVec2(btn_pos.x + btn_size.x, btn_pos.y);
            ImVec2 p2 = ImVec2(btn_pos.x + btn_size.x, btn_pos.y + btn_size.y);
            ImVec2 p3 = ImVec2(btn_pos.x, btn_pos.y + btn_size.y);
            // rotate 90 deg clockwise: uv_bl, uv_tl, uv_tr, uv_br
            ImVec2 uv_tl = collapse_uv0;
            ImVec2 uv_tr = ImVec2(collapse_uv1.x, collapse_uv0.y);
            ImVec2 uv_br = collapse_uv1;
            ImVec2 uv_bl = ImVec2(collapse_uv0.x, collapse_uv1.y);
            dl->AddImageQuad(collapse_id, p0, p1, p2, p3, uv_bl, uv_tl, uv_tr, uv_br);
        } else {
            bool btn = ImGui::InvisibleButton("##gcode_fold_btn", btn_size);
            ImDrawList* dl = ImGui::GetWindowDrawList();
            ImVec2 center = ImVec2(btn_pos.x + btn_size.x * 0.5f, btn_pos.y + btn_size.y * 0.5f);
            float r = btn_size.y * 0.32f;
            ImVec2 dir = fold ? ImVec2(1, 0) : ImVec2(-1, 0);
            ImVec2 perp = ImVec2(-dir.y, dir.x);
            ImVec2 dir_r  = ImVec2(dir.x * r, dir.y * r);
            ImVec2 perp_r = ImVec2(perp.x * r * 0.8f, perp.y * r * 0.8f);
            ImVec2 p1 = ImVec2(center.x + dir_r.x, center.y + dir_r.y);
            ImVec2 p2 = ImVec2(center.x - dir_r.x + perp_r.x, center.y - dir_r.y + perp_r.y);
            ImVec2 p3 = ImVec2(center.x - dir_r.x - perp_r.x, center.y - dir_r.y - perp_r.y);
            ImU32 col = ImGui::GetColorU32(ImGuiCol_Text);
            dl->AddTriangleFilled(p1, p2, p3, col);
            pressed = btn;
        }

        if (pressed) {
            fold = !fold;
            wxGetApp().imgui()->set_requires_extra_frame();
        }
        if (!fold)
        {
//             ImGui::Dummy(ImVec2(0, 5));
//             auto printer_name = Slic3r::GUI::wxGetApp().preset_bundle->printers.get_selected_preset_name();
//             ::sprintf(buf, "%s:%s", _u8L("Current Machine").c_str(), (char*)printer_name.c_str());
//             ImGui::Text(buf);

            ImGui::Dummy(ImVec2(0, 3));
            auto timestr = short_time(get_time_dhms(time_mode.time));
            ::sprintf(buf, "%s:%s", _u8L("Printing Time").c_str(), (char*)timestr.c_str());
            ImGui::Text("%s", buf);

            ImGui::SameLine(160 * view_scale);
            ::sprintf(buf, imperial_units ? " %s:%.2f oz" : " %s:%.2f g", _u8L("Material Wt").c_str(), ps.total_weight / unit_conver);
            imgui->text(buf);

            ImGui::Dummy(ImVec2(0, 3));
            ::sprintf(buf, imperial_units ? "%s:%.2f in" : "%s:%.2f m", _u8L("Material Length").c_str(), ps.total_used_filament / koef);
            imgui->text(buf);

            ImGui::SameLine(160 * view_scale);
            ::sprintf(buf, "%s:%.2f", _u8L("Material Cost").c_str(), ps.total_cost);
            imgui->text(buf);
            ImGui::Dummy(ImVec2(0, 5));
            if (!s_preview_section_fold)
                ImGui::Separator();
        } else if (fold) {
            ImGui::Dummy(ImVec2(0, 4));
        }
        return fold;
    }

    bool showOption(bool& showmark, bool& showbed, bool& showcolor) {

        float view_scale = wxGetApp().plater()->get_current_canvas3D()->get_scale();

        ImGui::Dummy(ImVec2(0, 5));
        if (s_preview_section_fold) {
            std::string folded_title = _u8L("Details");
            ImVec2 sz = ImGui::CalcTextSize(folded_title.c_str());
            ImGui::PushStyleColor(ImGuiCol_Text, ImGuiWrapper::COL_CREALITY);
            ImVec4 transparent(0, 0, 0, 0);
            ImGui::PushStyleColor(ImGuiCol_Header, transparent);
            ImGui::PushStyleColor(ImGuiCol_HeaderHovered, transparent);
            ImGui::PushStyleColor(ImGuiCol_HeaderActive, transparent);
            if (ImGui::Selectable(folded_title.c_str(), false, 0, sz)) {
                s_preview_section_fold = false;
                wxGetApp().imgui()->set_requires_extra_frame();
            }
            ImGui::PopStyleColor(3);
            ImRect txt_rect = ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
            ImGui::GetWindowDrawList()->AddLine(ImVec2(txt_rect.Min.x, txt_rect.Max.y - 1.0f),
                                                ImVec2(txt_rect.Max.x, txt_rect.Max.y - 1.0f),
                                                ImGui::GetColorU32(ImGuiCol_Text), 1.0f);
            ImGui::PopStyleColor();
            ImGui::Dummy(ImVec2(0, 5));
            return true;
        }

        std::string title       = _u8L("Show in Preview");
        std::string toggle_txt  = _u8L("Collapse");
        ImVec2       title_size = ImGui::CalcTextSize(title.c_str());
        ImVec2       toggle_sz  = ImGui::CalcTextSize(toggle_txt.c_str());
        float        line_y     = ImGui::GetCursorPosY();

        config.boldText(title, 1.2);
        ImGui::SameLine();
        ImGui::SetCursorPos(ImVec2(ImGui::GetWindowWidth() - ImGui::GetStyle().WindowPadding.x - toggle_sz.x, line_y));
        ImVec4 transparent(0, 0, 0, 0);
        ImGui::PushStyleColor(ImGuiCol_Header, transparent);
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, transparent);
        ImGui::PushStyleColor(ImGuiCol_HeaderActive, transparent);
        if (ImGui::Selectable(toggle_txt.c_str(), false, 0, toggle_sz)) {
            s_preview_section_fold = true;
            wxGetApp().imgui()->set_requires_extra_frame();
        }
        ImGui::PopStyleColor(3);
        ImRect txt_rect = ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
        ImGui::GetWindowDrawList()->AddLine(ImVec2(txt_rect.Min.x, txt_rect.Max.y - 1.0f), ImVec2(txt_rect.Max.x, txt_rect.Max.y - 1.0f),
                                            ImGui::GetColorU32(ImGuiCol_Text), 1.0f);
        DispConfig::ButtonConfig cfg;
        cfg.border = 1;

        ImGui::Dummy(ImVec2(0, 5));
        if (s_preview_section_fold) {
            return true;
        }

        ImGuiWindow* window            = ImGui::GetCurrentWindow();
        float        prev_font_scale   = window->FontWindowScale;
        float        enlarged_font     = prev_font_scale * 1.1f;
        ImVec2       enlarged_padding  = ImVec2(4.0f * view_scale, 3.0f * view_scale);
        ImGui::SetWindowFontScale(enlarged_font);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, enlarged_padding);
        showbed = config.checkBox(_u8L("Print Platform"), showbed, cfg);
        ImGui::SameLine();
        showmark = config.checkBox(_u8L("Nozzle"), showmark, cfg);
        ImGui::PopStyleVar(1);
        ImGui::SetWindowFontScale(prev_font_scale);

        // capsule toggle: Color Show (left) / G-code (right)
        ImGui::Dummy(ImVec2(0.0f, 4.0f * view_scale));
        ImVec2 pill_pos   = ImGui::GetCursorScreenPos();
        float  pill_h     = 30.0f * view_scale;
        float  pill_w     = 200.0f * view_scale;
        float  rounding   = pill_h * 0.5f;
        ImVec2 pill_size  = {pill_w, pill_h};
        ImVec2 left_size  = {pill_w * 0.5f, pill_h};
        ImVec2 right_size = left_size;

        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        bool   is_dark        = wxGetApp().dark_mode();
        ImVec4 inactive_bg    = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
        ImVec4 inactive_border= is_dark ? ImVec4(0.30f, 0.33f, 0.36f, 1.0f) : ImVec4(0.82f, 0.86f, 0.90f, 1.0f);
        ImVec4 active_bg      = ImGuiWrapper::COL_CREALITY;
        ImVec4 active_text    = ImVec4(1, 1, 1, 1);
        ImVec4 inactive_text  = is_dark ? ImVec4(0.78f, 0.80f, 0.82f, 1.0f) : ImVec4(0.36f, 0.36f, 0.40f, 1.0f);

        // pill border
        draw_list->AddRectFilled(pill_pos, ImVec2(pill_pos.x + pill_w, pill_pos.y + pill_h),
                                 ImGui::ColorConvertFloat4ToU32(inactive_bg), rounding);
        draw_list->AddRect(pill_pos, ImVec2(pill_pos.x + pill_w, pill_pos.y + pill_h),
                           ImGui::ColorConvertFloat4ToU32(inactive_border), rounding, 0, 1.0f);

        // left (Color Show)
        bool left_active  = showcolor;
        ImVec2 left_min   = pill_pos;
        ImVec2 left_max   = ImVec2(pill_pos.x + left_size.x, pill_pos.y + left_size.y);
        if (left_active) {
            draw_list->AddRectFilled(left_min, left_max, ImGui::ColorConvertFloat4ToU32(active_bg), rounding,
                                     ImDrawFlags_RoundCornersLeft);
        }
        const float text_pad = 8.0f * view_scale;
        std::string left_full_txt = _u8L("Color Show");
        std::string left_draw_txt = left_full_txt;
        bool left_truncated = false;
        bool bold_pushed = wxGetApp().imgui()->push_bold_font();
        {
            const float max_text_w = left_size.x - 1.0f * text_pad;
            if (max_text_w > 0.0f) {
                left_draw_txt  = ImGuiWrapper::trunc(left_full_txt, max_text_w, "...");
                left_truncated = (left_draw_txt != left_full_txt);
            } else {
                left_draw_txt  = "...";
                left_truncated = true;
            }
            ImVec2 left_txt_sz  = ImGui::CalcTextSize(left_draw_txt.c_str());
            ImVec2 left_txt_pos = ImVec2(left_min.x + (left_size.x - left_txt_sz.x) * 0.5f,
                                         left_min.y + (left_size.y - left_txt_sz.y) * 0.5f);
            draw_list->PushClipRect(left_min, left_max, true);
            draw_list->AddText(left_txt_pos, ImGui::ColorConvertFloat4ToU32(left_active ? active_text : inactive_text), left_draw_txt.c_str());
            draw_list->PopClipRect();
        }
        if (bold_pushed)
            wxGetApp().imgui()->pop_bold_font();

        // right (G-code)
        bool right_active = !showcolor;
        ImVec2 right_min  = ImVec2(pill_pos.x + left_size.x, pill_pos.y);
        ImVec2 right_max  = ImVec2(pill_pos.x + left_size.x + right_size.x, pill_pos.y + right_size.y);
        if (right_active) {
            draw_list->AddRectFilled(right_min, right_max, ImGui::ColorConvertFloat4ToU32(active_bg), rounding,
                                     ImDrawFlags_RoundCornersRight);
        }
        std::string right_txt = _u8L("G-Code");
        ImVec2 right_txt_sz   = ImGui::CalcTextSize(right_txt.c_str());
        ImVec2 right_txt_pos  = ImVec2(right_min.x + (right_size.x - right_txt_sz.x) * 0.5f,
                                       right_min.y + (right_size.y - right_txt_sz.y) * 0.5f);
        bold_pushed = wxGetApp().imgui()->push_bold_font();
        draw_list->AddText(right_txt_pos, ImGui::ColorConvertFloat4ToU32(right_active ? active_text : inactive_text), right_txt.c_str());
        if (bold_pushed)
            wxGetApp().imgui()->pop_bold_font();

        ImGui::SetCursorScreenPos(left_min);
        ImGui::InvisibleButton("##color_show", left_size);
        if (ImGui::IsItemClicked() && !showcolor) {
            showcolor = true;
            wxGetApp().imgui()->set_requires_extra_frame();
        }
        if (ImGui::IsItemHovered() && left_truncated) {
            ImGui::PushStyleColor(ImGuiCol_PopupBg, ImGuiWrapper::COL_WINDOW_BACKGROUND);
            ImGui::PushStyleColor(ImGuiCol_Border, {0, 0, 0, 0});
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.00f, 1.00f, 1.00f, 1.00f));
            ImGui::SetTooltip("%s", left_full_txt.c_str());
            ImGui::PopStyleColor(3);
        }
        ImGui::SetCursorScreenPos(right_min);
        ImGui::InvisibleButton("##gcode_toggle", right_size);
        if (ImGui::IsItemClicked() && showcolor) {
            showcolor = false;
            wxGetApp().imgui()->set_requires_extra_frame();
        }

        ImGui::SetCursorScreenPos(ImVec2(pill_pos.x, pill_pos.y + pill_h));

        return false;
    }

    void showColorHeader( GCodeViewer::EViewType m_view_type,float& contentWidth) {
        initColorData();
        ImGui::Dummy({ window_padding, window_padding });
        ImGui::SameLine();
        switch (m_view_type)
        {
        case GCodeViewer::EViewType::FeatureType:
        {
            char buffer[64];
            const bool single_color_flush = (m_extruder_ids.size() == 1) && (total_flushed_filament_m > 0.0 || total_flushed_filament_g > 0.0);
            bool       custom_role_merged = false;
            for (size_t i = 0; i < m_roles.size(); ++i) {
                ExtrusionRole role = m_roles[i];
                if (role < erCount) {
                    labels.push_back(_u8L(ExtrusionEntity::role_to_string(role)));
                    auto [time, percent] = role_time_and_percent(role);
                    auto [model_used_filament_m, model_used_filament_g] = used_filament_per_role(role);
                    if (single_color_flush && role == erCustom) {
                        time += time_mode.flush_time;
                        percent = (time_mode.time > 0.0f) ? time / time_mode.time : 0.0f;
                        model_used_filament_m += total_flushed_filament_m;
                        model_used_filament_g += total_flushed_filament_g;
                        custom_role_merged = true;
                    }
                    times.push_back((time > 0.0f) ? short_time(get_time_dhms(time)) : "");
                    if (percent == 0)
                        ::sprintf(buffer, "0%%");
                    else
                        percent > 0.001 ? ::sprintf(buffer, "%.1f%%", percent * 100) : ::sprintf(buffer, "<0.1%%");
                    percents.push_back(buffer);
                    ::sprintf(buffer, imperial_units ? "%.2f in" : "%.2f m", model_used_filament_m);
                    used_filaments_length.push_back(buffer);
                    ::sprintf(buffer, imperial_units ? "%.2f oz" : "%.2f g", model_used_filament_g);
                    used_filaments_weight.push_back(buffer);
                }
            }
            if (single_color_flush && !custom_role_merged) {
                labels.push_back(_u8L(ExtrusionEntity::role_to_string(erCustom)));
                times.push_back(short_time(get_time_dhms(time_mode.flush_time)));
                if (time_mode.time > 0.0f) {
                    float percent = time_mode.flush_time / time_mode.time;
                    percent > 0.001 ? ::sprintf(buffer, "%.1f%%", percent * 100) : ::sprintf(buffer, "<0.1%%");
                } else {
                    ::sprintf(buffer, "0%%");
                }
                percents.push_back(buffer);
                ::sprintf(buffer, imperial_units ? "%.2f in" : "%.2f m", total_flushed_filament_m);
                used_filaments_length.push_back(buffer);
                ::sprintf(buffer, imperial_units ? "%.2f oz" : "%.2f g", total_flushed_filament_g);
                used_filaments_weight.push_back(buffer);
            }

            auto [time, percent] = move_time_and_percent(EMoveType::Travel);
            travel_time = (time > 0.0f) ? short_time(get_time_dhms(time)) : "";
            if (percent == 0)
                ::sprintf(buffer, "0%%");
            else
                percent > 0.001 ? ::sprintf(buffer, "%.1f%%", percent * 100) : ::sprintf(buffer, "<0.1%%");
            travel_percent = buffer;

            // Collect flush data for display
            has_flush_data = (total_flushed_filament_m > 0.0 || total_flushed_filament_g > 0.0) && !single_color_flush;
            if (has_flush_data) {
                flush_label = _u8L("Flushed");
                {
                    float flush_time_val = std::max(0.0f, time_mode.flush_time);
                    flush_time_str = flush_time_val > 0.0f ? short_time(get_time_dhms(flush_time_val)) : "";
                    if (flush_time_val > 0.0f && time_mode.time > 0.0f) {
                        float flush_time_pct = flush_time_val / time_mode.time * 100.0f;
                        flush_time_pct > 0.001f ? ::sprintf(buffer, "%.1f%%", flush_time_pct) : ::sprintf(buffer, "<0.1%%");
                        flush_time_percent_str = buffer;
                    } else {
                        flush_time_percent_str = "";
                    }
                }
                // Calculate flush percentage based on filament volume
                if (ps.total_used_filament > 0.0) {
                    double flush_percent_val = total_flushed_filament_m / (ps.total_used_filament / koef) * 100.0;
                    flush_percent_val > 0.001 ? ::sprintf(buffer, "%.1f%%", flush_percent_val) : ::sprintf(buffer, "<0.1%%");
                } else {
                    ::sprintf(buffer, "0%%");
                }
                flush_percent_str = buffer;
                ::sprintf(buffer, imperial_units ? "%.2f in" : "%.2f m", total_flushed_filament_m);
                flush_length_str = buffer;
                ::sprintf(buffer, imperial_units ? "%.2f oz" : "%.2f g",
                    imperial_units ? total_flushed_filament_g / unit_conver : total_flushed_filament_g);
                flush_weight_str = buffer;
            }

            if (has_flush_data) {
                labels.push_back(flush_label);
                times.push_back(flush_time_str);
                percents.push_back(flush_time_percent_str.empty() ? flush_percent_str : flush_time_percent_str);
                used_filaments_length.push_back(flush_length_str);
                used_filaments_weight.push_back(flush_weight_str);
            }
            offsets = calculate_offsets({ 
                {_u8L("Line Type"), labels},
                {_u8L("Time"), times}, 
                {_u8L("Percent"), percents}, 
                {_u8L("Length"), used_filaments_length}, 
                {_u8L("Weight"), used_filaments_weight}, 
                {_u8L("Display"), 
                {""}} }, icon_size);
            {
                const ImGuiStyle& style = ImGui::GetStyle();
                const float display_gap = std::max(style.ItemSpacing.x, 6.0f * m_scale);
                offsets[5] = offsets[4] + max_width(used_filaments_weight, _u8L("Weight")) + display_gap;
            }
            append_headers({ 
                {_u8L("Line Type"), offsets[0]}, 
                {_u8L("Time"), offsets[1]}, 
                {_u8L("Percent"), offsets[2]}, 
                {_u8L("Length"), offsets[3]},
                {_u8L("Weight"), offsets[4]},
                //{_u8L("Display"), offsets[5]} 
                });

            {
                const ImGuiStyle& style = ImGui::GetStyle();
                const float checkbox_width = ImGui::GetFrameHeight();
                const float checkbox_right_margin = 6.0f * m_scale;

                float rightmost = 0.0f;
                rightmost = std::max(rightmost, offsets[0] + max_width(labels, _u8L("Line Type")));
                rightmost = std::max(rightmost, offsets[1] + max_width(times, _u8L("Time")));
                rightmost = std::max(rightmost, offsets[2] + max_width(percents, _u8L("Percent")));
                rightmost = std::max(rightmost, offsets[3] + max_width(used_filaments_length, _u8L("Length")));
                rightmost = std::max(rightmost, offsets[4] + max_width(used_filaments_weight, _u8L("Weight")));
                rightmost = std::max(rightmost, offsets[5] + checkbox_width + checkbox_right_margin);

                const float required_window_width = rightmost + style.ScrollbarSize + 0.5f * window_padding;
                if (required_window_width > contentWidth) {
                    contentWidth = required_window_width;
                    imgui->set_requires_extra_frame();
                }
            }
            break;
        }
        case GCodeViewer::EViewType::Height: { imgui->title(_u8L("Layer Height (mm)")); break; }
        case GCodeViewer::EViewType::Width: { imgui->title(_u8L("Line Width (mm)")); break; }
        case GCodeViewer::EViewType::Feedrate:
        {
            imgui->title(_u8L("Speed (mm/s)"));
            break;
        }

        case GCodeViewer::EViewType::FanSpeed: { imgui->title(_u8L("Fan Speed (%)")); break; }
        case GCodeViewer::EViewType::Temperature: { imgui->title(_u8L("Temperature (°C)")); break; }
        case GCodeViewer::EViewType::VolumetricRate: { imgui->title(_u8L("Volumetric flow rate (mm³/s)")); break; }
        case GCodeViewer::EViewType::LayerTime: { imgui->title(_u8L("Layer Time")); break; }
        case GCodeViewer::EViewType::LayerTimeLog: { imgui->title(_u8L("Layer Time (log)")); break; }
        case GCodeViewer::EViewType::Acceleration: {
            std::string str = _u8L("Acceleration") + std::string(" (mm/s²)");
            imgui->title(str); break; 
            }
#if ENABLE_AUE_CUSTOM_PREVIEW
        case GCodeViewer::EViewType::Custom:
        {
            std::vector<std::string> roi_labels;
            std::vector<std::string> roi_desc;
            roi_labels.reserve(m_extruder_ids.size() * 3);
            roi_desc.reserve(m_extruder_ids.size() * 3);

            const bool multi_extruder = (m_extruder_ids.size() > 1);
            for (unsigned char extruder_id : m_extruder_ids) {
                std::string prefix;
                if (multi_extruder)
                    prefix = _u8L("Extruder") + " " + std::to_string(extruder_id + 1) + " - ";

                roi_labels.push_back(prefix + _u8L("Filament color"));
                roi_desc.push_back(_u8L("Filament color (original toolpath color)"));

                roi_labels.push_back(prefix + _u8L("ROI Trigger"));
                roi_desc.push_back(_u8L("ROI Trigger (slow-speed extrusion, e.g. overhang slow-down)"));
                roi_labels.push_back(prefix + _u8L("ROI Defect"));
                roi_desc.push_back(_u8L("ROI Defect (after returning to normal speed, under-extrusion prone)"));
            }

            offsets = calculate_offsets({ { _u8L("Color"), roi_labels }, { _u8L("Description"), roi_desc } }, icon_size);
            append_headers({ { _u8L("Color"), offsets[0] }, { _u8L("Description"), offsets[1] } });

            {
                const ImGuiStyle& style = ImGui::GetStyle();
                const float reserved_right_width = 2.0f * window_padding + style.ScrollbarSize + style.ItemSpacing.x;
                const float desc_column_width = max_width(roi_desc, _u8L("Description"));
                const float required_window_width = offsets[1] + desc_column_width + reserved_right_width;
                if (required_window_width > contentWidth) {
                    contentWidth = required_window_width;
                    imgui->set_requires_extra_frame();
                }
            }
            break;
        }
#endif // ENABLE_AUE_CUSTOM_PREVIEW
        case GCodeViewer::EViewType::Tool:
        {
            std::vector<std::string> extruder_labels;
            std::vector<std::string> used_filaments;
            extruder_labels.reserve(m_extruder_ids.size());
            used_filaments.reserve(m_extruder_ids.size());

            char buffer[64];
            for (size_t i = 0; i < m_extruder_ids.size(); ++i) {
                const unsigned char extruder_id = m_extruder_ids[i];
                extruder_labels.push_back(_u8L("Extruder") + " " + std::to_string(extruder_id + 1));
                ::sprintf(buffer, imperial_units ? "%.2f in    %.2f g" : "%.2f m    %.2f g", model_used_filaments_m[i], model_used_filaments_g[i]);
                used_filaments.push_back(buffer);
            }

            offsets = calculate_offsets({ { _u8L("Filament"), extruder_labels }, { _u8L("Used filament"), used_filaments } }, icon_size);
            append_headers({ { _u8L("Filament"), offsets[0] }, { _u8L("Used filament"), offsets[1] } });

            {
                const ImGuiStyle& style = ImGui::GetStyle();
                const float reserved_right_width = 2.0f * window_padding + style.ScrollbarSize + style.ItemSpacing.x;
                const float used_column_width = max_width(used_filaments, _u8L("Used filament"));
                const float required_window_width = offsets[1] + used_column_width + reserved_right_width;
                if (required_window_width > contentWidth) {
                    contentWidth = required_window_width;
                    imgui->set_requires_extra_frame();
                }
            }
            break;
        }
        case GCodeViewer::EViewType::ColorPrint:
        {
            std::vector<std::string> total_filaments;
            char buffer[64];
            ::sprintf(buffer, imperial_units ? "%.2f in\n%.2f oz" : "%.2f m\n%.2f g", ps.total_used_filament / /*1000*/koef, ps.total_weight / unit_conver);
            total_filaments.push_back(buffer);


            const bool merge_flush_into_model = should_merge_flush_into_model(m_print_statistics);
            const int color_print_displayed_columns = merge_flush_into_model ? (displayed_columns & ~ColumnData::Flushed) : displayed_columns;

            std::vector<std::pair<std::string, std::vector<::string>>> title_columns;
            if (color_print_displayed_columns & ColumnData::Model) {
                //title_columns.push_back({ _u8L("Filament"), {""} });
                title_columns.push_back({ _u8L("Filament"), total_filaments });
                title_columns.push_back({ _u8L("Model"), total_filaments });
            }
            if (color_print_displayed_columns & ColumnData::Support) {
                title_columns.push_back({ _u8L("Support"), total_filaments });
            }
            if (color_print_displayed_columns & ColumnData::Flushed) {
                title_columns.push_back({ _u8L("Flushed"), total_filaments });
            }
            if (color_print_displayed_columns & ColumnData::WipeTower) {
                title_columns.push_back({ _u8L("Tower"), total_filaments });
            }
            if ((color_print_displayed_columns & ~ColumnData::Model) > 0) {
                title_columns.push_back({ _u8L("Total"), total_filaments });
            }

            //checkbox 占位
            title_columns.push_back({ _u8L(""), total_filaments });

            auto offsets_ = calculate_offsets(title_columns, icon_size);
            std::vector<std::pair<std::string, float>> title_offsets;
            for (int i = 0; i < offsets_.size(); i++) {
                title_offsets.push_back({ title_columns[i].first, offsets_[i] });
                color_print_offsets[title_columns[i].first] = offsets_[i];
            }
            append_headers(title_offsets);

            contentWidth = offsets_.empty() ? 0.0f : offsets_.back() + ImGui::GetFontSize() + ImGui::GetStyle().ItemSpacing.x + window_padding + ImGui::GetStyle().ScrollbarSize;

            break;
        }
        default: { break; }
        }
    }

    void showColorTable(GCodeViewer::EViewType m_view_type
        , const std::vector<CustomGCode::Item>& m_custom_gcode_per_print_z
        , GCodeViewer::ETools &m_tools, bool preview_lite_mode
        , std::function<void()>featureFn, std::function<void()>fadeRateFn) {

        switch (m_view_type)
        {
        case Slic3r::GUI::GCodeViewer::EViewType::FeatureType: 
        {
            const bool is_lite_mode = preview_lite_mode;
            
            for (size_t i = 0; i < m_roles.size(); ++i) {
                ExtrusionRole role = m_roles[i];
                if (role >= erCount)
                    continue;
                bool enable = true;
                if (is_lite_mode) {
                    enable = !(role_been_filtered_in_lite_mode(role));
                }
                const bool visible = is_visible(role);
                std::vector<std::pair<std::string, float>> columns_offsets;
                columns_offsets.push_back({ labels[i], offsets[0] });
                columns_offsets.push_back({ times[i], offsets[1] });
                columns_offsets.push_back({ percents[i], offsets[2] });
                columns_offsets.push_back({ used_filaments_length[i], offsets[3] });
                columns_offsets.push_back({ used_filaments_weight[i], offsets[4] });
                append_item(EItemType::Rect
                    , Extrusion_Role_Colors[static_cast<unsigned int>(role)]
                    , columns_offsets, enable, visible, [&]() {
                        m_extrusions.role_visibility_flags = visible ?
                            m_extrusions.role_visibility_flags & ~(1 << role) :
                            m_extrusions.role_visibility_flags | (1 << role);
                        featureFn();
                    });
            }
            if ((m_extruder_ids.size() == 1) && (total_flushed_filament_m > 0.0 || total_flushed_filament_g > 0.0) &&
                std::find(m_roles.begin(), m_roles.end(), erCustom) == m_roles.end()) {
                const bool visible = is_visible(erCustom);
                const size_t idx = labels.size() - 1;
                std::vector<std::pair<std::string, float>> columns_offsets;
                columns_offsets.push_back({ labels[idx], offsets[0] });
                columns_offsets.push_back({ times[idx], offsets[1] });
                columns_offsets.push_back({ percents[idx], offsets[2] });
                columns_offsets.push_back({ used_filaments_length[idx], offsets[3] });
                columns_offsets.push_back({ used_filaments_weight[idx], offsets[4] });
                append_item(EItemType::Rect
                    , Extrusion_Role_Colors[static_cast<unsigned int>(erCustom)]
                    , columns_offsets, !is_lite_mode, visible, [&]() {
                        m_extrusions.role_visibility_flags = visible ?
                            m_extrusions.role_visibility_flags & ~(1 << erCustom) :
                            m_extrusions.role_visibility_flags | (1 << erCustom);
                        featureFn();
                    });
            }

            for (auto type : options_items) {
                if (type != EMoveType::Travel) {

                    const bool visible = m_buffers[buffer_id(type)].visible;

                    auto append_option_item_with_type = [&](const ColorRGBA& color, const std::string& label, bool checkbox = true) {
                        append_item(EItemType::Rect
                            , color, { { label , offsets[0] } }
                            , checkbox, visible, [&]() {
                                m_buffers[buffer_id(type)].visible = !m_buffers[buffer_id(type)].visible;
                                featureFn();
                            });
                    };

                    if (type == EMoveType::Seam)
                        append_option_item_with_type(Options_Colors[GCodeViewer::Seams], _u8L("Seams"));
                    else if (type == EMoveType::Retract)
                        append_option_item_with_type(Options_Colors[GCodeViewer::Retractions], _u8L("Retract"), !is_lite_mode);
                    else if (type == EMoveType::Unretract)
                        append_option_item_with_type(Options_Colors[GCodeViewer::Unretractions], _u8L("Unretract"));
                    else if (type == EMoveType::Tool_change)
                        append_option_item_with_type(Options_Colors[GCodeViewer::ToolChanges], _u8L("Filament Changes"), !is_lite_mode);
                    else if (type == EMoveType::Wipe)
                        append_option_item_with_type(Wipe_Color, _u8L("Wipe"), !is_lite_mode);
                }
                else {
                    // Keep this before Travel because this list renders in reverse visual order.
                    if (has_flush_data) {
                        std::vector<std::pair<std::string, float>> flush_columns;
                        flush_columns.push_back({ flush_label, offsets[0] });
                        flush_columns.push_back({ flush_time_str, offsets[1] });
                        flush_columns.push_back({ flush_time_percent_str.empty() ? flush_percent_str : flush_time_percent_str, offsets[2] });
                        flush_columns.push_back({ flush_length_str, offsets[3] });
                        flush_columns.push_back({ flush_weight_str, offsets[4] });
                        const bool flush_visible = is_visible(erWipeTower);
                        append_item(EItemType::Rect
                            , Flush_Color, flush_columns
                            , !is_lite_mode, flush_visible, [&]() {
                                m_extrusions.role_visibility_flags = flush_visible ?
                                    m_extrusions.role_visibility_flags & ~(1 << erWipeTower) :
                                    m_extrusions.role_visibility_flags | (1 << erWipeTower);
                                featureFn();
                            });
                    }

                    const bool visible = m_buffers[buffer_id(EMoveType::Travel)].visible;

                    std::vector<std::pair<std::string, float>> columns_offsets;
                    columns_offsets.push_back({ _u8L("Travel"), offsets[0] });
                    columns_offsets.push_back({ travel_time, offsets[1] });
                    columns_offsets.push_back({ travel_percent, offsets[2] });
                    append_item(EItemType::Rect
                        , Travel_Colors[0], columns_offsets
                        , !is_lite_mode, visible, [&]() {
                            m_buffers[buffer_id(EMoveType::Travel)].visible = !m_buffers[buffer_id(EMoveType::Travel)].visible;
                            featureFn();
                        });
                }
            }
        }
            break;
        case Slic3r::GUI::GCodeViewer::EViewType::Height:
            append_range(m_extrusions.ranges.height, 2);
            break;
        case Slic3r::GUI::GCodeViewer::EViewType::Width:
            append_range(m_extrusions.ranges.width, 2);
            break;
        case Slic3r::GUI::GCodeViewer::EViewType::Feedrate: {
            append_range(m_extrusions.ranges.feedrate, 0);
            ImGui::Spacing();
            ImGui::Dummy({ window_padding, window_padding });
            ImGui::SameLine();
            offsets = calculate_offsets({ { _u8L("Options"), { _u8L("Travel")}}, { _u8L("Display"), {""}} }, icon_size);
            append_headers({ {_u8L("Options"), offsets[0] }, { _u8L("Display"), offsets[1]} });
            const bool travel_visible = m_buffers[buffer_id(EMoveType::Travel)].visible;
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 3.0f));
            append_item(EItemType::None, Travel_Colors[0]
                , { {_u8L("travel"), offsets[0] } }, true, travel_visible, [&]() {
                m_buffers[buffer_id(EMoveType::Travel)].visible = !m_buffers[buffer_id(EMoveType::Travel)].visible;
                fadeRateFn();
                });
            ImGui::PopStyleVar(1);
        }
            break;
        case Slic3r::GUI::GCodeViewer::EViewType::FanSpeed:
            append_range(m_extrusions.ranges.fan_speed, 0);
            break;
        case Slic3r::GUI::GCodeViewer::EViewType::Temperature:
            append_range(m_extrusions.ranges.temperature, 0);
            break;
        case Slic3r::GUI::GCodeViewer::EViewType::VolumetricRate:
            append_range(m_extrusions.ranges.volumetric_rate, 2);
            break;
        case Slic3r::GUI::GCodeViewer::EViewType::Acceleration:
            append_range(m_extrusions.ranges.acceleration, 0);
            break;
#if ENABLE_AUE_CUSTOM_PREVIEW
        case Slic3r::GUI::GCodeViewer::EViewType::Custom: {
            const bool multi_extruder = (m_extruder_ids.size() > 1);
            for (unsigned char extruder_id : m_extruder_ids) {
                const ColorRGBA filament_color = (extruder_id < m_tools.m_tool_colors.size()) ? m_tools.m_tool_colors[extruder_id] : ColorRGBA::GRAY();
                const auto highlight_colors = pick_interest_region_highlight_colors(filament_color);
                const ColorRGBA& trigger_color = highlight_colors.first;
                const ColorRGBA& defect_color  = highlight_colors.second;

                std::string prefix;
                if (multi_extruder)
                    prefix = _u8L("Extruder") + " " + std::to_string(extruder_id + 1) + " - ";

                append_item(EItemType::Rect, filament_color,
                    { { prefix + _u8L("Filament color"), offsets[0] }, { _u8L("Filament color (original toolpath color)"), offsets[1] } });
                append_item(EItemType::Rect, trigger_color,
                    { { prefix + _u8L("ROI Trigger"), offsets[0] }, { _u8L("ROI Trigger (slow-speed extrusion, e.g. overhang slow-down)"), offsets[1] } });
                append_item(EItemType::Rect, defect_color,
                    { { prefix + _u8L("ROI Defect"), offsets[0] }, { _u8L("ROI Defect (after returning to normal speed, under-extrusion prone)"), offsets[1] } });
            }
        }
           break;
#endif // ENABLE_AUE_CUSTOM_PREVIEW
        case Slic3r::GUI::GCodeViewer::EViewType::Tool: {
            size_t i = 0;
            for (unsigned char extruder_id : m_extruder_ids) {
                ::sprintf(buf, imperial_units ? "%.2f in    %.2f g" : "%.2f m    %.2f g", model_used_filaments_m[i], model_used_filaments_g[i]);
                append_item(EItemType::Rect, m_tools.m_tool_colors[extruder_id], { { _u8L("Extruder") + " " + std::to_string(extruder_id + 1), offsets[0]}, {buf, offsets[1]} });
                i++;
            }
        }
           break;
        case Slic3r::GUI::GCodeViewer::EViewType::ColorPrint: {
            //BBS: replace model custom gcode with current plate custom gcode
            const std::vector<CustomGCode::Item>& custom_gcode_per_print_z = m_custom_gcode_per_print_z;
            const bool merge_flush_into_model = should_merge_flush_into_model(m_print_statistics);
            const int color_print_displayed_columns = merge_flush_into_model ? (displayed_columns & ~ColumnData::Flushed) : displayed_columns;
            size_t i = 0;
            for (auto extruder_idx : m_extruder_ids) {
                if (m_tools.m_tool_visibles.size() <= extruder_idx)
                    continue;

                const bool filament_visible = m_tools.m_tool_visibles[extruder_idx];
                if (i < model_used_filaments_m.size() && i < model_used_filaments_g.size()) {
                    std::vector<std::pair<std::string, float>> columns_offsets;
                    columns_offsets.push_back({ std::to_string(extruder_idx + 1), color_print_offsets[_u8L("Filament")] });

                    char buf[64];
                    float column_sum_m = 0.0f;
                    float column_sum_g = 0.0f;
                    if (color_print_displayed_columns & ColumnData::Model) {
                        const double model_filament_m = model_used_filaments_m[i] + (merge_flush_into_model ? flushed_filaments_m[i] : 0.0);
                        const double model_filament_g = model_used_filaments_g[i] + (merge_flush_into_model ? flushed_filaments_g[i] : 0.0);
                        if ((color_print_displayed_columns & ~ColumnData::Model) > 0)
                            ::sprintf(buf, imperial_units ? "%.2f in\n%.2f oz" : "%.2f m\n%.2f g", model_filament_m, model_filament_g / unit_conver);
                        else
                            ::sprintf(buf, imperial_units ? "%.2f in    %.2f oz" : "%.2f m    %.2f g", model_filament_m, model_filament_g / unit_conver);
                        columns_offsets.push_back({ buf, color_print_offsets[_u8L("Model")] });
                        column_sum_m += model_filament_m;
                        column_sum_g += model_filament_g;
                    }
                    if (color_print_displayed_columns & ColumnData::Support) {
                        ::sprintf(buf, imperial_units ? "%.2f in\n%.2f oz" : "%.2f m\n%.2f g", support_used_filaments_m[i], support_used_filaments_g[i] / unit_conver);
                        columns_offsets.push_back({ buf, color_print_offsets[_u8L("Support")] });
                        column_sum_m += support_used_filaments_m[i];
                        column_sum_g += support_used_filaments_g[i];
                    }
                    if (color_print_displayed_columns & ColumnData::Flushed) {
                        ::sprintf(buf, imperial_units ? "%.2f in\n%.2f oz" : "%.2f m\n%.2f g", flushed_filaments_m[i], flushed_filaments_g[i] / unit_conver);
                        columns_offsets.push_back({ buf, color_print_offsets[_u8L("Flushed")] });
                        column_sum_m += flushed_filaments_m[i];
                        column_sum_g += flushed_filaments_g[i];
                    }
                    if (color_print_displayed_columns & ColumnData::WipeTower) {
                        ::sprintf(buf, imperial_units ? "%.2f in\n%.2f oz" : "%.2f m\n%.2f g", wipe_tower_used_filaments_m[i], wipe_tower_used_filaments_g[i] / unit_conver);
                        columns_offsets.push_back({ buf, color_print_offsets[_u8L("Tower")] });
                        column_sum_m += wipe_tower_used_filaments_m[i];
                        column_sum_g += wipe_tower_used_filaments_g[i];
                    }
                    if ((color_print_displayed_columns & ~ColumnData::Model) > 0) {
                        ::sprintf(buf, imperial_units ? "%.2f in\n%.2f oz" : "%.2f m\n%.2f g", column_sum_m, column_sum_g / unit_conver);
                        columns_offsets.push_back({ buf, color_print_offsets[_u8L("Total")] });
                    }
						
                    append_color_print_item(EItemType::Rect, m_tools.m_tool_colors[extruder_idx]
                        , columns_offsets, true, filament_visible, [&]() {
                            m_tools.m_tool_visibles[extruder_idx] = !m_tools.m_tool_visibles[extruder_idx];
                            featureFn();
                        });
                }
                i++;
            }
            if (m_extruder_ids.size() > 1) {
                // Separator
                ImGuiWindow* window = ImGui::GetCurrentWindow();
                const ImRect separator(ImVec2(window->Pos.x + window_padding * 3, window->DC.CursorPos.y), ImVec2(window->Pos.x + window->Size.x - window_padding * 3, window->DC.CursorPos.y + 1.0f));
                ImGui::ItemSize(ImVec2(0.0f, 0.0f));
                const bool item_visible = ImGui::ItemAdd(separator, 0);
                window->DrawList->AddLine(separator.Min, ImVec2(separator.Max.x, separator.Min.y), ImGui::GetColorU32(ImGuiCol_Separator));

                std::vector<std::pair<std::string, float>> columns_offsets;
                columns_offsets.push_back({ _u8L("Total"), color_print_offsets[_u8L("Filament")] });
                if (color_print_displayed_columns & ColumnData::Model) {
                    const double model_filament_m = total_model_used_filament_m + (merge_flush_into_model ? total_flushed_filament_m : 0.0);
                    const double model_filament_g = total_model_used_filament_g + (merge_flush_into_model ? total_flushed_filament_g : 0.0);
                    if ((color_print_displayed_columns & ~ColumnData::Model) > 0)
                        ::sprintf(buf, imperial_units ? "%.2f in\n%.2f oz" : "%.2f m\n%.2f g", model_filament_m, model_filament_g / unit_conver);
                    else
                        ::sprintf(buf, imperial_units ? "%.2f in    %.2f oz" : "%.2f m    %.2f g", model_filament_m, model_filament_g / unit_conver);
                    columns_offsets.push_back({ buf, color_print_offsets[_u8L("Model")] });
                }
                if (color_print_displayed_columns & ColumnData::Support) {
                    ::sprintf(buf, imperial_units ? "%.2f in\n%.2f oz" : "%.2f m\n%.2f g", total_support_used_filament_m, total_support_used_filament_g / unit_conver);
                    columns_offsets.push_back({ buf, color_print_offsets[_u8L("Support")] });
                }
                if (color_print_displayed_columns & ColumnData::Flushed) {
                    ::sprintf(buf, imperial_units ? "%.2f in\n%.2f oz" : "%.2f m\n%.2f g", total_flushed_filament_m, total_flushed_filament_g / unit_conver);
                    columns_offsets.push_back({ buf, color_print_offsets[_u8L("Flushed")] });
                }
                if (color_print_displayed_columns & ColumnData::WipeTower) {
                    ::sprintf(buf, imperial_units ? "%.2f in\n%.2f oz" : "%.2f m\n%.2f g", total_wipe_tower_used_filament_m, total_wipe_tower_used_filament_g / unit_conver);
                    columns_offsets.push_back({ buf, color_print_offsets[_u8L("Tower")] });
                }
                if ((color_print_displayed_columns & ~ColumnData::Model) > 0) {
                    ::sprintf(buf, imperial_units ? "%.2f in\n%.2f oz" : "%.2f m\n%.2f g",
                        total_model_used_filament_m + total_support_used_filament_m + (merge_flush_into_model ? 0.0 : total_flushed_filament_m) + total_wipe_tower_used_filament_m,
                        (total_model_used_filament_g + total_support_used_filament_g + (merge_flush_into_model ? 0.0 : total_flushed_filament_g) + total_wipe_tower_used_filament_g) / unit_conver);
                    columns_offsets.push_back({ buf, color_print_offsets[_u8L("Total")] });
                }
                append_item(EItemType::None, m_tools.m_tool_colors[0], columns_offsets);
            }

            //BBS display filament change times
            ImGui::Dummy({ window_padding, window_padding });
            ImGui::SameLine();
            imgui->text(_u8L("Filament change times") + ":");
            ImGui::SameLine();
            ::sprintf(buf, "%d", m_print_statistics.total_filamentchanges);
            imgui->text(buf);

            //BBS display cost
            ImGui::Dummy({ window_padding, window_padding });
            ImGui::SameLine();
            imgui->text(_u8L("Cost") + ":");
            ImGui::SameLine();
            ::sprintf(buf, "%.2f", ps.total_cost);
            imgui->text(buf);
        }
            break;
        case Slic3r::GUI::GCodeViewer::EViewType::LayerTime:
            append_range(m_extrusions.ranges.layer_duration, true);
            break;
        case Slic3r::GUI::GCodeViewer::EViewType::LayerTimeLog:
            append_range(m_extrusions.ranges.layer_duration_log, true);
            break;
        default:
            break;
        }
    }

private:

    std::pair<float,float> role_time_and_percent(ExtrusionRole role) {
        auto it = std::find_if(time_mode.roles_times.begin(), time_mode.roles_times.end(), [role](const std::pair<ExtrusionRole, float>& item) { return role == item.first; });
        return (it != time_mode.roles_times.end()) ? std::make_pair(it->second, it->second / time_mode.time) : std::make_pair(0.0f, 0.0f);
    };

    std::pair<float, float> used_filament_per_role(ExtrusionRole role) {
        auto it = m_print_statistics.used_filaments_per_role.find(role);
        if (it == m_print_statistics.used_filaments_per_role.end())
            return std::make_pair(0.0, 0.0);

        double koef = imperial_units ? GizmoObjectManipulation::in_to_mm / 1000.0 : 1.0;
        double unit_conver = imperial_units ? GizmoObjectManipulation::oz_to_g : 1;
        return std::make_pair(it->second.first / koef, it->second.second / unit_conver);
    };

    std::pair<float, float> move_time_and_percent(EMoveType move_type) {
        auto it = std::find_if(time_mode.moves_times.begin(), time_mode.moves_times.end(), [move_type]
        (const std::pair<EMoveType, float>& item) { return move_type == item.first; });
        return (it != time_mode.moves_times.end()) ?
            std::make_pair(it->second, it->second / time_mode.time) : std::make_pair(0.0f, 0.0f);
    };

    float max_width(const std::vector<std::string>& items, const std::string& title, float extra_size = 0.0f) {
        float ret = ImGui::CalcTextSize(title.c_str()).x;
        for (const std::string& item : items) {
            ret = std::max(ret, extra_size + ImGui::CalcTextSize(item.c_str()).x);
        }
        return ret;
    };

    std::vector<float> calculate_offsets(const std::vector<std::pair<std::string, std::vector<::string>>>& title_columns, float extra_size = 0.0f) {
        const ImGuiStyle& style = ImGui::GetStyle();
        std::vector<float> offsets;
        //offsets.push_back(max_width(title_columns[0].second, title_columns[0].first, extra_size) + 3.0f * style.ItemSpacing.x);
        offsets.push_back(max_width(title_columns[0].second, title_columns[0].first, extra_size) + style.ItemSpacing.x + icon_size);
        for (size_t i = 1; i < title_columns.size() - 1; i++)
            offsets.push_back(offsets.back() + max_width(title_columns[i].second, title_columns[i].first) + style.ItemSpacing.x);
        if (title_columns.back().first == _u8L("Percent")) {
            const auto preferred_offset = ImGui::GetWindowWidth() 
                - ImGui::CalcTextSize(_u8L("Percent").c_str()).x 
                - ImGui::GetFrameHeight() / 2 
                - 2 * window_padding 
                - ImGui::GetStyle().ScrollbarSize;
            if (preferred_offset > offsets.back()) {
                offsets.back() = preferred_offset;
                imgui->set_requires_extra_frame();
            }
        }

        float average_col_width = 400*m_scale / static_cast<float>(title_columns.size());
        std::vector<float> ret;
        ret.push_back(0);
        for (size_t i = 1; i < title_columns.size(); i++) {
            ret.push_back(std::max(offsets[i - 1], i * average_col_width));
        }
		
        return ret;
    };

    std::pair<double, double> get_used_filament_from_volume(double volume, int extruder_id) {
        double koef = imperial_units ? 1.0 / GizmoObjectManipulation::in_to_mm : 0.001;
        std::pair<double, double> ret = { koef * volume / (PI * sqr(0.5 * m_filament_diameters[extruder_id])),
                                          volume * m_filament_densities[extruder_id] * 0.001 };
        return ret;
    };

    void append_headers(const std::vector<std::pair<std::string, float>>& title_offsets) {
        for (size_t i = 0; i < title_offsets.size(); i++) {
            ImGui::SameLine(title_offsets[i].second);
            imgui->bold_text(title_offsets[i].first);
        }
        // Ensure right padding
        ImGui::SameLine();
        ImGui::Dummy({ window_padding, 1 });
        ImGui::Separator();
    };

    void append_item(EItemType type,const ColorRGBA& color,
        const std::vector<std::pair<std::string, float>>& columns_offsets,
        bool checkbox = true,bool visible = true,std::function<void()> callback = nullptr)
    {
        float row_start_y = ImGui::GetCursorPosY();
        auto draw_list = ImGui::GetWindowDrawList();
        // render icon
        ImVec2 pos = ImVec2(ImGui::GetCursorScreenPos().x + window_padding, ImGui::GetCursorScreenPos().y);
        switch (type) {
        default:
        case EItemType::Rect: {
            draw_list->AddRectFilled({ pos.x + 1.0f * m_scale, pos.y + 1.0f * m_scale }, { pos.x + icon_size - 1.0f * m_scale, pos.y + icon_size + 1.0f * m_scale },
                ImGuiWrapper::to_ImU32(color));
            break;
        }
        case EItemType::Circle: {
            ImVec2 center(0.5f * (pos.x + pos.x + icon_size), 0.5f * (pos.y + pos.y + icon_size + 5.0f));
            draw_list->AddCircleFilled(center, 0.5f * icon_size, ImGuiWrapper::to_ImU32(color), 16);
            break;
        }
        case EItemType::Hexagon: {
            ImVec2 center(0.5f * (pos.x + pos.x + icon_size), 0.5f * (pos.y + pos.y + icon_size + 5.0f));
            draw_list->AddNgonFilled(center, 0.5f * icon_size, ImGuiWrapper::to_ImU32(color), 6);
            break;
        }
        case EItemType::Line: {
            draw_list->AddLine({ pos.x + 1, pos.y + icon_size + 2 }, { pos.x + icon_size - 1, pos.y + 4 }, ImGuiWrapper::to_ImU32(color), 3.0f);
            break;
        case EItemType::None:
            break;
        }
        }

        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(20.0 * m_scale, 6.0 * m_scale));
        ImGui::Dummy({ 0.0, 0.0 });
        ImGui::SameLine();
        if (callback) {
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(20.0 * m_scale, 0.0));
            float max_height = 0.f;
            for (auto column_offset : columns_offsets) {
                if (ImGui::CalcTextSize(column_offset.first.c_str()).y > max_height)
                    max_height = ImGui::CalcTextSize(column_offset.first.c_str()).y;
            }
            bool   b_menu_item = ImGui::BBLMenuItem(("##" + columns_offsets[0].first).c_str(), nullptr, false, true, max_height);
            ImRect row_rect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
            ImGui::PopStyleVar(1);
            if (b_menu_item)
                callback();
            if (checkbox) {
                const float checkbox_width = ImGui::GetFrameHeight();
                const float checkbox_right_margin = 6.0f * m_scale;
                float bias = ImGui::GetWindowContentRegionMax().x - checkbox_width - checkbox_right_margin;
                bias = std::max(0.0f, bias);
                ImGui::SameLine(bias);
                float  check_height  = ImGui::GetFrameHeight();
                float  target_y      = row_start_y + icon_size * 0.5f - check_height * 0.5f;
                target_y             = std::max(row_start_y, target_y);
                ImVec2 checkbox_pos  = ImGui::GetCursorPos();
                ImGui::SetCursorPos(ImVec2(checkbox_pos.x, target_y));
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4.0f * m_scale, 3.0f * m_scale));
                visible = config.checkBox("##" + columns_offsets[0].first, visible);
                ImGui::PopStyleVar(1);
                ImGui::SetCursorPos(checkbox_pos);
            }
        }

        float dummy_size = type == EItemType::None ? window_padding * 3 : ImGui::GetStyle().ItemSpacing.x + icon_size;
        ImGui::SameLine(dummy_size);
        imgui->text(columns_offsets[0].first);

        for (auto i = 1; i < columns_offsets.size(); i++) {
            ImGui::SameLine(columns_offsets[i].second);
            imgui->text(columns_offsets[i].first);
        }
        ImGui::PopStyleVar(1);
    };


	void append_color_print_item(EItemType                             type,
                     const ColorRGBA&                                  color,
                     const std::vector<std::pair<std::string, float>>& columns_offsets,
                     bool                                              checkbox = true,
                     bool                                              visible  = true,
                     std::function<void()>                             callback = nullptr)
    {
        float row_start_y = ImGui::GetCursorPosY();
        auto draw_list = ImGui::GetWindowDrawList();

        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(20.0 * m_scale, 6.0 * m_scale));
        float dummy_size = type == EItemType::None ? window_padding * 3 : ImGui::GetStyle().ItemSpacing.x + icon_size;
        // render icon
        ImVec2 pos = ImVec2(ImGui::GetCursorScreenPos().x + dummy_size, ImGui::GetCursorScreenPos().y);
        switch (type) {
        default:
        case EItemType::Rect: {
            /*draw_list->AddRectFilled({pos.x + 1.0f * m_scale, pos.y + 1.0f * m_scale},
                                     {pos.x + icon_size - 1.0f * m_scale, pos.y + icon_size + 1.0f * m_scale},
                                     ImGuiWrapper::to_ImU32(color));*/

			bool is_dark = wxGetApp().dark_mode();			
			
			// frame background color
			ColorRGB x = is_dark ? ColorRGB((unsigned char) 43, (unsigned char) 43, (unsigned char) 45) :
                                   ColorRGB((unsigned char) 230, (unsigned char) 230, (unsigned char) 233);
			
			float k = abs(color.r() - x.r()) + abs(color.g() - x.g()) + abs(color.b() - x.b());
            if (k < 0.3) {
                ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
			} else {
                ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
			}			

			ImGui::RenderFrame({pos.x - 1.0f * m_scale, pos.y - 1.0f * m_scale},
                               {pos.x + icon_size + 1.0f * m_scale, pos.y + icon_size + 1.0f * m_scale}, ImGuiWrapper::to_ImU32(color),
                               true,
                               3.0f);
			ImGui::PopStyleVar();
            break;
        }
        case EItemType::Circle: {
            ImVec2 center(0.5f * (pos.x + pos.x + icon_size), 0.5f * (pos.y + pos.y + icon_size + 5.0f));
            draw_list->AddCircleFilled(center, 0.5f * icon_size, ImGuiWrapper::to_ImU32(color), 16);
            break;
        }
        case EItemType::Hexagon: {
            ImVec2 center(0.5f * (pos.x + pos.x + icon_size), 0.5f * (pos.y + pos.y + icon_size + 5.0f));
            draw_list->AddNgonFilled(center, 0.5f * icon_size, ImGuiWrapper::to_ImU32(color), 6);
            break;
        }
        case EItemType::Line: {
            draw_list->AddLine({pos.x + 1, pos.y + icon_size + 2}, {pos.x + icon_size - 1, pos.y + 4}, ImGuiWrapper::to_ImU32(color), 3.0f);
            break;
        case EItemType::None: break;
        }
        }

        
        //ImGui::Dummy({0.0, 0.0});
        //ImGui::SameLine();
        if (callback) {
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(20.0 * m_scale, 0.0));
            float max_height = 0.f;
            for (auto column_offset : columns_offsets) {
                if (ImGui::CalcTextSize(column_offset.first.c_str()).y > max_height)
                    max_height = ImGui::CalcTextSize(column_offset.first.c_str()).y;
            }
            bool   b_menu_item = ImGui::BBLMenuItem(("##" + columns_offsets[0].first).c_str(), nullptr, false, true, max_height);
            ImRect row_rect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
            ImGui::PopStyleVar(1);
            if (b_menu_item)
                callback();
            if (checkbox) {
                const float checkbox_width = ImGui::GetFrameHeight();
                const float checkbox_right_margin = 6.0f * m_scale;
                float bias = ImGui::GetWindowContentRegionMax().x - checkbox_width - checkbox_right_margin;
                bias = std::max(0.0f, bias);
                ImGui::SameLine(bias);
                float  check_height  = ImGui::GetFrameHeight();
                float  target_y      = row_start_y + icon_size * 0.5f - check_height * 0.5f;
                target_y             = std::max(row_start_y, target_y);
                ImVec2 checkbox_pos  = ImGui::GetCursorPos();
                ImGui::SetCursorPos(ImVec2(checkbox_pos.x, target_y));
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4.0f * m_scale, 3.0f * m_scale));
                visible = config.checkBox("##" + columns_offsets[0].first, visible);
                ImGui::PopStyleVar(1);
                ImGui::SetCursorPos(checkbox_pos);
            }
        }

        const auto& t = columns_offsets[0].first;
        ImVec2 text_size = ImGui::CalcTextSize(t.c_str());
        ImGui::SameLine(dummy_size + (icon_size - text_size.x) / 2.0f);

		float gray = color.r() * 0.299f + color.g() * 0.587f + color.b() * 0.114f;
        if (gray > 0.5f) {
            gray = 0.0f;
        } else {
            gray = 1.0f;
        }
        
		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(gray, gray, gray, 1.0f));

        imgui->text(t);

		ImGui::PopStyleColor();

        for (auto i = 1; i < columns_offsets.size(); i++) {
            ImGui::SameLine(columns_offsets[i].second);
            imgui->text(columns_offsets[i].first);
        }
        ImGui::PopStyleVar(1);
    };

    void append_range_item(int i, float value, unsigned int decimals) {
        char buf[1024];
        ::sprintf(buf, "%.*f", decimals, value);
        append_item(EItemType::Rect, Range_Colors[i], { { buf , 0} });
    };

    void append_range(const GCodeViewer::Extrusions::Range& range, unsigned int decimals) {
        if (range.count == 1)
            // single item use case
            append_range_item(0, range.min, decimals);
        else if (range.count == 2) {
            append_range_item(static_cast<int>(Range_Colors.size()) - 1, range.max, decimals);
            append_range_item(0, range.min, decimals);
        }
        else {
            const float step_size = range.step_size();
            for (int i = static_cast<int>(Range_Colors.size()) - 1; i >= 0; --i) {
                append_range_item(i, range.get_value_at_step(i), decimals);
            }
        }
    };

    bool is_visible(ExtrusionRole role) const {
        return 
            role < erCount 
            && (m_extrusions.role_visibility_flags& (1 << role)) != 0;
    }

    ////////////////////////////////////
    std::vector<float> offsets;
    std::vector<std::string> labels;
    std::vector<std::string> times;
    std::string travel_time;
    std::vector<std::string> percents;
    std::vector<std::string> used_filaments_length;
    std::vector<std::string> used_filaments_weight;
    std::string travel_percent;
    // Flush row data for Line Type legend
    std::string flush_label;
    std::string flush_time_str;
    std::string flush_percent_str;
    std::string flush_length_str;
    std::string flush_weight_str;
    bool        has_flush_data = false;
    std::string flush_time_percent_str;
    std::vector<double> model_used_filaments_m;
    std::vector<double> model_used_filaments_g;
    double total_model_used_filament_m = 0, total_model_used_filament_g = 0;
    std::vector<double> flushed_filaments_m;
    std::vector<double> flushed_filaments_g;
    double total_flushed_filament_m = 0, total_flushed_filament_g = 0;
    std::vector<double> wipe_tower_used_filaments_m;
    std::vector<double> wipe_tower_used_filaments_g;
    double total_wipe_tower_used_filament_m = 0, total_wipe_tower_used_filament_g = 0;
    std::vector<double> support_used_filaments_m;
    std::vector<double> support_used_filaments_g;
    double total_support_used_filament_m = 0, total_support_used_filament_g = 0;
    int displayed_columns = 0;
    std::map<std::string, float> color_print_offsets;
};
 
//BBS: GUI refactor: add canvas width and height
void GCodeViewer::render(int canvas_width, int canvas_height)
{
#if ENABLE_GCODE_VIEWER_STATISTICS
    m_statistics.reset_opengl();
    m_statistics.total_instances_gpu_size = 0;
#endif // ENABLE_GCODE_VIEWER_STATISTICS

    // test helper
    bool is_capture_mode = false;
    if (Test::enable_test) {
        std::string mode = Test::Visitor().call_cmd("is_capture_mode", "{}");
        if (!mode.empty())
            is_capture_mode = nlohmann::json::parse(mode)["enable"].get<int>() != 0;
    }

    glsafe(::glEnable(GL_DEPTH_TEST));

    bool empty_roles = m_roles.empty();
    bool ban_shells_render = false;
    if (wxGetApp().preset_bundle->machine_is_belt() && empty_roles) // 路径没渲染完成时，cr30禁止显示半透明轮廓
        ban_shells_render = true;

    if (!ban_shells_render)
    {
        if (!m_roles.empty() && m_layers_slider && m_layers_slider->is_higher_at_max() && m_layers_slider->is_lower_at_min()) {
        } else {
            render_shells(canvas_width, canvas_height);
        }
    }

    if (empty_roles)
        return;

    if (!m_no_render_path) {
        Transform3d transform = wxGetApp().plater()->get_current_canvas3D()->get_preview_extra_transform();
        auto pos = transform * m_sequential_view.current_position.cast<double>();
        marker.set_world_position(pos.cast<float>());
        marker.set_world_offset(m_sequential_view.current_offset);
        float sc = wxGetApp().plater()->get_current_canvas3D()->get_scale();
        int bottom_margin = 120 * GCODE_VIEWER_SLIDER_SCALE*sc;
        if (!is_capture_mode)
            marker.render(canvas_width, canvas_height - bottom_margin, m_view_type, m_showMark);
    }
    
    if (m_bLoaded)
        render_toolpaths();
    
    if (!is_capture_mode && m_legend_enabled) {

        ImVec4 tmp_color = ImVec4(158.0f / 255.0, 158.0f / 255.0, 158.0f / 255.0, 1.0f);
        ImGui::PushStyleColor(ImGuiCol_ScrollbarBg, ImGui::ColorConvertU32ToFloat4(IM_COL32_BLACK_TRANS));
        ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab, tmp_color);
        ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabHovered, tmp_color);
        ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabActive, tmp_color);
        /*ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, 6.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarRounding, 3.0f);*/
        
        GcodeHelper helper(
            m_scale,(int)m_time_estimate_mode, options_items
            ,m_roles,m_buffers,m_extrusions
            ,m_print_statistics,m_extruder_ids
            ,m_filament_diameters,m_filament_densities);

        auto wcfg = helper.prepare(m_fold,m_contentWidth);
        //wcfg.bgalpha = 5/100.f;
        DispConfig().processWindows("gcode_legend", [&]() {
            {
                const ImGuiStyle& style = ImGui::GetStyle();
                ImVec2 win_pos = ImGui::GetWindowPos();
                ImVec2 win_sz  = ImGui::GetWindowSize();
                ImDrawList* bg = ImGui::GetBackgroundDrawList();
                const ImVec4 sh_rgb = ImVec4(118.0f/255.0f, 142.0f/255.0f, 171.0f/255.0f, 1.0f);
                const float alphas[8] = { 0.050f, 0.040f, 0.032f, 0.026f, 0.020f, 0.014f, 0.010f, 0.006f };
                const float steps[8]  = { 2.00f, 1.60f, 1.20f, 0.90f, 0.70f, 0.50f, 0.35f, 0.20f };
                for (int i = 0; i < 8; ++i) {
                    float spread = steps[i] * m_scale;
                    float round  = style.WindowRounding + spread;
                    float off_x = spread * 0.10f;
                    float off_y = spread * 0.80f;
                    ImU32 col = ImGui::ColorConvertFloat4ToU32(ImVec4(sh_rgb.x, sh_rgb.y, sh_rgb.z, alphas[i]));
                    bg->AddRectFilled(ImVec2(win_pos.x - off_x, win_pos.y + off_y),
                                      ImVec2(win_pos.x + win_sz.x + off_x, win_pos.y + win_sz.y + spread + off_y),
                                      col, round);
                }
            }

            bool old = m_fold;
            m_fold = helper.showTitle(old);
            if (m_fold != old) {
                _on_set_fold(m_fold);
            }
            if (m_fold)
                return;

            bool isReduceHeight = false;
            auto* notificationManager = wxGetApp().plater()->get_notification_manager();
            if (notificationManager)
            {
                size_t notificationNum = notificationManager->get_warning_and_error_notification_count();
                if (notificationNum > 0)
                {
                    isReduceHeight = true;
                }
            }


            bool option_folded = helper.showOption(m_showMark, m_showBed, m_showColor);
            if (option_folded)
                return;
            if (m_showColor) {
                ImGui::Dummy(ImVec2(0, 5));
                int ret = DispConfig().combo(view_type_items_str, m_view_type_sel);
                if (ret >= 0) {
                    m_view_type_sel = ret;
                    set_view_type(view_type_items[m_view_type_sel]);
                    m_contentWidth = -1.0f;
                    reset_visible(view_type_items[m_view_type_sel]);
                    refresh_render_paths(false, false);
                    update_moves_slider();
                    wxGetApp().plater()->get_current_canvas3D()->set_as_dirty();
                }

                {
                    if (!m_only_gcode_in_preview) {
                        
                        const bool is_lite_mode = m_is_lite_mode;
                        ImGuiContext&     g            = *GImGui;
                        ImGuiWindow*      window       = g.CurrentWindow;
                        const ImGuiStyle& style        = g.Style;

                        ImGui::SameLine();

                        auto label = _u8L("Lite Mode");
                        auto labelsz = ImGui::CalcTextSize(label.c_str());
                        
                        const float h = ImGui::GetTextLineHeight();
                        ImVec2 switch_size(2.0f * h, h);
                        ImVec2 grap_size(h, h);

						//draw text
                        ImGui::SetCursorPosX(ImGui::GetWindowWidth() - grap_size.x - switch_size.x - labelsz.x - 2);
						ImVec2 p_start = window->DC.CursorPos;
						bool hover_text = ImGui::IsMouseHoveringRect(p_start, ImVec2(p_start.x + labelsz.x, p_start.y + labelsz.y));
						ImVec4 text_color = hover_text ? ImGuiWrapper::COL_CREALITY : ImGui::GetStyleColorVec4(ImGuiCol_Text);
                        ImGui::TextColored(text_color, "%s", label.c_str());

						ImVec2 textPos = ImGui::GetItemRectMin();
                        ImGui::GetWindowDrawList()->AddLine(ImVec2(textPos.x, textPos.y + labelsz.y),
                                                            ImVec2(textPos.x + labelsz.x, textPos.y + labelsz.y),
                                                            ImGui::ColorConvertFloat4ToU32(text_color));

                        if (ImGui::IsItemHovered()) {
                            //ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                            if (ImGui::IsMouseClicked(0)) {
                                wxGetApp().open_browser_with_warning_dialog(wxGetApp().app_config->get("language").find("zh_CN") == 0 ?
                                                                                "https://wiki.creality.com/zh/software/6-0/lite-mode" :
                                                                                "https://wiki.creality.com/en/software/6-0/lite-mode");
                            }
                        }

                        ImGui::SameLine();
                        
						// draw switch
                        ImVec2 cur_pos = window->DC.CursorPos;
                        ImVec2 p_min(cur_pos.x, cur_pos.y + g.Style.ItemSpacing.y);
                        ImVec2 p_max = ImVec2(p_min.x + switch_size.x, p_min.y + switch_size.y);

                        ImVec4 border_color = m_is_dark ? ImVec4(64.0f / 255.0f, 64.0f / 255.0f, 64.0f / 255.0f, 1.0f) : ImVec4(218.0f / 255.0f, 219.0f / 255.0f, 223.0f / 255.0f, 1.0f);
                        ImGui::PushStyleColor(ImGuiCol_Border, border_color);
                        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, is_lite_mode ? 0.0f : 1.0f);

                        ImU32 sw_bg = is_lite_mode ? IM_COL32(8, 122, 51, 255) : IM_COL32_BLACK_TRANS;
                        
                        ImGui::RenderFrame(p_min, p_max, sw_bg, true, h * 0.5f);
                        
                        if (ImGui::IsMouseHoveringRect(p_start, p_max)) {
                            ImGui::PushStyleColor(ImGuiCol_PopupBg, ImGuiWrapper::COL_WINDOW_BACKGROUND);
                            ImGui::PushStyleColor(ImGuiCol_Border, {0, 0, 0, 0});
                            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.00f, 1.00f, 1.00f, 1.00f));
                            
							std::string tooltip = _u8L(
                                "In lite mode, only the essential toolpath data is displayed.\n"
                                "If you need to view internal parameters such as infill, please disable this mode and "
                                "re-slice the model.");
                            ImGuiIO& io = ImGui::GetIO();
                            BOOST_LOG_TRIVIAL(warning) << "GCodeViewer LiteMode tooltip, is_lite_mode=" << (is_lite_mode ? 1 : 0)
                                                       << ", only_gcode_in_preview=" << (m_only_gcode_in_preview ? 1 : 0)
                                                       << ", mouse_x=" << io.MousePos.x << ", mouse_y=" << io.MousePos.y;
                            boost::log::core::get()->flush();
                            ImGui::SetTooltip("%s", tooltip.c_str());
                            /*ImGui::SetTooltip(_u8L("In lite mode, only the essential toolpath data is displayed.\n"
                                                   "If you need to view internal parameters such as infill, please disable this mode and "
                                                   "re-slice the model.")
                                                  .c_str());*/
                            ImGui::PopStyleColor(3);
                        }

                        if (is_lite_mode) {
                            // on at right
                            ImVec2 p = ImVec2(p_min.x + 1.0 * grap_size.x, p_min.y);
                            ImGui::RenderFrame(p, ImVec2(p.x+grap_size.x, p.y+grap_size.y), ImGui::ColorConvertFloat4ToU32(ImGuiWrapper::COL_CREALITY), true, h * 0.5f);
                        } else {
                            // off at left
                            ImU32 fill_col = m_is_dark ? IM_COL32(3, 3, 3, 255) : IM_COL32(204, 204, 204, 255);
                            ImGui::RenderFrame(p_min, ImVec2(p_min.x + grap_size.x, p_min.y + grap_size.y), fill_col, true, h * 0.5f);
                        }
                        
                        bool toggled = ImGui::InvisibleButton("##lite_mode_toggle", switch_size);

                        if (toggled) {
                            bool k = !is_lite_mode;
                            wxGetApp().app_config->set("gcode_preview_lite_mode", (k ? "true" : "false"));
                            wxGetApp().plater()->invalid_slice_result_need_reslice();
                        }


                        ImGui::PopStyleColor();
                        ImGui::PopStyleVar();
                    }
                }

                helper.showColorHeader(m_view_type,m_contentWidth);
                if (m_user_mode != wxGetApp().get_mode()) {
                    update_by_mode(wxGetApp().get_mode());
                    m_user_mode = wxGetApp().get_mode();
                    m_contentWidth = -1.0f;
                }

                //try to show scrollbar in colortable region 
                ImGui::SetCursorPosX(0);

                const ImGuiWindowFlags child_flags =  ImGuiWindowFlags_NoTitleBar |
                                                      ImGuiWindowFlags_NoResize | 
                                                      ImGuiWindowFlags_NoMove |
                                                      ImGuiWindowFlags_NoBackground;
                const ImGuiID          child_id    = ImGui::GetID("color_table");

                float pos_y    = ImGui::GetCursorScreenPos().y;
                float canvas_h = wxGetApp().plater()->get_current_canvas3D()->get_canvas_size().get_height();


                float scale = wxGetApp().plater()->get_current_canvas3D()->get_scale();
                float reduceHeight = isReduceHeight ? GCODE_REDUCE_HEIGHT*scale : 20.0f;

                const bool child_is_visible = ImGui::BeginChild(child_id, ImVec2(-1.0f, canvas_h - pos_y - reduceHeight),
                    false, child_flags);
                
                if (m_bLoaded)
                {
                    helper.showColorTable(
                        m_view_type, m_custom_gcode_per_print_z, m_tools, m_is_lite_mode,
                        [this]() {
                            refresh_render_paths(false, false);
                            update_moves_slider();
                            wxGetApp().plater()->get_current_canvas3D()->set_as_dirty();
                        },
                        [this]() {
                            refresh(*m_gcode_result, wxGetApp().plater()->get_extruder_colors_from_plater_config(m_gcode_result));
                            update_moves_slider();
                            wxGetApp().plater()->get_current_canvas3D()->set_as_dirty();
                        });
                }

#if 0
				//
				std::string version = std::string(PROJECT_VERSION_EXTRA);
                bool is_alpha = boost::algorithm::icontains(version, "alpha");
                bool is_beta = boost::algorithm::icontains(version, "beta");
                if (!m_is_lite_mode && (is_alpha || is_beta)) {
                    ImGui::Text("   Lite Mode Enable:");
                    ImGui::SameLine();
                    char* txt = show_gcode_surface() ? "YES" : "NO";
                    ImGui::Text(txt);
				}
#endif

               ImGui::EndChild();

               
            }

            else if (!m_no_render_path)
            {
                uint64_t line = 0;
                if (!m_sequential_view.gcode_ids.empty()) {
                    const size_t idx = std::min<size_t>(static_cast<size_t>(m_sequential_view.current.last), m_sequential_view.gcode_ids.size() - 1);
                    line = m_sequential_view.gcode_ids[idx];
                    if (line == 0) {
                        // Try to find the closest mapped G-code line id (some moves may not map to a line).
                        const size_t max_scan = 2048;
                        for (size_t j = idx; j > 0 && (idx - j) < max_scan && line == 0; --j)
                            line = m_sequential_view.gcode_ids[j];
                        for (size_t j = idx + 1; j < m_sequential_view.gcode_ids.size() && (j - idx) < max_scan && line == 0; ++j)
                            line = m_sequential_view.gcode_ids[j];
                    }
                }
                if (line == 0)
                    line = 1;
                gcode_window.renderGcode(line, canvas_width, canvas_height, isReduceHeight);
            }
            }, wcfg);

        ImGui::PopStyleColor(4);
    }

#if ENABLE_GCODE_VIEWER_STATISTICS
    render_statistics();
#endif // ENABLE_GCODE_VIEWER_STATISTICS

    //BBS render slider
    if (!is_capture_mode) {
        if (m_moves_slider->render(canvas_width, canvas_height))
            m_layers_slider->switch_one_layer_mode();
        m_layers_slider->render(canvas_width, canvas_height);
    }

#if ENABLE_FILTERED_STRIDE_DIALOG

    ImGui::SetNextWindowPos(ImVec2(canvas_width - 50, 100), ImGuiCond_Always, ImVec2(1, 0));
    ImGuiWrapper& imgui = *wxGetApp().imgui();
    imgui.begin(std::string("Adjust Stride factor"),
                ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);
    imgui.text("Stride factor:");
    ImGui::SameLine();
    // ImGui::SetNextItemWidth(100.0f);
    bool toogle = ImGui::SliderFloat("##Stride slider", &m_stride_factor, 1.0f, 20.0f, "%.3f", ImGuiSliderFlags_None);
    ImGui::Text("Dynamic Stride:%d", get_dynamic_stride(estimate_pixels_of_one_layer()));
    imgui.end();

#endif //ENABLE_FILTERED_STRIDE_DIALOG
}

#if ENABLE_GCODE_VIEWER_STATISTICS
void GCodeViewer::render_statistics()
{
    static const float offset = 275.0f;

    ImGuiWrapper& imgui = *wxGetApp().imgui();

    auto add_time = [&imgui](const std::string& label, int64_t time) {
        imgui.text_colored(ImGuiWrapper::COL_ORANGE_LIGHT, label);
        ImGui::SameLine(offset);
        imgui.text(std::to_string(time) + " ms (" + get_time_dhms(static_cast<float>(time) * 0.001f) + ")");
    };

    auto add_memory = [&imgui](const std::string& label, int64_t memory) {
        auto format_string = [memory](const std::string& units, float value) {
            return std::to_string(memory) + " bytes (" +
                   Slic3r::float_to_string_decimal_point(float(memory) * value, 3)
                    + " " + units + ")";
        };

        static const float kb = 1024.0f;
        static const float inv_kb = 1.0f / kb;
        static const float mb = 1024.0f * kb;
        static const float inv_mb = 1.0f / mb;
        static const float gb = 1024.0f * mb;
        static const float inv_gb = 1.0f / gb;
        imgui.text_colored(ImGuiWrapper::COL_ORANGE_LIGHT, label);
        ImGui::SameLine(offset);
        if (static_cast<float>(memory) < mb)
            imgui.text(format_string("KB", inv_kb));
        else if (static_cast<float>(memory) < gb)
            imgui.text(format_string("MB", inv_mb));
        else
            imgui.text(format_string("GB", inv_gb));
    };

    auto add_counter = [&imgui](const std::string& label, int64_t counter) {
        imgui.text_colored(ImGuiWrapper::COL_ORANGE_LIGHT, label);
        ImGui::SameLine(offset);
        imgui.text(std::to_string(counter));
    };

    imgui.set_next_window_pos(0.5f * wxGetApp().plater()->get_current_canvas3D()->get_canvas_size().get_width(), 0.0f, ImGuiCond_Once, 0.5f, 0.0f);
    ImGui::SetNextWindowSizeConstraints({ 300.0f, 100.0f }, { 600.0f, 900.0f });
    imgui.begin(std::string("GCodeViewer Statistics"), ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize);
    ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());

    if (ImGui::CollapsingHeader("Time")) {
        add_time(std::string("GCodeProcessor:"), m_statistics.results_time);

        ImGui::Separator();
        add_time(std::string("Load:"), m_statistics.load_time);
        add_time(std::string("  Load vertices:"), m_statistics.load_vertices);
        add_time(std::string("  Smooth vertices:"), m_statistics.smooth_vertices);
        add_time(std::string("  Load indices:"), m_statistics.load_indices);
        add_time(std::string("Refresh:"), m_statistics.refresh_time);
        add_time(std::string("Refresh paths:"), m_statistics.refresh_paths_time);
    }

    if (ImGui::CollapsingHeader("OpenGL calls")) {
        add_counter(std::string("Multi GL_LINES:"), m_statistics.gl_multi_lines_calls_count);
        add_counter(std::string("Multi GL_TRIANGLES:"), m_statistics.gl_multi_triangles_calls_count);
        add_counter(std::string("GL_TRIANGLES:"), m_statistics.gl_triangles_calls_count);
        ImGui::Separator();
        add_counter(std::string("Instanced models:"), m_statistics.gl_instanced_models_calls_count);
        add_counter(std::string("Batched models:"), m_statistics.gl_batched_models_calls_count);
    }

    if (ImGui::CollapsingHeader("CPU memory")) {
        add_memory(std::string("GCodeProcessor results:"), m_statistics.results_size);

        ImGui::Separator();
        add_memory(std::string("Paths:"), m_statistics.paths_size);
        add_memory(std::string("Render paths:"), m_statistics.render_paths_size);
        add_memory(std::string("Models instances:"), m_statistics.models_instances_size);
    }

    if (ImGui::CollapsingHeader("GPU memory")) {
        add_memory(std::string("Vertices:"), m_statistics.total_vertices_gpu_size);
        add_memory(std::string("Indices:"), m_statistics.total_indices_gpu_size);
        add_memory(std::string("Instances:"), m_statistics.total_instances_gpu_size);
        ImGui::Separator();
        add_memory(std::string("Max VBuffer:"), m_statistics.max_vbuffer_gpu_size);
        add_memory(std::string("Max IBuffer:"), m_statistics.max_ibuffer_gpu_size);
    }

    if (ImGui::CollapsingHeader("Other")) {
        add_counter(std::string("Travel segments count:"), m_statistics.travel_segments_count);
        add_counter(std::string("Wipe segments count:"), m_statistics.wipe_segments_count);
        add_counter(std::string("Extrude segments count:"), m_statistics.extrude_segments_count);
        add_counter(std::string("Instances count:"), m_statistics.instances_count);
        add_counter(std::string("Batched count:"), m_statistics.batched_count);
        ImGui::Separator();
        add_counter(std::string("VBuffers count:"), m_statistics.vbuffers_count);
        add_counter(std::string("IBuffers count:"), m_statistics.ibuffers_count);
    }

    imgui.end();
}
#endif // ENABLE_GCODE_VIEWER_STATISTICS

void GCodeViewer::log_memory_used(const std::string& label, int64_t additional) const
{
    if (Slic3r::get_logging_level() >= 0) {
        int64_t paths_size = 0;
        int64_t render_paths_size = 0;
        for (const TBuffer& buffer : m_buffers) {
            paths_size += SLIC3R_STDVEC_MEMSIZE(buffer.paths, Path);
            render_paths_size += SLIC3R_STDUNORDEREDSET_MEMSIZE(buffer.render_paths, RenderPath);
            for (const RenderPath& path : buffer.render_paths) {
                render_paths_size += SLIC3R_STDVEC_MEMSIZE(path.sizes, unsigned int);
                render_paths_size += SLIC3R_STDVEC_MEMSIZE(path.offsets, size_t);
            }
        }
        int64_t layers_size = SLIC3R_STDVEC_MEMSIZE(m_layers.get_zs(), double);
        layers_size += SLIC3R_STDVEC_MEMSIZE(m_layers.get_endpoints(), Layers::Endpoints);
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << " " << label << " "
                                 << boost::format("paths_size %1%, render_paths_size %2%,layers_size %3%, additional %4%\n")
            %paths_size %render_paths_size %layers_size %additional;
        BOOST_LOG_TRIVIAL(trace) << label
            << "(" << format_memsize_MB(additional + paths_size + render_paths_size + layers_size) << ");"
            << log_memory_info();
    }
}

ColorRGBA GCodeViewer::option_color(EMoveType move_type) const
{
    switch (move_type)
    {
    case EMoveType::Tool_change:  { return Options_Colors[static_cast<unsigned int>(EOptionsColors::ToolChanges)]; }
    case EMoveType::Color_change: { return Options_Colors[static_cast<unsigned int>(EOptionsColors::ColorChanges)]; }
    case EMoveType::Pause_Print:  { return Options_Colors[static_cast<unsigned int>(EOptionsColors::PausePrints)]; }
    case EMoveType::Custom_GCode: { return Options_Colors[static_cast<unsigned int>(EOptionsColors::CustomGCodes)]; }
    case EMoveType::Retract:      { return Options_Colors[static_cast<unsigned int>(EOptionsColors::Retractions)]; }
    case EMoveType::Unretract:    { return Options_Colors[static_cast<unsigned int>(EOptionsColors::Unretractions)]; }
    case EMoveType::Seam:         { return Options_Colors[static_cast<unsigned int>(EOptionsColors::Seams)]; }
    default:                      { return { 0.0f, 0.0f, 0.0f, 1.0f }; }
    }
}

void GCodeViewer::_on_set_fold(bool fold_value) 
{
    wxGetApp().plater()->get_current_canvas3D()->set_left_panel_fold(GLCanvas3D::CanvasPreview, fold_value);
}

bool GCodeViewer::show_gcode_surface() const { 
	
	//BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << boost::format("verify m_gcode_result begin");
    //boost::log::core::get()->flush();

	if (m_gcode_result) {
        if (m_gcode_result->all_surface_with_shell == false) 
			return false;
	}	

	//BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << boost::format("verify m_gcode_result end");
    //boost::log::core::get()->flush();

	bool full_range   = m_layers_slider->is_higher_at_max() && m_layers_slider->is_lower_at_min() && m_moves_slider->is_higher_at_max();
    bool show_surface = full_range && is_visible(ExtrusionRole::erExternalPerimeter);  //show outer wall

    if (show_surface) {
        bool has_top    = false;
        bool has_bottom = false;
        for (size_t i = 0; i < m_roles.size(); i++) {
            const ExtrusionRole& role = m_roles.at(i);
            if (role == ExtrusionRole::erTopSolidInfill) {
                has_top = true;
            } else if (role == ExtrusionRole::erBottomSurface) {
                has_bottom = true;
            }
        }
	
		// has top and show
        if (has_top) {
            show_surface = is_visible(ExtrusionRole::erTopSolidInfill);
		}

		// has bottom and show
		if (show_surface) {
            if (has_bottom) {
                show_surface = is_visible(ExtrusionRole::erBottomSurface);  
			}
		}
    }

	return show_surface;
}

double GCodeViewer::estimate_pixels_of_one_layer() const
{ 
	auto                      camera        = GUI::wxGetApp().plater()->get_camera();
    auto                      zoom          = camera.get_zoom();
    Transform3d               vier_mat      = camera.get_view_matrix();
    Matrix4d                  vier_proj_mat = camera.get_projection_matrix().matrix() * vier_mat.matrix();
    const std::array<int, 4>& viewport      = camera.get_viewport();

    size_t       layers_num = m_layers.size();
    const double h          = m_paths_bounding_box.max.z();
    const double average_h  = h / layers_num;
    Vec3d target = camera.get_target();
	Vec3d up = camera.get_dir_up();
    Vec3d t = target + up.normalized() * average_h;

    auto  s_min = calc_pt_in_screen(target, vier_proj_mat, viewport[2], viewport[3]);
    auto  s_max = calc_pt_in_screen(t, vier_proj_mat, viewport[2], viewport[3]);
    double dist = (s_min - s_max).norm();

	return dist;
}

int GCodeViewer::get_dynamic_stride(double layer_height_of_pixel) const
{
    if (layer_height_of_pixel <= 0.1) return 2;
    //int s = std::max((int) (m_stride_factor * layer_height_of_pixel), 3);
	int s = std::max((int)ceilf(m_stride_factor * layer_height_of_pixel), 3);
    if (s > 6) {
		return 0;
	}
	return s;
}

inline bool GCodeViewer::should_be_filtered_of_layer(int stride, int layer) const
{
    size_t layers_num = m_layers.size();
    
	if (layers_num == 0 || layer < m_layers_z_range[0] || layer > m_layers_z_range[1] || layer < 0 || layer > layers_num)
        return true;

	if (stride <= 0 || layer == m_layers_z_range[0] || layer == m_layers_z_range[1]) 
		return false;	
	
	if (m_top_surface_layer.find(layer) != m_top_surface_layer.end()) 
		return false;

	//const Camera& camera = wxGetApp().plater()->get_camera();

	
	//if (!camera.is_looking_downward()) {
        /*if ((layer - m_layers_z_range[0]) % stride == 0) {
            return true;
        }*/
 //   } else {
	//	// from top to bottom
        if ((m_layers_z_range[1] - layer) % stride == 0) {
            return true;
        }
	//}
	
	return false;
}

int GCodeViewer::get_layer_index(const Path& path) const 
{
    if (m_layers.empty()) return -1;
    assert(m_layers.size() == m_layers.get_endpoints().size());
    const std::vector<Layers::Endpoints>& endpoints = m_layers.get_endpoints();
    size_t length    = endpoints.size();
    for (size_t i = 0; i < length; i++) {
        const auto& ep = endpoints.at(i);
        if (ep.first <= path.sub_paths.front().first.s_id && path.sub_paths.back().last.s_id <= ep.last) 
			return i;
	}
	return -1;
}

void GCodeViewer::set_fold(bool fold)
{ 
    m_fold = fold;
    _on_set_fold(fold);
}


const std::vector<double>& GCodeViewer::get_filtered_layers_z_offset(const int dynamic_stride)
{
	
	bool valid = m_filter_layer_result.check_valid(dynamic_stride, m_layers_z_range);
    if (valid){
		return m_filter_layer_result.get_layers_z_offset();
	}

	return m_filter_layer_result.rebuild_filter_layers_z_offset(this, dynamic_stride, m_layers_z_range, m_layers);
}

Vec3f GCodeViewer::encode_position(const Vec3f& position) 
{
	return (position - m_paths_bounding_box.min.cast<float>()) * SHORT_TYPE_POSITION_SCALE;
}

bool GCodeViewer::should_enable_memory_optimize(const GCodeProcessorResult& gcode_result)
{
	
#if ENABLE_VECTOR_HYBRID_BACKEND
    bool enable_mem_compress = GUI::wxGetApp().app_config->get_bool("enable_preview_mem_optimize");
    if (enable_mem_compress) {
        // based on current conditions
        /*size_t avail            = Slic3r::available_physical_memory();
        size_t total            = Slic3r::total_physical_memory();
        size_t line_count       = m_moves_count - seams_count;
        size_t estmate_mem_size = (seams_count * 102 + line_count * 30) * sizeof(unsigned short);*/
        // enable_mem_compress     = (estmate_mem_size >= avail * 0.5 && total < 18 * 1024 * 1024 * 1024);
    }

    // Avoid overflow causing rendering issues
    {
        PartPlateList& partplate_list = wxGetApp().plater()->get_partplate_list();
        PartPlate*     current_plate  = partplate_list.get_curr_plate();

        
        BoundingBoxf3 paths_bounding_box = current_plate->get_gcode_path_bounding_box();

        //auto min3  = paths_bounding_box.min;
        //auto max3  = paths_bounding_box.max;
        auto size = paths_bounding_box.size();
        double max_size = std::max(size(0), std::max(size(1), size(2)));
		
        if (max_size > 600.0) {
            enable_mem_compress = false;
        }
    }
    return enable_mem_compress;
#endif

	return false; 
}

} // namespace GUI
} // namespace Slic3r
