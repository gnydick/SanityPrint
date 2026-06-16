#include "GUI_App.hpp"
#include "GUI_Init.hpp"
#include "Tab.hpp"
#if AUTO_CONVERT_3MF
#include "Plater.hpp"
#include "MainFrame.hpp"
#include "GLToolbar.hpp"
#include "AutoConvert3mfMgr.hpp"
#endif

#include "slic3r/Utils/ExportMetas.hpp"
#include "libslic3r/AutomationMgr.hpp"
#include <boost/algorithm/string.hpp>
#include <string>

namespace Slic3r{
namespace GUI {

    void app_export_meta(TabPrint *tab_print, TabFilament *tab_filament, TabPrinter *tab_printer, const std::string& version)
    {
        std::map<std::string, Utils::GroupInfo> group_infos;

        auto add_group = [&group_infos](const std::vector<PageShp>& pages, const std::string& filed) {            
            for (auto& page : pages) {
                for (auto& group : page->m_optgroups) {
                    for (auto& opt : group->opt_map()) {
                        std::string key = opt.first;
                        int pos = key.find_last_of("#");
                        if (pos < key.length() && pos >= 0) {
                            key = key.substr(0, pos);
                        }

                        auto iter = group_infos.find(key);
                        if(iter == group_infos.end())
                        {
                            Utils::GroupInfo info;
                            info.filed = filed;
                            info.main_group = page->title().ToStdString();
                            info.sub_group = group->title.ToStdString();
                            group_infos.insert(std::pair(key, info));
                        }else{
                            continue;
                        }
                    }
                }
            }
        };

        add_group(tab_print->get_pages(), "Profile");
        add_group(tab_filament->get_pages(), "Filament");
        add_group(tab_printer->get_pages(), "Printer");

        Utils::ExportParam param;
        param.translate = false;
        param.version = version;
        Utils::export_metas(group_infos, param);
    }

    void GUI_App::parse_args()
    {
        if(init_params && init_params->argc > 1)
        {
            std::string _arg1 = init_params->argv[1];
            if(boost::algorithm::starts_with(_arg1, "export_meta"))
            {
                std::vector<std::string> strs;
                boost::split(strs, _arg1, boost::is_any_of(";"), boost::token_compress_on);

                std::string version = "error";
                if(strs.size() > 1)
                    version = strs[1];
                
                TabPrint *tab_print = dynamic_cast<TabPrint*>(tabs_list.at(0));
                TabFilament *tab_filament = dynamic_cast<TabFilament*>(tabs_list.at(1));
                TabPrinter *tab_printer = dynamic_cast<TabPrinter*>(tabs_list.at(2));
                app_export_meta(tab_print, tab_filament, tab_printer, version);
            }
        }
        if (init_params && init_params->argc > 2)
        {
#if AUTOMATION_TOOL
            std::string _arg1 = init_params->argv[1];
            if (boost::algorithm::starts_with(_arg1, "automation"))
			{
                if(init_params->argc < 3)
					return;
                // If parameters are valid, run automation directly
                AutomationMgr::set3mfPath(init_params->argv[2]); //
                AutomationMgr::setFuncType(1); // GCode
                // Handle special \"scale\" automation case
                if (boost::algorithm::contains(init_params->argv[2], "scale")) {
                    if (init_params->argc < 4)
                        return;
                    AutomationMgr::set3mfPath(init_params->argv[3]); //
                    AutomationMgr::setFuncType(2);    // GCode  
                }
			}
#endif            
        }
    }

#if AUTO_CONVERT_3MF
    void GUI_App::parse_convert_3mf_args()
    {
            if (init_params && init_params->argc > 3)
            {
        
                std::string _arg1 = init_params->argv[1];
                if (boost::algorithm::starts_with(_arg1, "convert_3mf"))
			    {
                    if(init_params->argc < 4    )
					    return;

                    // check whether the second argument is a directory
                    wxString inputPath = wxString::FromUTF8(init_params->argv[2]);

                    if (wxDirExists(inputPath) && init_params->argc >= 7) {
                        // stl to 3mf
                        // convert_3mf  some_folder  printer_preset  filament_preset  process_preset   output_3mf_filename
                        // for example:
                        // convert_3mf "some_stl_folder"  "Creality K2 Plus 0.4 nozzle"  "Hyper PLA"  "0.20mm Standard"  "outputfile_1.3mf"

                        auto_convert_3mf_mgr.set_printer_preset(init_params->argv[3]);
                        auto_convert_3mf_mgr.set_output_3mf(init_params->argv[6]);

                        auto_convert_3mf_mgr.set_filament_preset(init_params->argv[4]);
                        auto_convert_3mf_mgr.set_process_preset(init_params->argv[5]);

                        auto_convert_3mf_mgr.set_conversion_mode(ConversionMode::STL_TO_3MF);

                    } else {
                        auto_convert_3mf_mgr.set_printer_preset(init_params->argv[3]);
                        auto_convert_3mf_mgr.set_output_3mf(init_params->argv[4]);
                        auto_convert_3mf_mgr.set_conversion_mode(ConversionMode::_3MF_TO_3MF);
                    }
            
                    // start the conversion process
                    auto_convert_3mf_mgr.start_conversion();
                
			} 

        }
    }
#endif 

}
}
