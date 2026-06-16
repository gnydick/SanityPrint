#include "DataType.hpp"

#include <sstream>
namespace DM{

    template<typename T>
    T safe_get_json_field(
        const nlohmann::json& data, std::string field_name, nlohmann::json::value_t expect_type, const T& default_val, bool throwerr = false)
    {
        if (!data.contains(field_name))
            return default_val;

        // if the type matches, return directly
        if (data[field_name].type() == expect_type) {
            try {
                return data[field_name].get<T>();
            } catch (std::exception&) {
                if (throwerr)
                    throw;
                return default_val;
            }
        }

        // handle conversion between numeric types (int ↔ float ↔ uint)
        auto is_num = [](nlohmann::json::value_t type) {
            return (type == nlohmann::json::value_t::number_integer || type == nlohmann::json::value_t::number_float ||
                    type == nlohmann::json::value_t::number_unsigned);
        };

        if (is_num(expect_type) && is_num(data[field_name].type())) {
            // numeric types can be converted between each other
            return data[field_name].get<T>();
        }

        // handle string ↔ number conversion
        if constexpr (std::is_same_v<T, std::string>) {
            // T is string, but the JSON may be number or bool
            if (data[field_name].is_number()) {
                return std::to_string(data[field_name].get<double>());
            } else if (data[field_name].is_boolean()) {
                return data[field_name].get<bool>() ? "true" : "false";
            } else if (data[field_name].is_string()) {
                return data[field_name].get<std::string>();
            }
        } else if constexpr (std::is_arithmetic_v<T>) { // T is a numeric type such as int/double
            // T is a number, but the JSON may be string or bool
            if (data[field_name].is_string()) {
                std::istringstream iss(data[field_name].get<std::string>());
                T                  output;
                if (iss >> output) {
                    return output;
                }
            } else if (data[field_name].is_boolean()) {
                return data[field_name].get<bool>() ? static_cast<T>(1) : static_cast<T>(0);
            }
        }

        if (throwerr)
            throw std::runtime_error("Failed to convert JSON field: " + field_name);
        return default_val;
    }

    bool DM::Material::operator==(const DM::Material& other) const
    {
        return  type == other.type && color == other.color;
    }

    // only used for compare in the param filament panel update
    bool DM::Material::operator!=(const DM::Material& other) const
    {
        return !(*this == other);
    }

    // only used for compare in the param filament panel update
    bool DM::MaterialBox::operator==(const DM::MaterialBox& other) const
    {
        return box_id == other.box_id && materials == other.materials;
    }

    // only used for compare in the param filament panel update
    bool DM::MaterialBox::operator!=(const DM::MaterialBox& other) const
    {
        return !(*this == other);
    }

    DM::Device Device::deserialize(nlohmann::json& device, bool need_update_box_info)
    {
        using namespace nlohmann;
        using jvalue = json::value_t;
        DM::Device data;
        try{
            if (!device.empty())
            {
                data.valid = true;

                data.mac = safe_get_json_field(device, "mac", jvalue::string, std::string(""));
                data.address = safe_get_json_field(device, "address", jvalue::string, std::string(""));
                data.model       = safe_get_json_field(device, "model", jvalue::string, std::string(""));
                data.online      = safe_get_json_field(device, "online", jvalue::boolean, false);
                data.deviceState = safe_get_json_field(device, "deviceState", jvalue::number_integer, 0);
                data.name        = safe_get_json_field(device, "name", jvalue::string, std::string(""));
                
                data.deviceType         = safe_get_json_field(device, "deviceType", jvalue::number_integer, 0);
                data.isCurrentDevice    = safe_get_json_field(device, "isCurrentDevice", jvalue::boolean, false);
                data.webrtcSupport      = safe_get_json_field(device, "webrtcSupport", jvalue::number_integer, 0) == 1? true:false;
                data.tbId               = safe_get_json_field(device, "tbId", jvalue::string, std::string(""));
                data.modelName          = safe_get_json_field(device, "modelName", jvalue::string, std::string(""));
                data.isMultiColorDevice = safe_get_json_field(device, "IsMultiColorDevice", jvalue::boolean, false);
                data.oldPrinter         = safe_get_json_field(device, "oldPrinter", jvalue::boolean, false);

                if (data.deviceType == 1001){
                    data.apiKey             = safe_get_json_field(device, "apiKey", jvalue::string, std::string(""));
                    data.deviceUI           = safe_get_json_field(device, "deviceUI", jvalue::string, std::string(""));
                    data.hostType           = safe_get_json_field(device, "hostType", jvalue::number_integer, 1);
                    data.caFile             = safe_get_json_field(device, "caFile", jvalue::string, std::string(""));
                    data.ignoreCertRevocation   = safe_get_json_field(device, "ignoreCertRevocation", jvalue::boolean, false);
                }

                if (need_update_box_info)
                {
                    if (device.contains("boxsInfo") && device["boxsInfo"].contains("boxColorInfo")) {
                        for (const auto& box_info : device["boxsInfo"]["boxColorInfo"]) {
                            DM::DeviceBoxColorInfo box_color_info;
                            box_color_info.boxType      = safe_get_json_field((json) box_info, "boxType", jvalue::number_integer, 0, true);
                            box_color_info.color        = safe_get_json_field((json) box_info, "color", jvalue::string, std::string(""), true);
                            box_color_info.boxId        = safe_get_json_field((json) box_info, "boxId", jvalue::number_integer, 0, true);
                            box_color_info.materialId   = safe_get_json_field((json) box_info, "materialId", jvalue::number_integer, 0, true);                            
                            box_color_info.filamentType = safe_get_json_field((json) box_info, "filamentType", jvalue::string,
                                                                              std::string(""), true);
                            box_color_info.filamentName = safe_get_json_field((json) box_info, "filamentName", jvalue::string,
                                                                              std::string(""), true);
                            if (box_info.contains("cId")) {
                                box_color_info.cId = safe_get_json_field((json) box_info, "cId", jvalue::string, std::string(""), true);
                            }
                            data.boxColorInfos.push_back(box_color_info);
                        }

                    }

                    if (device.contains("boxsInfo") && device["boxsInfo"].is_object()) {
                        auto& boxsInfo = device["boxsInfo"];
                        if (boxsInfo.contains("cfsName") && boxsInfo["cfsName"].is_string()) {

                            //MF003(CFS)   MF040(CFSLite)   MF046(CFSMini)   MF049(CFSNano)
                            data.cfsName = boxsInfo["cfsName"].get<std::string>();
                        }
                    }

                    if (device.contains("boxsInfo") && device["boxsInfo"].contains("materialBoxs")) {
                        auto& materialBoxs = device["boxsInfo"]["materialBoxs"];

                        for (const auto& box : materialBoxs) {
                            DM::MaterialBox materialBox;
                            materialBox.box_id    = safe_get_json_field((json) box, "id", jvalue::number_integer, 0);
                            materialBox.box_state = safe_get_json_field((json) box, "state", jvalue::number_integer, 0);
                            materialBox.box_type  = safe_get_json_field((json) box, "type", jvalue::number_integer, 0);
                            if (box.contains("temp")) {
                                materialBox.temp = safe_get_json_field((json) box, "temp", jvalue::number_integer, 0);
                            }
                            if (box.contains("humidity")) {
                                materialBox.humidity = safe_get_json_field((json) box, "humidity", jvalue::number_integer, 0);
                            }

                            if (box.contains("materials")) {
                                for (const auto& material : box["materials"]) {
                                    DM::Material mat;

                                    mat.material_id = safe_get_json_field((json) material, "id", jvalue::number_integer, 0);
                                    mat.vendor      = safe_get_json_field((json) material, "vendor", jvalue::string, std::string(""));
                                    mat.type        = safe_get_json_field((json) material, "type", jvalue::string, std::string(""));
                                    mat.name         = safe_get_json_field((json) material, "name", jvalue::string, std::string(""));
                                    mat.rfid         = safe_get_json_field((json) material, "rfid", jvalue::string, std::string(""));
                                    mat.color        = safe_get_json_field((json) material, "color", jvalue::string, std::string(""));
                                    mat.diameter     = safe_get_json_field((json) material, "diameter", jvalue::number_float, (double) 0.0);
                                    mat.minTemp      = safe_get_json_field((json) material, "minTemp", jvalue::number_integer, 0);
                                    mat.maxTemp      = safe_get_json_field((json) material, "maxTemp", jvalue::number_integer, 0);
                                    mat.pressure     = safe_get_json_field((json) material, "pressure", jvalue::number_float, (double) 0.0);
                                    mat.percent      = safe_get_json_field((json) material, "percent", jvalue::number_integer, 0);
                                    mat.state        = safe_get_json_field((json) material, "state", jvalue::number_integer, 0);
                                    mat.selected     = safe_get_json_field((json) material, "selected", jvalue::number_integer, 0);
                                    mat.editStatus   = safe_get_json_field((json) material, "editStatus", jvalue::number_integer, 0);
                                    mat.userMaterial = safe_get_json_field((json) material, "userMaterial", jvalue::string, std::string(""));
                                    materialBox.materials.push_back(mat);
                                }
                            }

                            data.materialBoxes.push_back(materialBox);
                        }
                    }
                } // end    if (need_update_box_info)
            }
        }
        catch (std::exception &e) {
            BOOST_LOG_TRIVIAL(error) << "DM::Device Device::deserialize;Error:" << e.what();
        }
        
        return data;
    }

    bool Material::compareMaterials(const DM::Material& a, const DM::Material& b)
    {
        return a.type == b.type && a.color == b.color && a.state == b.state && a.editStatus == b.editStatus && a.name == b.name;
    }

    bool MaterialBox::compareMaterialBoxes(const DM::MaterialBox& a, const DM::MaterialBox& b)
    {
        if (a.box_id != b.box_id || a.box_type != b.box_type)
        {
            return false;
        }

        if (a.materials.size() != b.materials.size()) {
            return false;
        }

        for (const auto& materialA : a.materials) {
            auto it = std::find_if(b.materials.begin(), b.materials.end(),
                [&materialA](const DM::Material& materialB) {
                    return materialA.material_id == materialB.material_id;
                });

            if (it == b.materials.end() || !Material::compareMaterials(materialA, *it)) {
                return false;
            }
        }

        return true;
    }

    bool MaterialBox::findAndCompareMaterialBoxes(const std::vector<DM::MaterialBox>& boxesA, const DM::MaterialBox& boxB)
    {
        auto it = std::find_if(boxesA.begin(), boxesA.end(),
            [&boxB](const DM::MaterialBox& boxA) {
                return boxA.box_id == boxB.box_id && boxA.box_type == boxB.box_type;
            });

        if (it == boxesA.end()) {
            return false;
        }

        return compareMaterialBoxes(*it, boxB);
    }

}