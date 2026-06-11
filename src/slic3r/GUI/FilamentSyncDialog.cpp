#include "FilamentSyncDialog.hpp"

#include "GUI.hpp"
#include "GUI_App.hpp"
#include "I18N.hpp"
#include "MainFrame.hpp"
#include "MsgDialog.hpp"
#include "Widgets/Button.hpp"
#include "Widgets/CheckBox.hpp"
#include "print_manage/PrinterMgr.hpp"
#include "print_manage/data/DataCenter.hpp"
#include "slic3r/Utils/Http.hpp"

#include "libslic3r/Preset.hpp"
#include "libslic3r/PresetBundle.hpp"

#include <boost/log/trivial.hpp>
#include <boost/thread.hpp>

#include <algorithm>
#include <map>
#include <set>

#include <wx/scrolwin.h>
#include <wx/sizer.h>
#include <wx/stattext.h>

namespace Slic3r { namespace GUI {

static constexpr int MATERIAL_API_PORT = 7125;

// ---------------------------------------------------------------------------
// Payload assembly: one preset -> the query params of POST /server/material.
// The param names and their slicer-field meanings come from the printer's
// OpenAPI definition (K2 Plus Local Material Catalog, /server/material).
// ---------------------------------------------------------------------------

struct MaterialPayload
{
    std::string                                      preset_name;
    std::vector<std::pair<std::string, std::string>> params;
};

static void add_string_param(const DynamicPrintConfig &cfg, std::vector<std::pair<std::string, std::string>> &out,
                             const char *param, const char *key)
{
    const auto *opt = cfg.option<ConfigOptionStrings>(key);
    if (!opt || opt->values.empty())
        return;
    const std::string &value = opt->values.front();
    // "\"\"" is the slicer's legacy serialization of an unset color; neither it
    // nor an empty value belongs on the printer.
    if (value.empty() || value == "\"\"")
        return;
    out.emplace_back(param, value);
}

static void add_int_param(const DynamicPrintConfig &cfg, std::vector<std::pair<std::string, std::string>> &out,
                          const char *param, const char *key)
{
    const auto *opt = cfg.option<ConfigOptionInts>(key);
    if (opt && !opt->values.empty())
        out.emplace_back(param, std::to_string(opt->values.front()));
}

static void format_double_param(std::vector<std::pair<std::string, std::string>> &out, const char *param, double value)
{
    std::string s = std::to_string(value);
    s.erase(s.find_last_not_of('0') + 1);
    if (!s.empty() && s.back() == '.')
        s.pop_back();
    out.emplace_back(param, s);
}

static void add_float_param(const DynamicPrintConfig &cfg, std::vector<std::pair<std::string, std::string>> &out,
                            const char *param, const char *key)
{
    const auto *opt = cfg.option<ConfigOptionFloats>(key);
    if (opt && !opt->values.empty())
        format_double_param(out, param, opt->values.front());
}

static void add_percent_param(const DynamicPrintConfig &cfg, std::vector<std::pair<std::string, std::string>> &out,
                              const char *param, const char *key)
{
    const auto *opt = cfg.option<ConfigOptionPercents>(key);
    if (opt && !opt->values.empty())
        format_double_param(out, param, opt->values.front());
}

static void add_bool_param(const DynamicPrintConfig &cfg, std::vector<std::pair<std::string, std::string>> &out,
                           const char *param, const char *key)
{
    const auto *opt = cfg.option<ConfigOptionBools>(key);
    if (opt && !opt->values.empty())
        out.emplace_back(param, opt->values.front() ? "1" : "0");
}

static MaterialPayload payload_from_preset(const Preset &preset)
{
    MaterialPayload payload;
    payload.preset_name = preset.name;
    const DynamicPrintConfig &cfg = preset.config;
    auto &p = payload.params;

    // vendor is the only REQUIRED field of the upsert.
    {
        const auto *vendor = cfg.option<ConfigOptionStrings>("filament_vendor");
        p.emplace_back("vendor", (vendor && !vendor->values.empty() && !vendor->values.front().empty())
                                     ? vendor->values.front()
                                     : std::string("Custom"));
    }
    p.emplace_back("name", preset.name); // upsert identity key

    add_string_param(cfg, p, "type", "filament_type");
    add_string_param(cfg, p, "color", "default_filament_colour");
    add_int_param(cfg, p, "mintemp", "nozzle_temperature_range_low");
    add_int_param(cfg, p, "maxtemp", "nozzle_temperature_range_high");
    add_int_param(cfg, p, "nozzletemp", "nozzle_temperature");
    add_int_param(cfg, p, "nozzletemp1", "nozzle_temperature_initial_layer");
    add_int_param(cfg, p, "bedtemp", "hot_plate_temp");
    add_int_param(cfg, p, "bedtemp1", "hot_plate_temp_initial_layer");
    add_int_param(cfg, p, "coolplatetemp", "cool_plate_temp");
    add_int_param(cfg, p, "coolplatetemp1", "cool_plate_temp_initial_layer");
    add_int_param(cfg, p, "engplatetemp", "eng_plate_temp");
    add_int_param(cfg, p, "engplatetemp1", "eng_plate_temp_initial_layer");
    add_int_param(cfg, p, "texturedplatetemp", "textured_plate_temp");
    add_int_param(cfg, p, "texturedplatetemp1", "textured_plate_temp_initial_layer");
    add_int_param(cfg, p, "chambertemp", "chamber_temperature");
    add_int_param(cfg, p, "softeningtemp", "temperature_vitrification");
    add_float_param(cfg, p, "density", "filament_density");
    add_float_param(cfg, p, "diameter", "filament_diameter");
    add_float_param(cfg, p, "cost", "filament_cost");
    add_percent_param(cfg, p, "shrink", "filament_shrink");
    add_float_param(cfg, p, "flow", "filament_flow_ratio");
    add_float_param(cfg, p, "maxspeed", "filament_max_volumetric_speed");
    add_bool_param(cfg, p, "soluble", "filament_soluble");
    add_bool_param(cfg, p, "support", "filament_is_support");

    // Parameterized material behavior fields (SanityPrint extension). The param
    // names are the literal slicer config keys; the printer stores them verbatim
    // in kvParam.
    add_int_param(cfg, p, "filament_temp_type", "filament_temp_type");
    add_bool_param(cfg, p, "filament_cooling_smart_zone", "filament_cooling_smart_zone");
    add_float_param(cfg, p, "filament_bed_adhesion_strength", "filament_bed_adhesion_strength");
    add_float_param(cfg, p, "filament_thermal_length", "filament_thermal_length");
    add_float_param(cfg, p, "filament_brim_adhesion_coeff", "filament_brim_adhesion_coeff");
    add_float_param(cfg, p, "filament_small_island_threshold", "filament_small_island_threshold");
    add_int_param(cfg, p, "filament_chamber_temp_limit", "filament_chamber_temp_limit");
    add_bool_param(cfg, p, "filament_is_flexible", "filament_is_flexible");

    return payload;
}

static std::vector<MaterialPayload> collect_custom_filament_payloads()
{
    std::vector<MaterialPayload> payloads;
    const PresetBundle *bundle = wxGetApp().preset_bundle;
    if (!bundle)
        return payloads;
    for (const Preset &preset : bundle->filaments) {
        if (preset.is_user())
            payloads.push_back(payload_from_preset(preset));
    }
    return payloads;
}

// ---------------------------------------------------------------------------
// Device enumeration: same json walk the Device screen uses (DataCenter).
// ---------------------------------------------------------------------------

std::vector<SyncDevice> FilamentSyncDialog::collect_devices()
{
    std::vector<SyncDevice> devices;
    try {
        // DeviceMgr is the persisted store behind the Device page; the runtime
        // DataCenter json is only filled once that webview has loaded, so it is
        // consulted just for the online flag.
        std::map<std::string, std::vector<DM::DeviceMgr::Data>> store;
        std::vector<std::string>                                order;
        DM::DeviceMgr::Ins().Get(store, order);
        if (store.empty()) {
            DM::DeviceMgr::Ins().Load();
            DM::DeviceMgr::Ins().Get(store, order);
        }

        std::set<std::string> seen;
        for (const auto &group : store) {
            for (const auto &data : group.second) {
                if (data.address.empty() || !seen.insert(data.address).second)
                    continue;
                SyncDevice dev;
                dev.address = data.address;

                // The persisted store usually has an empty name (and a model code
                // like "F008"); the friendly name shown on the Device page is the
                // printer's live self-reported one from DataCenter. Prefer it,
                // then the persisted name, then the model, then the address.
                DM::Device runtime;
                try {
                    runtime = DM::DataCenter::Ins().get_printer_data(data.address);
                } catch (...) {}
                dev.online = runtime.online;
                if (!runtime.name.empty())
                    dev.name = runtime.name;
                else if (!data.name.empty())
                    dev.name = data.name;
                else if (!runtime.modelName.empty())
                    dev.name = runtime.modelName;
                else if (!data.model.empty())
                    dev.name = data.model;
                else
                    dev.name = data.address;

                devices.push_back(std::move(dev));
            }
        }
    } catch (const std::exception &e) {
        BOOST_LOG_TRIVIAL(error) << "FilamentSync: failed to enumerate devices: " << e.what();
    }
    return devices;
}

// ---------------------------------------------------------------------------
// Dialog
// ---------------------------------------------------------------------------

FilamentSyncDialog::FilamentSyncDialog(wxWindow *parent)
    : wxDialog(parent, wxID_ANY, _L("Sync Filaments to Printers"), wxDefaultPosition, wxDefaultSize,
               wxDEFAULT_DIALOG_STYLE)
{
    SetBackgroundColour(*wxWHITE);
    m_filament_count = collect_custom_filament_payloads().size();

    wxBoxSizer *main_sizer = new wxBoxSizer(wxVERTICAL);

    auto *info = new wxStaticText(
        this, wxID_ANY,
        wxString::Format(_L("Push all custom filaments (%zu) to the selected printers:"), m_filament_count));
    main_sizer->Add(info, 0, wxALL, FromDIP(15));

    const std::vector<SyncDevice> devices = collect_devices();

    // Select all
    {
        wxBoxSizer *row = new wxBoxSizer(wxHORIZONTAL);
        m_select_all    = new ::CheckBox(this);
        row->Add(m_select_all, 0, wxALIGN_CENTER_VERTICAL, 0);
        auto *label = new wxStaticText(this, wxID_ANY, _L("Select all"));
        row->Add(label, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(6));
        main_sizer->Add(row, 0, wxLEFT | wxRIGHT | wxBOTTOM, FromDIP(15));
        m_select_all->Bind(wxEVT_TOGGLEBUTTON, [this](wxCommandEvent &e) {
            for (auto &row : m_device_rows)
                row.first->SetValue(m_select_all->GetValue());
            e.Skip();
        });
    }

    auto *scrolled = new wxScrolledWindow(this, wxID_ANY, wxDefaultPosition,
                                          wxSize(FromDIP(360), FromDIP(std::min<size_t>(devices.size(), 8) * 32 + 8)));
    scrolled->SetScrollRate(0, FromDIP(16));
    scrolled->SetBackgroundColour(*wxWHITE);
    wxBoxSizer *list_sizer = new wxBoxSizer(wxVERTICAL);
    for (const SyncDevice &dev : devices) {
        wxBoxSizer *row = new wxBoxSizer(wxHORIZONTAL);
        auto *checkbox  = new ::CheckBox(scrolled);
        row->Add(checkbox, 0, wxALIGN_CENTER_VERTICAL, 0);
        wxString text = from_u8(dev.name);
        if (dev.name != dev.address)
            text += wxString::Format(" (%s)", from_u8(dev.address));
        if (!dev.online)
            text += _L(" - offline");
        auto *label = new wxStaticText(scrolled, wxID_ANY, text);
        if (!dev.online)
            label->SetForegroundColour(wxColour(150, 150, 155));
        row->Add(label, 0, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(6));
        list_sizer->Add(row, 0, wxALL, FromDIP(4));
        m_device_rows.emplace_back(checkbox, dev);
    }
    if (devices.empty()) {
        auto *none = new wxStaticText(scrolled, wxID_ANY, _L("No printers found. Add printers on the Device page first."));
        none->SetForegroundColour(wxColour(150, 150, 155));
        list_sizer->Add(none, 0, wxALL, FromDIP(8));
    }
    scrolled->SetSizer(list_sizer);
    main_sizer->Add(scrolled, 1, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(15));

    // Buttons
    wxBoxSizer *btn_sizer = new wxBoxSizer(wxHORIZONTAL);
    btn_sizer->AddStretchSpacer(1);

    StateColor btn_bg_blue(std::pair<wxColour, int>(wxColour(26, 111, 163), StateColor::Pressed),
                           std::pair<wxColour, int>(wxColour(93, 173, 226), StateColor::Hovered),
                           std::pair<wxColour, int>(wxColour(46, 134, 193), StateColor::Normal));
    StateColor btn_bg_white(std::pair<wxColour, int>(wxColour(206, 206, 206), StateColor::Pressed),
                            std::pair<wxColour, int>(wxColour(238, 238, 238), StateColor::Hovered),
                            std::pair<wxColour, int>(*wxWHITE, StateColor::Normal));

    m_sync_button = new Button(this, _L("Push"));
    m_sync_button->SetBackgroundColor(btn_bg_blue);
    m_sync_button->SetTextColor(wxColour("#FFFFFE"));
    m_sync_button->SetMinSize(wxSize(FromDIP(58), FromDIP(24)));
    m_sync_button->SetCornerRadius(FromDIP(12));
    btn_sizer->Add(m_sync_button, 0, wxRIGHT, FromDIP(10));

    auto *cancel_button = new Button(this, _L("Cancel"));
    cancel_button->SetBackgroundColor(btn_bg_white);
    cancel_button->SetMinSize(wxSize(FromDIP(58), FromDIP(24)));
    cancel_button->SetCornerRadius(FromDIP(12));
    btn_sizer->Add(cancel_button, 0, 0, 0);

    main_sizer->Add(btn_sizer, 0, wxEXPAND | wxALL, FromDIP(15));

    m_sync_button->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) {
        std::vector<SyncDevice> targets = selected_targets(true);
        if (targets.empty())
            return;
        if (m_filament_count == 0) {
            MessageDialog dlg(this, _L("There are no custom filaments to sync."),
                              wxString(SLIC3R_APP_FULL_NAME) + " - " + _L("Info"), wxOK | wxCENTRE);
            dlg.ShowModal();
            return;
        }
        start_sync(targets);
        EndModal(wxID_OK);
    });
    cancel_button->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { EndModal(wxID_CANCEL); });

    SetSizerAndFit(main_sizer);
    CenterOnParent();
    wxGetApp().UpdateDlgDarkUI(this);
}

std::vector<SyncDevice> FilamentSyncDialog::selected_targets(bool require_any)
{
    std::vector<SyncDevice> targets;
    for (auto &row : m_device_rows)
        if (row.first->GetValue())
            targets.push_back(row.second);
    if (targets.empty() && require_any) {
        MessageDialog dlg(this, _L("Please select at least one printer."),
                          wxString(SLIC3R_APP_FULL_NAME) + " - " + _L("Info"), wxOK | wxCENTRE);
        dlg.ShowModal();
    }
    return targets;
}

// POST one material to one device. Returns true on a 200 response.
static bool post_material(const SyncDevice &dev, const std::vector<std::pair<std::string, std::string>> &params,
                          const std::string &preset_name)
{
    std::string url = "http://" + dev.address + ":" + std::to_string(MATERIAL_API_PORT) + "/server/material";
    char        sep = '?';
    for (const auto &kv : params) {
        url += sep + kv.first + "=" + Http::url_encode(kv.second);
        sep = '&';
    }

    bool ok   = false;
    Http http = Http::post(url);
    // A non-empty body is required: Http::priv::http_perform installs a file
    // read callback unconditionally, and a body-less POST makes libcurl invoke
    // it with a garbage userp (hard crash). All request data is in the query
    // string; the printer ignores the body.
    http.set_post_body(std::string("{}"));
    http.timeout_connect(5)
        .timeout_max(15)
        .on_complete([&ok](std::string /*body*/, unsigned status) { ok = (status == 200); })
        .on_error([&preset_name, &dev](std::string /*body*/, std::string error, unsigned status) {
            BOOST_LOG_TRIVIAL(error) << "FilamentSync: POST failed for '" << preset_name << "' on " << dev.address
                                     << " status=" << status << " error=" << error;
        })
        .perform_sync();
    return ok;
}

void FilamentSyncDialog::start_sync(const std::vector<SyncDevice> &targets)
{
    auto payloads = std::make_shared<std::vector<MaterialPayload>>(collect_custom_filament_payloads());
    auto devices  = std::make_shared<std::vector<SyncDevice>>(targets);

    boost::thread([payloads, devices]() {
        int         ok_count   = 0;
        int         fail_count = 0;
        std::string failures;

        try {
            for (const MaterialPayload &payload : *payloads) {
                for (const SyncDevice &dev : *devices) {
                    if (post_material(dev, payload.params, payload.preset_name)) {
                        ++ok_count;
                    } else {
                        ++fail_count;
                        if (failures.size() < 600)
                            failures += "\n" + payload.preset_name + " -> " + dev.name;
                    }
                }
            }
        } catch (const std::exception &e) {
            BOOST_LOG_TRIVIAL(error) << "FilamentSync: push thread exception: " << e.what();
        }

        wxGetApp().CallAfter([ok_count, fail_count, failures]() {
            wxString msg = wxString::Format(_L("Filament sync finished: %d pushed, %d failed."), ok_count, fail_count);
            if (fail_count > 0)
                msg += "\n" + from_u8(failures);
            MessageDialog dlg(wxGetApp().mainframe, msg, wxString(SLIC3R_APP_FULL_NAME) + " - " + _L("Sync Filaments"),
                              wxOK | wxCENTRE);
            dlg.ShowModal();
        });
    }).detach();
}

}} // namespace Slic3r::GUI
