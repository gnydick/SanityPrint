#include "Check3mfVendor.hpp"
#include <list>
#include <thread>
#include <boost/log/core.hpp>
#include "I18N.hpp"
#include "GUI_App.hpp"
#include "MainFrame.hpp"
#include "ParamsPanel.hpp"
#include "Tab.hpp"
#include "libslic3r/common_header/common_header.h"
#include <expat.h>
#include "PrinterPresetConfig.hpp"
#include "libslic3r/ModelObject.hpp"

namespace Slic3r {
namespace GUI {

Check3mfVendor::Check3mfVendor() {}
Check3mfVendor* Check3mfVendor::getInstance()
{
    static Check3mfVendor instance;
    return &instance;
}

void Check3mfVendor::updateCurPrinterType() {
    const Preset& preset = wxGetApp().preset_bundle->printers.get_selected_preset();
    m_isCurPrinterProject = preset.is_project_embedded;
}

bool Check3mfVendor::check(const std::string& fileName, const std::string& printerSettingId, BusyCursor* busy)
{
    bool bRet = false;
    m_isCreality3mf = false;
    bRet = isCreality3mf(fileName);
    m_isCreality3mf = bRet;
    if (!bRet) {
        m_bNeedSelectPrinterPreset = false;
        if (busy != nullptr)
            busy->reset();
        ChoosePresetDlg dlg((wxWindow*)wxGetApp().mainframe, printerSettingId, m_isCurPrinterProject);
        dlg.ShowModal();
        if (busy != nullptr)
            busy->set();
        m_bNeedSelectPrinterPreset = true;
        m_printerPresetName = dlg.m_printerPresetName;
        m_printerPresetIdx = dlg.m_printerPresetIdx;
    }
    return bRet;
}

bool Check3mfVendor::get3mfConfig(const DynamicPrintConfig& config_loaded, DynamicPrintConfig& new_config_loaded)
{
    bool bRet = false;
    m_new_config_loaded.clear();
    m_process_config.clear();
    m_defaultProcess.clear();
    do {
        bool isCurSelectedPrinter = wxGetApp().preset_bundle->printers.get_selected_preset_name() == m_printerPresetName;
        
        Preset* printerPreset = wxGetApp().preset_bundle->printers.find_preset(m_printerPresetName);
        if (printerPreset == nullptr) {
            break;
        }
        std::string vendor;
        if (printerPreset->is_system) {
            if (printerPreset->vendor == nullptr) {
                BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << " vendor null for system printer preset=" << printerPreset->name << " tid=" << std::this_thread::get_id();
                if (auto core = boost::log::core::get())
                    core->flush();
                vendor = "Creality";
            } else {
                vendor = printerPreset->vendor->id;
            }
        } else {
            Preset* parentPreset = wxGetApp().preset_bundle->printers.find_preset(printerPreset->inherits());
            if (parentPreset != nullptr) {
                if (parentPreset->vendor == nullptr) {
                    BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << " vendor null for parent printer preset=" << parentPreset->name << " child=" << printerPreset->name << " tid=" << std::this_thread::get_id();
                    if (auto core = boost::log::core::get())
                        core->flush();
                    vendor = "Creality";
                } else {
                    vendor = parentPreset->vendor->id;
                }
            } else {
                vendor = "Creality";
            }
        }
        for (auto iter = printerPreset->config.cbegin(); iter != printerPreset->config.cend(); ++iter) {
            new_config_loaded.optptr(iter->first, true)->set(iter->second.get());
        }
        // Use a named option to avoid taking address of a temporary ConfigOptionString.
        ConfigOptionString printer_settings_id_opt(m_printerPresetName);
        new_config_loaded.optptr("printer_settings_id", true)->set(&printer_settings_id_opt);

        std::string defaultFilament = "";
        //if (isCurSelectedPrinter) {
        //    defaultFilament = wxGetApp().preset_bundle->filaments.get_selected_preset_name();
        //} else 
        {
            ConfigOptionStrings* optFilaments = new_config_loaded.option<ConfigOptionStrings>("default_filament_profile", false);
            if (optFilaments != nullptr) {
                auto values = optFilaments->vserialize();
                if (!values.empty()) {
                    defaultFilament = values.front();
                } else {
                    BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << " default_filament_profile empty for printer preset=" << printerPreset->name << " tid=" << std::this_thread::get_id();
                    if (auto core = boost::log::core::get())
                        core->flush();
                }
            }
        }
        Preset* filamentPreset = wxGetApp().preset_bundle->filaments.find_preset(defaultFilament);
        if (filamentPreset == nullptr) {
            for (auto preset : wxGetApp().preset_bundle->filaments.get_presets()) {
                if (!preset.is_visible)
                    continue;
                const ConfigOptionStrings* compatible_printers = preset.config.opt<ConfigOptionStrings>("compatible_printers");
                if (compatible_printers) {
                    for (auto value : compatible_printers->values) {
                        if (value == (printerPreset->is_system ? m_printerPresetName : printerPreset->name)) {
                            filamentPreset = wxGetApp().preset_bundle->filaments.find_preset(preset.name);
                            break;
                        }
                    }
                }
                if (filamentPreset != nullptr) {
                    break;
                }
            }
            if (filamentPreset == nullptr) {
                filamentPreset = wxGetApp().preset_bundle->filaments.find_preset("Default Filament");
                if (filamentPreset == nullptr)
                    break;
            }
        }
        for (auto iter = filamentPreset->config.cbegin(); iter != filamentPreset->config.cend(); ++iter) {
            new_config_loaded.optptr(iter->first, true)->set(iter->second.get());
        }
        std::vector<std::pair<std::string,std::string>>   vtPrinterDefaultMaterials;
        wxGetApp().printerPresetConfig->getPrinterDefaultMaterials(vendor, printerPreset->is_system?m_printerPresetName:printerPreset->name, vtPrinterDefaultMaterials);
        //std::sort(vtPrinterDefaultMaterials.begin(), vtPrinterDefaultMaterials.end());
        ConfigOptionStrings* optFilamentsDefault = new_config_loaded.option<ConfigOptionStrings>("default_filament_profile", false);
        const ConfigOptionStrings* filament_settings_id = config_loaded.opt<ConfigOptionStrings>("filament_settings_id");
        if (optFilamentsDefault != nullptr && filament_settings_id != nullptr)
        {
            ConfigOptionStrings* new_filament_settings_id = new_config_loaded.opt<ConfigOptionStrings>("filament_settings_id", true);
            new_filament_settings_id->set(filament_settings_id);
            const ConfigOptionStrings* filament_type = config_loaded.opt<ConfigOptionStrings>("filament_type");
            for (size_t i = 0; i < new_filament_settings_id->values.size(); ++i) {
                auto& item = new_filament_settings_id->values[i];
                if (filament_type != nullptr && i < filament_type->values.size()) {
                    std::string filamentValue;
                    for (auto& item2 : vtPrinterDefaultMaterials) {
                        if (item2.second == filament_type->values[i]) {
                            if (item2.first.find("@") != std::string::npos) {
                                filamentValue = item2.first;
                            } else {
                                filamentValue = item2.first + " @" + (printerPreset->is_system ? m_printerPresetName : printerPreset->name);
                            }
                            break;
                        }
                    }
                    if (filamentValue.empty()) {
                        if (vtPrinterDefaultMaterials.empty()) {
                            filamentValue = filamentPreset->name;
                        } else {
                            Preset* filamentPreset2 = nullptr;
                            for (auto preset : wxGetApp().preset_bundle->filaments.get_presets()) {
                                //if (!preset.is_visible)
                                //    continue;
                                const ConfigOptionStrings* compatible_printers = preset.config.opt<ConfigOptionStrings>("compatible_printers");
                                if (compatible_printers) {
                                    for (auto value : compatible_printers->values) {
                                        if (value == (printerPreset->is_system ? m_printerPresetName : printerPreset->name)) {
                                            const ConfigOptionStrings* filament_type2 = preset.config.opt<ConfigOptionStrings>("filament_type");
                                            if (filament_type2 != nullptr && filament_type2->values[0] == filament_type->values[i]) {
                                                filamentPreset2 = wxGetApp().preset_bundle->filaments.find_preset(preset.name);
                                                break;
                                            }
                                        }
                                    }
                                }
                                if (filamentPreset2 != nullptr) {
                                    break;
                                }
                            }
                            if (filamentPreset2 != nullptr) {
                                filamentValue = filamentPreset2->name;
                            } else {
                                filamentValue = optFilamentsDefault->values[0];
                                if (filamentValue.find("@") == std::string::npos) {
                                    filamentValue = filamentValue + " @" +
                                                    (printerPreset->is_system ? m_printerPresetName : printerPreset->name);
                                }
                            }
                        }
                    }
                    item = filamentValue;
                } else {
                    item = optFilamentsDefault->values[0];
                }
            }
        }
        const ConfigOptionStrings* filament_colour = config_loaded.opt<ConfigOptionStrings>("filament_colour");
        if (filament_colour != nullptr) {
            new_config_loaded.opt<ConfigOptionStrings>("filament_colour", true)->set(filament_colour);
        }
        const ConfigOptionFloats* filament_diameter = config_loaded.opt<ConfigOptionFloats>("filament_diameter");
        if (filament_diameter != nullptr) {
            new_config_loaded.opt<ConfigOptionFloats>("filament_diameter", true)->set(filament_diameter);
        }

        std::string defaultProcess = "";
        if (isCurSelectedPrinter) {
            defaultProcess = wxGetApp().preset_bundle->prints.get_selected_preset_name();
        } else {
            ConfigOptionString* optProcess = new_config_loaded.option<ConfigOptionString>("default_print_profile", false);
            if (optProcess != nullptr) {
                defaultProcess = optProcess->value;
            }
        }
        m_defaultProcess = defaultProcess;
        Preset* processPreset = wxGetApp().preset_bundle->prints.find_preset(defaultProcess);
        if (processPreset == nullptr) {
            for (auto preset : wxGetApp().preset_bundle->prints.get_presets()) {
                if (!preset.is_visible)
                    continue;
                const ConfigOptionStrings* compatible_printers = preset.config.opt<ConfigOptionStrings>("compatible_printers");
                if (compatible_printers) {
                    for (auto value : compatible_printers->values) {
                        if (value == (printerPreset->is_system ? m_printerPresetName : printerPreset->name)) {
                            processPreset = wxGetApp().preset_bundle->prints.find_preset(preset.name);
                            break;
                        }
                    }
                }
                if (processPreset != nullptr) {
                    break;
                }
            }
            if (processPreset == nullptr) {
                processPreset = wxGetApp().preset_bundle->prints.find_preset("Default Setting");
                if (processPreset == nullptr)
                    break;
            }
        }
        const ConfigOptionStrings* different_settings_to_system = config_loaded.opt<ConfigOptionStrings>("different_settings_to_system");
        std::set<std::string> setDiff;
        if (different_settings_to_system != nullptr) {
            for (auto item : different_settings_to_system->values) {
                std::vector<std::string> vtDiff;
                boost::split(vtDiff, item, boost::is_any_of(";"));
                setDiff.insert(vtDiff.begin(), vtDiff.end());
            }
        }
        bool has_enable_support_diff = false;
        for (auto iter = processPreset->config.cbegin(); iter != processPreset->config.cend(); ++iter) {
            auto opt = config_loaded.optptr(iter->first);
            if (opt != nullptr) {
                if (iter->second->type() == opt->type()) {
                    if (setDiff.find(iter->first) != setDiff.end()) {
                        new_config_loaded.optptr(iter->first, true)->set(opt);
                        m_process_config.optptr(iter->first, true)->set(opt);
                        if (iter->first == "enable_support") {
                            has_enable_support_diff = true;
                            auto support_type_opt   = config_loaded.optptr("support_type");
                            if (support_type_opt != nullptr) {
                                new_config_loaded.optptr("support_type", true)->set(support_type_opt);
                                m_process_config.optptr("support_type", true)->set(support_type_opt);
                            }
                        }
                    } else {
                        if (has_enable_support_diff && iter->first == "support_type")
                            continue;
                        new_config_loaded.optptr(iter->first, true)->set(iter->second.get());
                        m_process_config.optptr(iter->first, true)->set(iter->second.get());
                    }
                }
            } else {
                new_config_loaded.optptr(iter->first, true)->set(iter->second.get());
                m_process_config.optptr(iter->first, true)->set(iter->second.get());
            }
        }
        ConfigOptionString print_settings_id_opt(processPreset->name);
        new_config_loaded.optptr("print_settings_id", true)->set(&print_settings_id_opt);

        bRet = true;
        m_new_config_loaded = new_config_loaded;
    } while (0);
    return bRet;
}

void Check3mfVendor::doSelectPrinterPreset()
{
    if (m_bNeedSelectPrinterPreset) {
        //if (m_printerPresetIdx != wxGetApp().plater()->sidebar_printer().get_selection_combo_printer()) {
        //    wxGetApp().plater()->sidebar_printer().select_printer_preset(m_printerPresetName, m_printerPresetIdx);
        //}
        Preset* processPreset = wxGetApp().preset_bundle->prints.find_preset(m_defaultProcess);
        if (processPreset != nullptr) {
            std::vector<std::string> dirty_options = processPreset->config.diff(m_process_config);
            if (dirty_options.size() > 0) {
                for (auto iter = m_process_config.cbegin(); iter != m_process_config.cend(); ++iter) {
                    processPreset->config.optptr(iter->first, true)->set(iter->second.get());
                }
                wxGetApp().preset_bundle->prints.get_selected_preset().set_dirty();
                wxGetApp().preset_bundle->prints.get_edited_preset().set_dirty();
            }
        }
    }
    m_bNeedSelectPrinterPreset = false;
}

bool Check3mfVendor::isCreality3mf() {
    return m_isCreality3mf;
}

void Check3mfVendor::setCreality3mf(bool isCreality3mf) {
    m_isCreality3mf = isCreality3mf;
}

void Check3mfVendor::updatePlateObject(const PlateDataPtrs& plate_data, const Slic3r::Model& model)
{
    m_vtPlateObject.clear();
    std::vector<int> obj_idxs;
    for (int i = 0; i < plate_data.size(); ++i) {
        obj_idxs.clear();
        for (auto item : plate_data[i]->obj_inst_map) {
            for (int k = 0; k < model.objects.size(); k++) {
                // if (item.second.second == model.objects[k]->instances[0]->loaded_id)
                if (item.first == model.objects[k]->from_loaded_id) {
                    obj_idxs.emplace_back(k);
                }
            }
        }
        m_vtPlateObject.emplace_back(obj_idxs);
    }
}
void Check3mfVendor::centerModelToPlate(View3D* view3D, Sidebar* sidebar)
{
    if (!m_isCreality3mf)
    {
        for (int i = 0; i < m_vtPlateObject.size(); ++i) {
            view3D->select_object_from_idx(m_vtPlateObject[i]);
            sidebar->obj_list()->update_selections();
            view3D->center_selected_plate(i);
        }
        m_vtPlateObject.clear();
    }
}

bool Check3mfVendor::isCreality3mf(const std::string& fileName)
{
    bool bRet = false;

    mz_zip_archive archive;
    mz_zip_zero_struct(&archive);
    do {
        if (!open_zip_reader(&archive, fileName)) {
            break;
        }
        // 1) Check whether Metadata/creality.config exists to mark it as Creality 3MF.
        // 2) If not, check whether Creality appears in 3D/3dmodel.model.
        const char* crealityConfig = "Metadata/creality.config";
        int file_index = mz_zip_reader_locate_file(&archive, crealityConfig, nullptr, 0);
        if (file_index < 0) {
            if (!isCrealityIn3dModel(&archive)) {
                break;
            }
        }
        bRet = true;
    } while (0);
    close_zip_reader(&archive);

    return bRet;
}

static bool isApplicationField = false;
static void startTag(void* userData, const char* name, const char** atts)
{
    if (strcmp(name, "metadata") == 0) {
        for (int i = 0; atts[i]; i += 2) {
            if (strcmp(atts[i], "name") == 0 && strcmp(atts[i + 1], "Application") == 0) {
                isApplicationField = true;
            }
        }
    }
}
static void endTag(void* userData, const char* name)
{
    if (strcmp(name, "metadata") == 0) {
        if (isApplicationField) {
            isApplicationField = false;
        }
    }
}
static void characterData(void* userData, const XML_Char* s, int len)
{
    Check3mfVendor* data = (Check3mfVendor*) userData;
    if (isApplicationField) {
        std::string itemValue(s, len);
        if (itemValue.find("Creality") != std::string::npos) {
            //isCreality3mf = true;
            data->setCreality3mf(true);
        }
    }
}

bool Check3mfVendor::isCrealityIn3dModel(mz_zip_archive* pArchive)
{
    bool bRet = false;
    isApplicationField = false;

    const std::string model_file       = "3D/3dmodel.model";
    int               model_file_index = mz_zip_reader_locate_file(pArchive, model_file.c_str(), nullptr, 0);
    if (model_file_index != -1) {
        int depth = 0;
        XML_Parser m_parser;
        do {
            m_parser  = XML_ParserCreate(nullptr);
            XML_SetUserData(m_parser, (void*)this);
            XML_SetElementHandler(m_parser, startTag, endTag);
            XML_SetCharacterDataHandler(m_parser, characterData);

            mz_zip_archive_file_stat stat;
            if (!mz_zip_reader_file_stat(pArchive, model_file_index, &stat))
            break;

            void* parser_buffer = XML_GetBuffer(m_parser, (int)stat.m_uncomp_size);
            if (parser_buffer == nullptr)
            break;

            mz_bool res = mz_zip_reader_extract_file_to_mem(pArchive, stat.m_filename, parser_buffer, (size_t)stat.m_uncomp_size, 0);
            if (res == 0)
            break;

            XML_ParseBuffer(m_parser, (int)stat.m_uncomp_size, 1);

            bRet = m_isCreality3mf;
        } while (0);
        XML_ParserFree(m_parser);
    }
    return bRet;
}

ChoosePresetDlg::ChoosePresetDlg(wxWindow* parent, const std::string& printerSettingId, bool isCurPrinterProject)
    : DPIDialog(parent ? parent : nullptr, wxID_ANY, _L("Tips"), wxDefaultPosition, wxDefaultSize, wxCAPTION | wxCLOSE_BOX)
{
    std::string icon_path = (boost::format("%1%/images/%2%.ico") % resources_dir() % Slic3r::CxBuildInfo::getIconName()).str();
    this->SetIcon(wxIcon(encode_path(icon_path.c_str()), wxBITMAP_TYPE_ICO));

    this->SetBackgroundColour(*wxWHITE);
    this->SetMinSize(wxSize(FromDIP(600), FromDIP(280)));
    this->SetMaxSize(wxSize(FromDIP(600), FromDIP(280)));
    wxBoxSizer* main_sizer = new wxBoxSizer(wxVERTICAL);
    wxStaticText* tip = new wxStaticText(this, wxID_ANY, _L("This project file is not from Sanity Print. Please select the printer preset you want to use for slicing."));
    tip->SetMinSize(wxSize(FromDIP(520), FromDIP(44)));
    tip->SetMaxSize(wxSize(FromDIP(520), FromDIP(44)));
    main_sizer->AddSpacer(FromDIP(20));
    main_sizer->Add(tip, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(40));
    m_combo = new ::ComboBox(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(), 0, nullptr, wxCB_READONLY);
    m_combo->SetMinSize(wxSize(FromDIP(520), FromDIP(40)));
    m_combo->SetMaxSize(wxSize(FromDIP(520), FromDIP(40)));
    m_combo->Bind(wxEVT_COMBOBOX, &ChoosePresetDlg::OnComboboxSelected, this);

    /* std::list<std::string> userPresets;
    std::list<std::string> systemPresets;
    const std::deque<Preset>& presets = wxGetApp().preset_bundle->printers.get_presets();
    for (auto& preset : presets) {
        if (!preset.is_visible)
            continue;
        if (preset.is_user()) {
            userPresets.push_back(preset.name);
        } else if (preset.is_system) {
            systemPresets.push_back(preset.name);
        }
    }
    if (userPresets.size() > 0) {
        m_combo->AppendString("-----" + _L("User presets") + "-----");
        m_vtComboText.push_back("-----User Presets-----");
        for (auto& preset : userPresets) {
            m_combo->AppendString(wxString::FromUTF8(preset.c_str()));
            m_vtComboText.push_back(preset);
        }
    }
    if (systemPresets.size() > 0) {
        m_combo->AppendString("-----" + _L("System presets") + "-----");
        m_vtComboText.push_back("-----System Presets-----");
        for (auto& preset : systemPresets) {
            m_combo->AppendString(wxString::FromUTF8(preset.c_str()));
            m_vtComboText.push_back(preset);
        }
    }*/
    m_vtComboText = wxGetApp().plater()->sidebar_printer().texts_of_combo_printer();
    bool isProjectPreset = false;
    int  firstCrealityPresetIdx = -1;
    int  defaultPrinterSettingId = -1;
    for (int i = 0; i < m_vtComboText.size(); ++i) {
        auto& item = m_vtComboText[i];
        if (wxString::FromUTF8(item) == wxString("------ " + _L("User presets") + " ------") ||
            wxString::FromUTF8(item) == wxString("------ " + _L("System presets") + " ------")) {
            isProjectPreset = false;
        }
        if (wxString::FromUTF8(item) == wxString("------ " + _L("Project-inside presets") + " ------")) {
            isProjectPreset = true;
        }
        if (isProjectPreset) {
            m_projectPresetCount++;
            continue;
        }
        if (wxString::FromUTF8(item) == wxString("------ " + _L("Select/Remove printers(system presets)") + " ------") ||
            wxString::FromUTF8(item) == wxString("------ " + _L("Create Nozzle") + " ------") ||
            wxString::FromUTF8(item) == wxString("------ " + _L("Create printer") + " ------")) {
            continue;
        }

        auto preset = wxGetApp().preset_bundle->printers.find_preset(item);
        std::string presetName = item;
        if (preset == nullptr && item.c_str()[0] == '*') {
            preset = wxGetApp().preset_bundle->printers.find_preset(item.substr(2));
            presetName = item.substr(2);
        }

        // Get index of the first Creality system preset
        if (firstCrealityPresetIdx == -1 && /*item.find("Creality") != std::string::npos*/
            preset != nullptr && preset->is_system && 
            (preset->name.find("Creality") != std::string::npos||preset->name.find("SPARKX") != std::string::npos)) {
            firstCrealityPresetIdx = i;
        }

        // Get index of the printer preset referenced by 3mf file
        if (!printerSettingId.empty() && defaultPrinterSettingId == -1 && presetName == printerSettingId &&
            preset != nullptr && !preset->is_project_embedded){
            defaultPrinterSettingId = i;
        }

        m_combo->AppendString(wxString::FromUTF8(item.c_str()));
        m_combo->SetItemTooltip(m_combo->GetCount() - 1, wxString::FromUTF8(item.c_str()));
    }
    int idx = wxGetApp().plater()->sidebar_printer().get_selection_combo_printer();
    std::string leftSelectedPresetName;
    if (idx >= 0 && idx < m_vtComboText.size()) {
        leftSelectedPresetName = m_vtComboText[idx];
    }

    //if (defaultPrinterSettingId != -1) {//如果找到3mf文件中的预设，则选择该预设
    //    m_comboLastSelected = defaultPrinterSettingId;
    //} else {
    //    auto preset = wxGetApp().preset_bundle->printers.find_preset(leftSelectedPresetName);
    //    if (preset != nullptr && preset->is_system &&
    //        preset->name.find("Creality") != std::string::npos) { // 如果找到3mf文件中的预设，则判断当前选择中的是创想的系统预设
    //        m_comboLastSelected = idx;
    //    } else {
    //        if (firstCrealityPresetIdx != -1) {//如果没有找到3mf文件中的预设，且当前选中的不是创想的系统预设，则选择第一个创想的系统预设
    //            m_comboLastSelected = firstCrealityPresetIdx;
    //        } else { 
    //            if (idx >= 0 && idx < m_vtComboText.size()){
    //                m_comboLastSelected = idx+m_projectPresetCount;
    //            }
    //            else {
    //                m_comboLastSelected = 1+m_projectPresetCount;
    //            }
    //        }
    //    }
    //}

    auto preset = wxGetApp().preset_bundle->printers.find_preset(leftSelectedPresetName);
    if (preset != nullptr && (preset->is_system || preset->is_user())) {
        if (isCurPrinterProject) {
            m_comboLastSelected = firstCrealityPresetIdx;
        } else {
            m_comboLastSelected = idx + m_projectPresetCount;
        }
    } else {
        m_comboLastSelected = firstCrealityPresetIdx;
    }

    m_combo->SetSelection(m_comboLastSelected - m_projectPresetCount);
    m_printerPresetName = m_vtComboText[m_comboLastSelected];
    m_printerPresetIdx  = m_comboLastSelected;
    m_combo->SetToolTip(wxString::FromUTF8(m_printerPresetName));
    main_sizer->Add(m_combo, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(40));
    Button* btnOk = new Button(this, _L("OK"));
    StateColor btn_bg_green(std::pair<wxColour, int>(wxColour(52, 152, 219), StateColor::Pressed),
                            std::pair<wxColour, int>(wxColour(52, 152, 219), StateColor::Hovered),
                            std::pair<wxColour, int>(wxColour(52, 152, 219), StateColor::Normal));

    btnOk->SetBackgroundColor(btn_bg_green);
    btnOk->SetBorderColor(wxColour(255, 255, 255));
    btnOk->SetTextColor(wxColour("#FFFFFE"));
    btnOk->SetMinSize(wxSize(FromDIP(104), FromDIP(32)));
    btnOk->SetMaxSize(wxSize(FromDIP(104), FromDIP(32)));
    btnOk->Bind(wxEVT_LEFT_DOWN, &ChoosePresetDlg::OnOk, this);

    main_sizer->AddSpacer(FromDIP(70));
    main_sizer->Add(btnOk, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(248));

    this->SetSizer(main_sizer);
    wxGetApp().UpdateDlgDarkUI(this);
    Layout();
    Fit();
}

void ChoosePresetDlg::on_dpi_changed(const wxRect& suggested_rect) {}

void ChoosePresetDlg::OnComboboxSelected(wxCommandEvent& evt)
{
    auto selected_item = evt.GetSelection();
    if (selected_item < 0 || (selected_item+m_projectPresetCount) >= m_vtComboText.size()) {
        return;
    }
    static bool hasSelected = false;
    if (wxString::FromUTF8(m_vtComboText[selected_item + m_projectPresetCount]) == wxString("------ " + _L("Project-inside presets") + " ------") ||
        wxString::FromUTF8(m_vtComboText[selected_item + m_projectPresetCount]) == wxString("------ " + _L("User presets") + " ------") ||
        wxString::FromUTF8(m_vtComboText[selected_item + m_projectPresetCount]) == wxString("------ " + _L("System presets") + " ------")) {
        if (!hasSelected)
            m_combo->SetSelection(m_comboLastSelected - m_projectPresetCount);
        else
            m_combo->SetSelection(m_comboLastSelected);
    } else {
        hasSelected = true;
        m_comboLastSelected = selected_item;
        m_printerPresetName = m_vtComboText[selected_item + m_projectPresetCount];
        m_printerPresetIdx  = selected_item + m_projectPresetCount;
        m_combo->SetToolTip(wxString::FromUTF8(m_printerPresetName));
    }
    evt.Skip();
}

void ChoosePresetDlg::OnOk(wxMouseEvent& event) {
    EndModal(wxID_OK);
}

} // namespace GUI
}
