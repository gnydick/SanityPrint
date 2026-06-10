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
    if (opt && !opt->values.empty() && !opt->values.front().empty())
        out.emplace_back(param, opt->values.front());
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

    // Only canonical ids (printer-minted, previously adopted) are sent, so all
    // printers converge on one id. Slicer placeholder ids (P-prefixed) are
    // omitted: the printer mints U#### and start_sync adopts it from the
    // upsert response.
    if (!preset.filament_id.empty() && preset.filament_id != "null" && preset.filament_id.front() != 'P')
        p.emplace_back("id", preset.filament_id);

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
    // in kvParam, which makes Pull apply them generically with no further mapping.
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

    m_pull_button = new Button(this, _L("Pull"));
    m_pull_button->SetBackgroundColor(btn_bg_blue);
    m_pull_button->SetTextColor(wxColour("#FFFFFE"));
    m_pull_button->SetMinSize(wxSize(FromDIP(58), FromDIP(24)));
    m_pull_button->SetCornerRadius(FromDIP(12));
    btn_sizer->Add(m_pull_button, 0, wxRIGHT, FromDIP(10));

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
    m_pull_button->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) {
        std::vector<SyncDevice> targets = selected_targets(true);
        if (targets.empty())
            return;
        start_pull(targets);
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

// A slicer-minted id (P-prefixed) is a placeholder; canonical ids are minted by
// the printer (U####) and adopted by the slicer on first successful push.
static bool is_placeholder_id(const std::string &id)
{
    return id.empty() || id == "null" || id.front() == 'P';
}

// POST one material to one device. Returns the canonical id from the response
// (empty on failure).
static std::string post_material(const SyncDevice &dev, const std::vector<std::pair<std::string, std::string>> &params,
                                 const std::string &preset_name)
{
    std::string url = "http://" + dev.address + ":" + std::to_string(MATERIAL_API_PORT) + "/server/material";
    char        sep = '?';
    for (const auto &kv : params) {
        url += sep + kv.first + "=" + Http::url_encode(kv.second);
        sep = '&';
    }

    std::string canonical_id;
    bool        ok = false;
    Http        http = Http::post(url);
    // A non-empty body is required: Http::priv::http_perform installs a file
    // read callback unconditionally, and a body-less POST makes libcurl invoke
    // it with a garbage userp (hard crash). All request data is in the query
    // string; the printer ignores the body.
    http.set_post_body(std::string("{}"));
    http.timeout_connect(5)
        .timeout_max(15)
        .on_complete([&canonical_id, &ok](std::string body, unsigned status) {
            if (status != 200)
                return;
            ok = true;
            try {
                nlohmann::json j = nlohmann::json::parse(body);
                if (j.contains("result") && j["result"].contains("id"))
                    canonical_id = j["result"]["id"].get<std::string>();
            } catch (...) {}
        })
        .on_error([&preset_name, &dev](std::string /*body*/, std::string error, unsigned status) {
            BOOST_LOG_TRIVIAL(error) << "FilamentSync: POST failed for '" << preset_name << "' on " << dev.address
                                     << " status=" << status << " error=" << error;
        })
        .perform_sync();
    return ok ? (canonical_id.empty() ? std::string("?") : canonical_id) : std::string();
}

void FilamentSyncDialog::start_sync(const std::vector<SyncDevice> &targets)
{
    auto payloads = std::make_shared<std::vector<MaterialPayload>>(collect_custom_filament_payloads());
    auto devices  = std::make_shared<std::vector<SyncDevice>>(targets);

    boost::thread([payloads, devices]() {
        int         ok_count   = 0;
        int         fail_count = 0;
        std::string failures;
        // preset name -> printer-minted canonical id to adopt
        auto reconciled = std::make_shared<std::map<std::string, std::string>>();

        try {
            for (MaterialPayload &payload : *payloads) {
                // Find the current id param (set when the preset already has a
                // canonical id; absent for placeholders).
                auto id_it = std::find_if(payload.params.begin(), payload.params.end(),
                                          [](const auto &kv) { return kv.first == "id"; });
                bool have_canonical = id_it != payload.params.end();

                for (const SyncDevice &dev : *devices) {
                    std::string got_id = post_material(dev, payload.params, payload.preset_name);
                    if (got_id.empty()) {
                        ++fail_count;
                        if (failures.size() < 600)
                            failures += "\n" + payload.preset_name + " -> " + dev.name;
                        continue;
                    }
                    ++ok_count;
                    // First successful response of a placeholder push: adopt the
                    // printer-minted id and include it for the remaining devices,
                    // so all printers converge on one id.
                    if (!have_canonical && got_id != "?") {
                        payload.params.emplace_back("id", got_id);
                        have_canonical = true;
                        (*reconciled)[payload.preset_name] = got_id;
                    }
                }
            }
        } catch (const std::exception &e) {
            BOOST_LOG_TRIVIAL(error) << "FilamentSync: push thread exception: " << e.what();
        }

        wxGetApp().CallAfter([ok_count, fail_count, failures, reconciled]() {
            // Adopt printer-minted ids on the main thread (preset bundle is not
            // thread safe) and persist them.
            int adopted = 0;
            try {
                PresetBundle *bundle = wxGetApp().preset_bundle;
                for (const auto &entry : *reconciled) {
                    Preset *preset = bundle->filaments.find_preset(entry.first, false, true);
                    if (preset && preset->is_user() && is_placeholder_id(preset->filament_id)) {
                        preset->filament_id = entry.second;
                        preset->save(nullptr);
                        ++adopted;
                    }
                }
            } catch (const std::exception &e) {
                BOOST_LOG_TRIVIAL(error) << "FilamentSync: id reconciliation failed: " << e.what();
            }

            wxString msg = wxString::Format(_L("Filament sync finished: %d pushed, %d failed."), ok_count, fail_count);
            if (adopted > 0)
                msg += "\n" + wxString::Format(_L("%d filament(s) adopted printer-assigned ids."), adopted);
            if (fail_count > 0)
                msg += "\n" + from_u8(failures);
            MessageDialog dlg(wxGetApp().mainframe, msg, wxString(SLIC3R_APP_FULL_NAME) + " - " + _L("Sync Filaments"),
                              wxOK | wxCENTRE);
            dlg.ShowModal();
        });
    }).detach();
}

void FilamentSyncDialog::start_pull(const std::vector<SyncDevice> &targets)
{
    auto devices = std::make_shared<std::vector<SyncDevice>>(targets);

    boost::thread([devices]() {
        // material name -> (id, kvParam) ; first device wins on duplicates
        auto pulled = std::make_shared<std::map<std::string, std::pair<std::string, std::map<std::string, std::string>>>>();
        int  fetch_fail = 0;

        try {
            for (const SyncDevice &dev : *devices) {
                std::string url  = "http://" + dev.address + ":" + std::to_string(MATERIAL_API_PORT) + "/server/material";
                bool        ok   = false;
                std::string body_out;
                Http http = Http::get(url);
                http.timeout_connect(5)
                    .timeout_max(15)
                    .on_complete([&ok, &body_out](std::string body, unsigned status) {
                        if (status == 200) { ok = true; body_out = std::move(body); }
                    })
                    .perform_sync();
                if (!ok) { ++fetch_fail; continue; }
                try {
                    nlohmann::json j = nlohmann::json::parse(body_out);
                    for (const auto &mat : j["result"]["materials"]) {
                        if (!mat.contains("base") || !mat["base"].contains("name"))
                            continue;
                        std::string name = mat["base"]["name"].get<std::string>();
                        if (pulled->count(name))
                            continue;
                        std::string id = mat["base"].contains("id") ? mat["base"]["id"].get<std::string>() : "";
                        std::map<std::string, std::string> kv;
                        if (mat.contains("kvParam"))
                            for (auto it = mat["kvParam"].begin(); it != mat["kvParam"].end(); ++it)
                                if (it.value().is_string())
                                    kv[it.key()] = it.value().get<std::string>();
                        (*pulled)[name] = {id, kv};
                    }
                } catch (const std::exception &e) {
                    BOOST_LOG_TRIVIAL(error) << "FilamentSync: pull parse failed for " << dev.address << ": " << e.what();
                    ++fetch_fail;
                }
            }
        } catch (const std::exception &e) {
            BOOST_LOG_TRIVIAL(error) << "FilamentSync: pull thread exception: " << e.what();
        }

        wxGetApp().CallAfter([pulled, fetch_fail]() {
            int updated = 0, unmatched = 0, stock = 0;
            std::string unmatched_names;
            try {
                PresetBundle *bundle = wxGetApp().preset_bundle;
                for (const auto &entry : *pulled) {
                    Preset *preset = bundle->filaments.find_preset(entry.first, false, true);
                    if (!preset || !preset->is_user()) {
                        // Only user-managed catalog rows are interesting: printer-minted
                        // (U####) or slicer-pushed (P####) ids. Everything else is the
                        // printer's built-in stock database, which the slicer already
                        // mirrors as system presets.
                        const std::string &id = entry.second.first;
                        if (id.empty() || (id.front() != 'U' && id.front() != 'P')) {
                            ++stock;
                            continue;
                        }
                        ++unmatched;
                        if (unmatched_names.size() < 400)
                            unmatched_names += "\n" + entry.first;
                        continue;
                    }
                    // kvParam keys are literal slicer config keys.
                    ConfigSubstitutionContext ctx(ForwardCompatibilitySubstitutionRule::EnableSilent);
                    for (const auto &kv : entry.second.second) {
                        std::string value = kv.second;
                        // Empty values would erase existing settings; gcode-registered
                        // materials cannot carry '#' so colors may arrive bare.
                        if (value.empty())
                            continue;
                        if (kv.first == "default_filament_colour" && value.front() != '#' && value.size() == 6 &&
                            value.find_first_not_of("0123456789abcdefABCDEF") == std::string::npos)
                            value = "#" + value;
                        try {
                            preset->config.set_deserialize(kv.first, value, ctx);
                        } catch (...) {}
                    }
                    if (!entry.second.first.empty() && is_placeholder_id(preset->filament_id))
                        preset->filament_id = entry.second.first;
                    preset->save(nullptr);
                    ++updated;
                }
            } catch (const std::exception &e) {
                BOOST_LOG_TRIVIAL(error) << "FilamentSync: pull apply failed: " << e.what();
            }

            wxString msg = wxString::Format(_L("Pull finished: %d preset(s) updated."), updated);
            if (unmatched > 0)
                msg += "\n" + wxString::Format(_L("%d custom material(s) exist only on the printer (no matching preset):"), unmatched)
                     + from_u8(unmatched_names);
            if (stock > 0)
                msg += "\n" + wxString::Format(_L("%d stock catalog material(s) ignored."), stock);
            if (fetch_fail > 0)
                msg += "\n" + wxString::Format(_L("%d printer(s) could not be reached."), fetch_fail);
            MessageDialog dlg(wxGetApp().mainframe, msg, wxString(SLIC3R_APP_FULL_NAME) + " - " + _L("Sync Filaments"),
                              wxOK | wxCENTRE);
            dlg.ShowModal();
        });
    }).detach();
}

}} // namespace Slic3r::GUI
