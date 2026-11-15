
#include <iostream>
#include <chrono>
#include <iomanip>
#include <ctime>
#include <filesystem>
#include <functional>
#include <nlohmann/json.hpp>
#include <Windows.h>
#include <algorithm>
#include <sstream>
#include <fstream>
#include <string>
#include <codecvt>
#include <regex>
#include <format>
#include <map>
#include <boost/nowide/fstream.hpp>
#include <boost/filesystem.hpp>

#include "AutomationMgr.hpp"
#include "libslic3r_version.h"
#include "libslic3r.h"
#include "Utils.hpp"

#include "libslic3r/Model.hpp"
#include "libslic3r/ModelObject.hpp"
#include "libslic3r/ModelVolume.hpp"
#include "libslic3r/ModelInstance.hpp"

namespace Slic3r {
AutomationMgr::AutomationType AutomationMgr::g_automationType = AutomationMgr::AutomationType::None;
std::string                   AutomationMgr::g_3mfPath        = "";

bool AutomationMgr::enabled() { return g_automationType != AutomationType::None; }

void AutomationMgr::set3mfPath(const std::string& path) { g_3mfPath = path; }

void AutomationMgr::setFuncType(int type) { g_automationType = (AutomationType) type; }

std::string AutomationMgr::get3mfPath() { return g_3mfPath; }

std::string AutomationMgr::getFileName()
{
    namespace fs = boost::filesystem;
    fs::path    path(g_3mfPath);
    std::string fileName = path.filename().string();
    return fileName;
}

void AutomationMgr::outputLog(const std::string& logContent, const int& logType)
{
    // output log   0: slice log  1: file error log  2:timeout  3:warnnings
    std::string filePath;
    switch (logType) {
    case 0: filePath = Slic3r::data_dir() + "/automation/sliceTime.txt"; break;
    case 1: filePath = Slic3r::data_dir() + "/automation/error.txt"; break;
    case 2: filePath = Slic3r::data_dir() + "/automation/timeout.txt"; break;
    case 3: filePath = Slic3r ::data_dir() + "/automation/warnnings.txt"; break;
    default: break;
    }
    if (!std::filesystem::exists(filePath)) {
        std::filesystem::create_directories(Slic3r::data_dir() + "/automation");
    }
    std::ofstream log_file(filePath, std::ios_base::app);
    if (!log_file.is_open()) {
        std::cerr << "Failed to open the log" << std::endl;
        return;
    }
    std::string        fileName = getFileName();
    std::ostringstream log_stream;
    // output log   0: slice log  1: file error log  2: input file log 3:
    switch (logType) {
    case 0:
        log_stream << logContent << std::endl;
        log_file << log_stream.str() << std::endl;
        break;
    case 1:
        log_stream << "Error File: " << fileName << "  " << logContent;
        log_file << log_stream.str() << std::endl;
        break;
    case 2:
        log_stream << "Slice Timeout File: " << fileName << "  " << logContent;
        log_file << log_stream.str() << std::endl;
        break;
    case 3:
        log_stream << "Warnings File: " << fileName << "  " << logContent;
        log_file << log_stream.str() << std::endl;
        break;
    default: break;
    }
}

std::string AutomationMgr::getCurrentTime()
{
    auto        now        = std::chrono::system_clock::now();
    std::time_t now_time_t = std::chrono::system_clock::to_time_t(now);

    std::tm* timeinfo = std::localtime(&now_time_t);
    timeinfo->tm_hour;

    if (timeinfo->tm_hour >= 24) {
        timeinfo->tm_hour -= 24;
        timeinfo->tm_mday += 1;
    }

    std::ostringstream oss;
    oss << std::put_time(timeinfo, "%Y-%m-%d %H:%M:%S");
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
    oss << "." << std::setfill('0') << std::setw(3) << ms.count();

    return oss.str();
}

// Generate an export file name (avoiding duplicate names)
std::string AutomationMgr::generateUniqueFilename(const std::string& basePath, const std::string& baseName, const std::string& extension)
{
    std::string filename = baseName + extension;
    fs::path    fullPath = fs::path(basePath) / filename;
    int         counter  = 1;

    while (fs::exists(fullPath)) {
        filename = baseName + "_" + std::to_string(counter) + extension;
        fullPath = fs::path(basePath) / filename;
        ++counter;
    }
    return filename;
}

// Export the parameter configuration to JSON
void AutomationMgr::exportPrintConfigToJson(const Model& model, const DynamicPrintConfig& config)
{
    using namespace Slic3r;
    using namespace nlohmann;
    namespace fs = std::filesystem;

    std::string _3mfFileName = getFileName();
    std::string timestap     = getTimeStamp();
    std::string fileName     = Slic3r::data_dir() + "/automation/config/config_" + _3mfFileName + timestap + ".json";
    fs::path    file_path(fileName);
    fs::path    dir = file_path.parent_path();

    if (!dir.empty() && !fs::exists(dir)) {
        fs::create_directories(dir);
    }

    json j;
    auto save_dynamic_config = [](const Slic3r::DynamicConfig& config, json& j) {
        t_config_option_keys     ks = config.keys();
        const Slic3r::ConfigDef* df = config.def();
        for (const t_config_option_key& k : ks) {
            if (!df->has(k))
                continue;
            const ConfigOption* option = config.optptr(k);
            j[k]                       = option->serialize();
        }
    };

    auto save_object_transform = [](ModelObject* object, json& j) {
        int size = (int) object->instances.size();
        for (int i = 0; i < size; ++i) {
            Slic3r::ModelInstance* instance = object->instances.at(i);
            std::stringstream      ss;
            ss.precision(8);
            ss.setf(std::ios::fixed);
            ss << instance->get_matrix().matrix();
            j[std::to_string(i) + "trans"] = ss.str();
        }
    };

    auto save_volume_transform = [](ModelVolume* volume, json& j) {
        std::stringstream ss;
        ss.precision(8);
        ss.setf(std::ios::fixed);
        ss << volume->get_matrix().matrix();
        j["transform"] = ss.str();
    };

    auto save_mesh = [](const TriangleMesh& mesh, json& j) {
        std::stringstream ss;
        ss.precision(8);
        ss.setf(std::ios::fixed);
        for (const Slic3r::Vec3f& v : mesh.its.vertices) {
            ss << v.matrix() << '\n';
        }
        for (const stl_triangle_vertex_indices& f : mesh.its.indices) {
            ss << f.matrix() << '\n';
        }
        j["mesh"] = ss.str();
    };

    {
        json G;
        save_dynamic_config(config, G);
        j["global"] = G;
    }

    {
        json M;
        int  size = (int) model.objects.size();
        for (int i = 0; i < size; ++i) {
            json         MO;
            ModelObject* object = model.objects.at(i);
            save_dynamic_config(object->config.get(), MO);
            save_object_transform(object, MO);
            int vsize = (int) object->volumes.size();
            for (int j = 0; j < vsize; ++j) {
                json         MOV;
                ModelVolume* volume = object->volumes.at(j);
                save_dynamic_config(volume->config.get(), MOV);
                save_volume_transform(volume, MOV);
                MO[std::to_string(j)] = MOV;
            }

            M[std::to_string(i)] = MO;
        }
        j["model"] = M;
    }
    boost::nowide::ofstream c;
    c.open(fileName.c_str(), std::ios::out | std::ios::trunc);
    c << std::setw(4) << j << std::endl;
    c.close();
}

std::string AutomationMgr::getTimeStamp()
{
    auto               now        = std::chrono::system_clock::now();
    auto               now_time_t = std::chrono::system_clock::to_time_t(now);
    std::tm*           timeinfo   = std::localtime(&now_time_t);
    std::ostringstream oss;
    oss << std::put_time(timeinfo, "%Y%m%d_%H%M%S");
    return "_" + oss.str() + "_";
}

void AutomationMgr::endFunction()
{
    if (g_automationType == AutomationType::GCode) {
        _sleep(2000); // Prevent the program from closing too quickly, which would force the thread to close before the file is written
        HANDLE hprocess = GetCurrentProcess();
        TerminateProcess(hprocess, 1);
    }
}
} // namespace Slic3r
