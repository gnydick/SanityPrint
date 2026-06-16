#include "AccountDeviceMgr.hpp"

#include "slic3r/GUI/I18N.hpp"
#include "nlohmann/json.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "libslic3r/Utils.hpp"

#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/Utils/Http.hpp"
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <string>

using namespace Slic3r;
using namespace Slic3r::GUI;

namespace pt = boost::property_tree;
using json = nlohmann::json;

// Define the static members
std::unique_ptr<AccountDeviceMgr> AccountDeviceMgr::instance;
std::once_flag AccountDeviceMgr::flag;
std::mutex AccountDeviceMgr::file_mutex;

AccountDeviceMgr::AccountDeviceMgr()  
{

}

AccountDeviceMgr::~AccountDeviceMgr() 
{

}

void AccountDeviceMgr::unbind_device(const std::string& device_id)
{
    if (!Slic3r::GUI::wxGetApp().is_login()) {
        return;
    }

    Slic3r::GUI::UserInfo user = Slic3r::GUI::wxGetApp().get_user();
    std::string account_id = user.userId;

    // Remove all data that does not belong to the currently logged-in account_id
    for (auto it = accountDeviceInfos.account_infos.begin(); it != accountDeviceInfos.account_infos.end();) {
        if (it->first != account_id) {
            it = accountDeviceInfos.account_infos.erase(it);
        } else {
            ++it;
        }
    }


    auto account_it = accountDeviceInfos.account_infos.find(account_id);
    if (account_it == accountDeviceInfos.account_infos.end()) {
        return;
    }

    auto& devices = account_it->second.my_devices;
    auto device_it = std::remove_if(devices.begin(), devices.end(),
                                    [&device_id](const DeviceInfo& device) {
                                        return device.device_unique_id == device_id;
                                    });

    if (device_it != devices.end()) {
        devices.erase(device_it, devices.end());
        save_to_file();
    }
}

void AccountDeviceMgr::unbind_device_by_address(const std::string& address)
{
    if (!Slic3r::GUI::wxGetApp().is_login()) {
        return;
    }

    Slic3r::GUI::UserInfo user = Slic3r::GUI::wxGetApp().get_user();
    std::string account_id = user.userId;

    // Remove all data that does not belong to the currently logged-in account_id
    for (auto it = accountDeviceInfos.account_infos.begin(); it != accountDeviceInfos.account_infos.end();) {
        if (it->first != account_id) {
            it = accountDeviceInfos.account_infos.erase(it);
        }
        else {
            ++it;
        }
    }


    auto account_it = accountDeviceInfos.account_infos.find(account_id);
    if (account_it == accountDeviceInfos.account_infos.end()) {
        return;
    }

    auto& devices = account_it->second.my_devices;
    auto device_it = std::remove_if(devices.begin(), devices.end(),
        [&address](const DeviceInfo& device) {
            return device.address == address;
        });

    if (device_it != devices.end()) {
        devices.erase(device_it, devices.end());
        save_to_file();
    }
}

void AccountDeviceMgr::unbind_device_by_group(const std::string& group)
{
    if (!Slic3r::GUI::wxGetApp().is_login()) {
        return;
    }

    Slic3r::GUI::UserInfo user = Slic3r::GUI::wxGetApp().get_user();
    std::string account_id = user.userId;

    // Remove all data that does not belong to the currently logged-in account_id
    for (auto it = accountDeviceInfos.account_infos.begin(); it != accountDeviceInfos.account_infos.end();) {
        if (it->first != account_id) {
            it = accountDeviceInfos.account_infos.erase(it);
        }
        else {
            ++it;
        }
    }


    auto account_it = accountDeviceInfos.account_infos.find(account_id);
    if (account_it == accountDeviceInfos.account_infos.end()) {
        return;
    }

    auto& devices = account_it->second.my_devices;
    auto device_it = std::remove_if(devices.begin(), devices.end(),
        [&group](const DeviceInfo& device) {
            return device.group == group;
        });

    if (device_it != devices.end()) {
        devices.erase(device_it, devices.end());
        save_to_file();
    }
}

void AccountDeviceMgr::clear_all_account_info()
{
    // Clear the current data
    accountDeviceInfos.account_infos.clear();
}

void AccountDeviceMgr::add_to_my_devices(const DeviceInfo& device_info)
{
    if (!Slic3r::GUI::wxGetApp().is_login()) {
        return;
    }

    Slic3r::GUI::UserInfo user = Slic3r::GUI::wxGetApp().get_user();
    std::string account_id = user.userId;

    // Remove all data that does not belong to the currently logged-in account_id
    for (auto it = accountDeviceInfos.account_infos.begin(); it != accountDeviceInfos.account_infos.end();) {
        if (it->first != account_id) {
            it = accountDeviceInfos.account_infos.erase(it);
        } else {
            ++it;
        }
    }

    // Find or create the AccountInfo
    auto& account = accountDeviceInfos.account_infos[account_id];

    // Set account_id (if this is a newly created AccountInfo)
    if (account.account_id.empty()) {
        account.account_id = account_id;
    }

    // Add the device to the account
    add_device_to_account(account, device_info);

    // write into json file
    save_to_file();
}

void AccountDeviceMgr::add_device_to_account(AccountInfo& account, const DeviceInfo& device_info)
{
    // Check whether the device already exists
    auto device_it = std::find_if(account.my_devices.begin(), account.my_devices.end(),
                                  [&device_info](const DeviceInfo& device) {
                                      return device.device_unique_id == device_info.device_unique_id;
                                  });

    // If the device does not exist, add it as a new device
    if (device_it == account.my_devices.end()) {
        account.my_devices.emplace_back(device_info);
    }
}

void AccountDeviceMgr::save_to_file()
{
    try {
        json j;
        for (const auto& account_pair : accountDeviceInfos.account_infos) {
            const auto& account = account_pair.second;
            json account_json;
            account_json["account_id"] = account.account_id;
            for (const auto& device : account.my_devices) {
                json device_json;
                device_json["device_unique_id"] = device.device_unique_id;

                // Add the other device information
                device_json["address"] = device.address;
                device_json["connectType"] = device.connectType;
                device_json["mac"] = device.mac;
                device_json["model"] = device.model;
                device_json["name"] = device.name;
                device_json["group"] = device.group;

                account_json["my_devices"].push_back(device_json);
            }
            j["accounts"].push_back(account_json);
        }

        std::filesystem::path accout_device_file = (std::filesystem::path(get_local_device_dir()) / account_device_json_file())
                                                       .make_preferred();

        // std::string temp_filename = accout_device_file.string();
        std::ofstream temp_file(accout_device_file.string());
        if (!temp_file.is_open()) {
            // throw std::runtime_error("Unable to open temporary file: " + temp_filename);
            return;
        }
        temp_file << j.dump(4); // formatted output, indented with 4 spaces
        temp_file.close();

        sync_to_cloud();

    } catch (const std::exception& e) {
        std::cerr << "Error saving data to JSON file: " << e.what() << std::endl;
        // throw;
        return;
    }
}


void AccountDeviceMgr::sync_to_cloud() 
{
    bool success = false;
    std::filesystem::path accout_device_file = (std::filesystem::path(get_local_device_dir()) / account_device_json_file()).make_preferred();
    std::string setting_id = m_account_device_file_id;
    
    if (setting_id.empty()) {
        std::string base_url = get_cloud_api_url();
        std::string new_setting_id;
        auto        preupload_profile_url = "/api/cxy/v2/slice/profile/user/preUploadProfile";
        Http::set_extra_headers(wxGetApp().get_extra_header());
        Http        http = Http::post(base_url + preupload_profile_url);
        std::string file_type;
        json        j;
        j["md5"]                = get_file_md5(accout_device_file.string());
        j["type"]               = wxGetApp().preset_type_local_device();
        boost::uuids::uuid uuid = boost::uuids::random_generator()();
        http.header("Content-Type", "application/json")
            .header("__CXY_REQUESTID_", to_string(uuid))
            .set_post_body(j.dump())
            .on_complete([&](std::string body, unsigned status) {
                json j = json::parse(body);
                if (j["code"] != 0) {
                    return;
                }
                new_setting_id             = j["result"]["id"];
                auto        lastModifyTime = j["result"]["lastModifyTime"];
                auto        uploadPolicy   = j["result"]["uploadPolicy"];
                std::string OSSAccessKeyId = uploadPolicy["OSSAccessKeyId"];
                std::string Signature      = uploadPolicy["Signature"];
                std::string Policy         = uploadPolicy["Policy"];
                std::string Key            = uploadPolicy["Key"];
                std::string Callback       = uploadPolicy["Callback"];
                std::string Host           = uploadPolicy["Host"];
                Http        http           = Http::post(Host);
                http.form_add("OSSAccessKeyId", OSSAccessKeyId)
                    .form_add("Signature", Signature)
                    .form_add("Policy", Policy)
                    .form_add("Key", Key)
                    .form_add("Callback", Callback)
                    .form_add_file("File", accout_device_file.string(), account_device_json_file())
                    .on_complete([&](std::string body, unsigned status) {
                        success = (status == 200);
                        if (success) {
                            reset_account_device_file_id(new_setting_id);
                        }
                        })
                    .on_error([&](std::string body, std::string error, unsigned status) {

                    })
                    .on_progress([&](Http::Progress progress, bool& cancel) {

                    })
                    .perform_sync();
            })
            .on_error([&](std::string body, std::string error, unsigned status) {

            })
            .on_progress([&](Http::Progress progress, bool& cancel) {

            })
            .perform_sync();

        if (!new_setting_id.empty()) {
            // setting_id           = new_setting_id;
            // result               = 0;
            /*auto update_time_str = values_map[BBL_JSON_KEY_UPDATE_TIME];
            if (!update_time_str.empty())
                update_time = std::atoll(update_time_str.c_str());*/
        } else {
            // result = -1;
        }
    } else {
    
        std::string version_type          = get_vertion_type();
        std::string base_url              = get_cloud_api_url();
        auto        preupload_profile_url = "/api/cxy/v2/slice/profile/user/editProfile";
        Http        http                  = Http::post(base_url + preupload_profile_url);
        Http::set_extra_headers(wxGetApp().get_extra_header());
        std::string file_type;
        json j;
        j["id"]                 = setting_id;
        boost::uuids::uuid uuid = boost::uuids::random_generator()();
                    http.header("Content-Type", "application/json")
                        .header("__CXY_REQUESTID_", to_string(uuid))
                        .set_post_body(j.dump())
                        .on_complete([&](std::string body, unsigned status) {

                        json j = json::parse(body);
                if (j["code"] != 0) {
                    return;
                }
                //auto new_setting_id         = j["result"]["id"];
                auto        lastModifyTime = j["result"]["lastModifyTime"];
                auto        uploadPolicy   = j["result"]["uploadPolicy"];
                std::string OSSAccessKeyId = uploadPolicy["OSSAccessKeyId"];
                std::string Signature      = uploadPolicy["Signature"];
                std::string Policy         = uploadPolicy["Policy"];
                std::string Key            = uploadPolicy["Key"];
                std::string Callback       = uploadPolicy["Callback"];
                std::string Host           = uploadPolicy["Host"];
                Http        http           = Http::post(Host);
                http.form_add("OSSAccessKeyId", OSSAccessKeyId)
                    .form_add("Signature", Signature)
                    .form_add("Policy", Policy)
                    .form_add("Key", Key)
                    .form_add("Callback", Callback)
                    .form_add_file("File", accout_device_file.string(), account_device_json_file())
                    .on_complete([&](std::string body, unsigned status) {
                        success = (status == 200);
                    })
                    .on_error([&](std::string body, std::string error, unsigned status) {

                    })
                    .on_progress([&](Http::Progress progress, bool& cancel) {

                    })
                    .perform_sync();

                            })
            .on_error([&](std::string body, std::string error, unsigned status) {

            })
            .on_progress([&](Http::Progress progress, bool& cancel) {

            })
            .perform_sync();
    }
    

    /*if (success) {
        Slic3r::GUI::show_info(nullptr, _L("success"), _L("success"));
    }*/
}

std::string AccountDeviceMgr::get_local_device_dir() 
{
    return wxGetApp().get_local_device_dir(); 
}

std::string AccountDeviceMgr::account_device_json_file()
{
    return wxGetApp().account_device_json_file(); 
}

void AccountDeviceMgr::load()
{
    try {
        std::filesystem::path accout_device_file = (std::filesystem::path(get_local_device_dir()) / account_device_json_file())
                                                       .make_preferred();

        std::lock_guard<std::mutex> lock(file_mutex);

        // Open the JSON file
        std::ifstream file(accout_device_file);
        if (!file.is_open()) {
            // throw std::runtime_error("Unable to open file: " + accout_device_file.string());
            return;
        }

        // Parse the JSON file
        json j;
        file >> j;
        file.close();

        // Clear the current data
        accountDeviceInfos.account_infos.clear();

        // Load the data into accountDeviceInfos
        for (const auto& account_json : j["accounts"]) {
            AccountInfo account;
            account.account_id = account_json["account_id"].get<std::string>();
            if (account_json.find("my_devices") != account_json.cend()) {
                for (const auto& device_json : account_json["my_devices"]) {
                    DeviceInfo device;
                    device.device_unique_id = device_json["device_unique_id"].get<std::string>();
                    // Load the other device information
                    try {
                        device.address     = device_json.contains("address") ? device_json["address"].get<std::string>() : "";
                        device.connectType = device_json.contains("connectType") ? device_json["connectType"].get<int>() : 3;
                        device.mac         = device_json.contains("mac") ? device_json["mac"].get<std::string>() : "";
                        device.model       = device_json.contains("model") ? device_json["model"].get<std::string>() : "";
                        device.name        = device_json.contains("name") ? device_json["name"].get<std::string>() : "";
                        device.group       = device_json.contains("group") ? device_json["group"].get<std::string>() : "defaultGroup";
                        if(device.group.empty()) {
                            device.group = "defaultGroup";
                        }
                    } catch (const std::exception& e) {
                        std::cout << "field not found, possibly old version " << std::endl;
                    }


                    account.my_devices.push_back(device);
                }
                accountDeviceInfos.account_infos[account.account_id] = account;
            }
        }

        std::cout << "Data successfully loaded from " << accout_device_file.string() << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Error loading data from JSON file: " << e.what() << std::endl;
    }
}

std::string AccountDeviceMgr::get_account_device_bind_json_info()
{
    if (!Slic3r::GUI::wxGetApp().is_login()) {
        return "";
    }

    Slic3r::GUI::UserInfo user = Slic3r::GUI::wxGetApp().get_user();
    std::string account_id = user.userId;

    try {
        load();
    } catch (std::exception& e) {
        accountDeviceInfos.account_infos.clear();
        return "";
    }

    auto account_it = accountDeviceInfos.account_infos.find(account_id);
    if (account_it == accountDeviceInfos.account_infos.end()) {
        return "";
    }

    json device_array = json::array();
    for (const auto& device : account_it->second.my_devices) {
        device_array.push_back(device.device_unique_id);
    }

    return device_array.dump();

}

void AccountDeviceMgr::reset_account_device_file_id(const std::string& fid) {
    m_account_device_file_id = fid;
}

std::string AccountDeviceMgr::get_account_device_info_for_printers_init()
{
    try {

    if (!Slic3r::GUI::wxGetApp().is_login()) {
        return "";
    }

    Slic3r::GUI::UserInfo user = Slic3r::GUI::wxGetApp().get_user();
    std::string account_id = user.userId;

    // Check whether an entry exists for this account_id
    if (accountDeviceInfos.account_infos.find(account_id) == accountDeviceInfos.account_infos.end()) {
        return ""; // Return an empty string
    }

    json result;
    result["groups"] = json::array();

    // Iterate over account_infos
    for (const auto& account_pair : accountDeviceInfos.account_infos) {
        const AccountInfo& account = account_pair.second;

        // Iterate over my_devices
        for (const auto& device : account.my_devices) {
            // Find or create the corresponding group
            auto group_it = std::find_if(result["groups"].begin(), result["groups"].end(),
                                         [&device](const json& group) {
                                             return group["group"] == device.group;
                                         });

            if (group_it == result["groups"].end()) {
                // If the group does not exist, create a new one
                json new_group;
                new_group["group"] = device.group;
                new_group["list"] = json::array();
                result["groups"].push_back(new_group);
                group_it = std::prev(result["groups"].end());
            }

            // Add the device information to the corresponding group
            json device_json;
            device_json["address"] = device.address;
            device_json["connectType"] = std::to_string(device.connectType);
            device_json["mac"] = device.mac;
            device_json["model"] = device.model;
            device_json["name"] = device.name;
            group_it->at("list").push_back(device_json);
        }
    }

    return result["groups"].dump();

    } catch (std::exception& e) {
        return "";
    }

    return "";

}
