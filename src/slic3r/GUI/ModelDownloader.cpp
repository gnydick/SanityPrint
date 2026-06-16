#include "ModelDownloader.h"
#include "GUI_App.hpp"
#include "NotificationManager.hpp"
#include "format.hpp"
#include "MainFrame.hpp"

#include <boost/algorithm/string.hpp>
#include <boost/log/trivial.hpp>

namespace Slic3r { namespace GUI {

DownloadTask::DownloadTask(std::string                    ID,
                           std::string                    url,
                           const std::string&             filename,
                           const boost::filesystem::path& dest_folder,
                           std::function<void(int)>       progress_cb,
                           std::function<void(std::string)>      complete_cb,
                           std::function<void(void)>      error_cb)
    : m_id(ID), m_filename(filename), m_dest_folder(dest_folder)
{
    assert(boost::filesystem::is_directory(dest_folder));
    m_final_path = dest_folder / m_filename;
    m_file_get   = std::make_shared<DownloadService>(ID, url, m_filename, dest_folder, progress_cb, complete_cb);
}

void DownloadTask::start()
{
    m_state = DownloadState::DownloadOngoing;
    m_file_get->get();
}
void DownloadTask::cancel()
{
    m_state = DownloadState::DownloadStopped;
    m_file_get->cancel();
}
void DownloadTask::pause()
{
    if (m_state != DownloadState::DownloadOngoing)
        return;
    m_state = DownloadState::DownloadPaused;
    m_file_get->pause();
}
void DownloadTask::resume()
{
    if (m_state != DownloadState::DownloadPaused)
        return;
    m_state = DownloadState::DownloadOngoing;
    m_file_get->resume();
}

ModelDownloader::ModelDownloader() {}

ModelDownloader::ModelDownloader(const std::string& user_id) : user_id_(user_id) { init(); }

void ModelDownloader::init()
{
    if (user_id_.empty()) {
        return;
    }
    load_cache_from_storage();
}

void ModelDownloader::start_download_model_group(const std::string& full_url,
                                                 const std::string& modelId,
                                                 const std::string& fileId,
                                                 const std::string& fileFormat,
                                                 const std::string& fileName
                                                 
    )
{
    auto target_path = dest_folder_;
    if (!fs::exists(target_path.append(modelId))) {
        fs::create_directories(target_path);
    }
    auto cache_path  = target_path;
    auto file_path   = cache_path.append(fileName + fileFormat).string();
    auto progress_cb = [&, modelId, fileId, file_path = std::move(file_path)](int progress) {
        std::lock_guard<std::mutex> lock_guard(cache_json_mutex_);
        if (cache_json_.is_object() && cache_json_.contains("models")) {
            for (auto& model : cache_json_["models"]) {
                std::string model_id = model["modelId"];
                if (model_id != modelId) {
                    continue;
                }
                for (auto& file : model["files"]) {
                    std::string file_id = file["fileId"];
                    if (file_id != fileId) {
                        continue;
                    }
                    auto& cache_progress = file["progress"];
                    if (cache_progress < progress) {
                        cache_progress = progress;
                        if (progress == 100) {
                            file["path"]    = file_path;
                            save_cache_to_storage();
                        }
                        
                        return;
                    }
                }
            }
        }
    };
    download_tasks_.emplace_back(std::make_unique<DownloadTask>(modelId, full_url, fileName + fileFormat, target_path, progress_cb));
    download_tasks_.back()->start();
    BOOST_LOG_TRIVIAL(debug) << "started download";
    std::lock_guard<std::mutex> lock_guard(cache_json_mutex_);
    if (cache_json_.is_object() && cache_json_.contains("models")) {
        bool found = false;
        for (auto& model : cache_json_["models"]) {
            std::string model_id = model["modelId"];
            if (model_id == modelId) {
                auto& files_array = model["files"];
                json  object;
                object["fileId"]   = fileId;
                object["progress"] = 0;
                object["path"]     = "";
                files_array.push_back(object);
                found = true;
                break;
            }
        }
        if (!found) {
            auto&          model_array = cache_json_["models"];
            nlohmann::json model_object;
            model_object["createTime"] = time(nullptr);
            model_object["modelId"]    = modelId;
            json files_array;
            json file_object;
            file_object["fileId"]   = fileId;
            file_object["progress"] = 0;
            file_object["path"]     = "";
            files_array.push_back(file_object);
            model_object["files"] = files_array;
            model_array.push_back(model_object);
        }
    } else {
        nlohmann::json model_array;
        nlohmann::json model_object;
        model_object["createTime"] = time(nullptr);
        model_object["modelId"]    = modelId;
        json files_array;
        json file_object;
        file_object["fileId"]   = fileId;
        file_object["progress"] = 0;
        file_object["path"]     = "";
        files_array.push_back(file_object);
        model_object["files"] = files_array;
        model_array.push_back(model_object);
        cache_json_["models"] = model_array;
    }

    save_cache_to_storage();
}

std::string ModelDownloader::filterInvalidFileNameChars(const std::string& input)
{
    std::string result;
    for (char c : input) {
        // Replace invalid characters and non-printable ASCII characters
        if (c == '\\' || c == '/' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|' ||
            static_cast<unsigned char>(c) < 32) {
            result += '-';
        } else {
            result += c;
        }
    }
 
    // Avoid empty file name
    if (result.empty()) {
        result = "unnamed";
    }

    return result;
}
bool endsWith(const std::string& str, const std::string& suffix) {
    if (str.length() < suffix.length()) {
        return false;
    }
    return str.substr(str.length() - suffix.length()) == suffix;
}
void ModelDownloader::start_download_3mf_group(const std::string& full_url,
                                               const std::string& modelId,
                                               const std::string& fileId,
                                               const std::string& fileFormat,
                                               const std::string& name)
{
    //wxGetApp().mainframe->select_tab((size_t) MainFrame::TabPosition::tp3DEditor);
    auto target_path = cache_3mf_folder_;
    /* if (!fs::exists(target_path.append(modelId))) {
         fs::create_directories(target_path);
     }*/

    // Generate a unique file name: if the fileId differs but the name is the same, use the "name (n).ext" rule
    std::string base_name = filterInvalidFileNameChars(name);
    std::string ext       = fileFormat;
    if (!ext.empty() && ext.front() != '.')
        ext = "." + ext;

    // If this fileId already had a path before, reuse the original file name first to keep repeated downloads consistent
    std::string existing_filename_for_this_id;
    int         same_name_count = 0;
    {
        std::lock_guard<std::mutex> lock_guard(cache_json_mutex_);
        if (cache_json_.is_object() && cache_json_.contains("3mfs")) {
            for (auto& file : cache_json_["3mfs"]) {
                try {
                    const std::string fid   = file["fileId"].get<std::string>();
                    const std::string fname = file.contains("name") ? file["name"].get<std::string>() : std::string();
                    if (fid == fileId) {
                        if (file.contains("path")) {
                            const std::string p = file["path"].get<std::string>();
                            if (!p.empty()) {
                                try {
                                    existing_filename_for_this_id = boost::filesystem::path(p).filename().string();
                                } catch (...) {}
    }
                        }
                    } else if (fname == name) {
                        // count of entries with the same name but a different fileId, used to decide the suffix number
                        ++same_name_count;
                    }
                } catch (...) {}
            }
        }
    }

    std::string safe_name;
    if (!existing_filename_for_this_id.empty()) {
        safe_name = existing_filename_for_this_id;
    } else {
        if (same_name_count > 0)
            safe_name = base_name + "(" + std::to_string(same_name_count) + ")" + ext;
        else
            safe_name = base_name + ext;
    }

    auto cache_path = target_path;
    auto file_path   = cache_path.append(safe_name).string();

    auto progress_cb = [&, modelId, fileId, file_path = std::move(file_path)](int progress) {
        std::lock_guard<std::mutex> lock_guard(cache_json_mutex_);
        if (cache_json_.is_object() && cache_json_.contains("3mfs")) {
            for (auto& file : cache_json_["3mfs"]) {
                if (file["fileId"] != fileId)
                    continue;
                auto& cache_progress = file["progress"];
                if (cache_progress < progress) {
                    cache_progress = progress;
                    if (progress == 100) {
                        file["path"]                        = file_path;
                        save_cache_to_storage();
                    }
                    return;
                }
            }
        }
    };

    auto complete_cb = [&, modelId, fileId](std::string path) {
        std::lock_guard<std::mutex> lock_guard(cache_json_mutex_);
        if (!(cache_json_.is_object() && cache_json_.contains("3mfs")))
            return;

        // locate entry; if not found, it was likely cancelled — skip import

        bool found = false;
        for (auto &file : cache_json_["3mfs"]) {
            try {
                const std::string fid = file["fileId"].get<std::string>();
                if (fid != fileId)
                    continue;
                found = true;
                // set to 100 and persist path when completion fires (guarded by cancellation state)
                int progress = 0;
                if (file.contains("progress"))
                    progress = file["progress"].get<int>();
                if (progress < 100) {
                    file["progress"] = 100;
                    file["path"]     = path;
                    save_cache_to_storage();
                }
                break;
            } catch (...) {}
        }

        if (!found) return;

        // Auto-import is now driven by CloudDownloadProgressDialog::on_timer
        // after the download dialog has fully closed, to avoid two progress
        // dialogs appearing simultaneously. Do not call request_model_download here.
    };

    BOOST_LOG_TRIVIAL(debug) << "started download";
    std::lock_guard<std::mutex> lock_guard(cache_json_mutex_);
    if (cache_json_.is_object() && cache_json_.contains("3mfs")) {
        bool found = false;
        for (auto& file : cache_json_["3mfs"]) {
            std::string file_id = file["fileId"];
            if (file_id == fileId) {
                found = true;
                // Reset progress and clear path so UI shows download progress again
                file["fileId"]       = fileId;
                file["progress"]     = 0;
                file["path"]         = "";
                file["name"]         = name;
                file["modelGroupId"] = modelId;
                
                break;
            }
        }
        if (!found) {
            auto&          model_array = cache_json_["3mfs"];
            nlohmann::json model_object;
            model_object["createTime"] = time(nullptr);
            model_object["fileId"]     = fileId;
            json files_array;
            model_object["progress"] = 0;
            model_object["path"]     = "";
            model_object["name"]     = name;
            model_object["modelGroupId"] = modelId;
            model_array.push_back(model_object);
        }
    } else {
        nlohmann::json model_array;
        nlohmann::json model_object;
        model_object["createTime"] = time(nullptr);
        model_object["fileId"]     = fileId;
        json file_object;
        model_object["progress"] = 0;
        model_object["path"]     = "";
        model_object["name"]     = name;
        model_object["modelGroupId"] = modelId;
        model_array.push_back(model_object);
        cache_json_["3mfs"] = model_array;
    }
    // Always start a fresh download to show progress window consistently.
    download_tasks_.emplace_back(
        std::make_unique<DownloadTask>(fileId, full_url, safe_name, target_path, progress_cb, complete_cb));
    download_tasks_.back()->start();
    
    save_cache_to_storage();
}


void ModelDownloader::cancel_download_model_group(const std::string& modelId)
{
    for (auto& task : download_tasks_) {
        if (task->get_id() == modelId) {
            task->cancel();
        }
    }

    std::lock_guard<std::mutex> lock_guard(cache_json_mutex_);
    if (cache_json_.is_object() && cache_json_.contains("models")) {
        bool found = false;
        int  idx   = -1;
        std::string removePath = "";
        for (auto& model : cache_json_["models"]) {
            idx++;
            std::string model_id = model["modelId"];
           
        
            if (model_id == modelId) {
                auto model_files = model["files"];
                std::string path = "";
                if (model_files.size()) 
                    path = model_files.at(0)["path"];
                if (path != "") {
                    size_t pos = path.find_last_of('\\');
                    if (pos != std::string::npos) {
                        path = path.substr(0, pos);
                    }
                    fs::remove_all(path);
                }
                found = true;
                break;
            }
        }
        if (found) {
            cache_json_["models"].erase(idx);
            save_cache_to_storage();
        }
    }
}

void ModelDownloader::cancel_download_3mf_group(const std::string& fileId)
{
    for (auto& task : download_tasks_) {
        if (task->get_id() == fileId) {
            task->cancel();
        }
    }

    std::lock_guard<std::mutex> lock_guard(cache_json_mutex_);
    if (cache_json_.is_object() && cache_json_.contains("3mfs")) {
        bool found = false;
        int  idx   = -1;
        std::string removePath = "";
        for (auto& file : cache_json_["3mfs"]) {
            idx++;
            std::string file_id = file["fileId"];
            removePath          = file["path"];
            if (file_id == fileId) {
                found = true;
                break;
            }
        }
        if (found) {
            cache_json_["3mfs"].erase(idx);
            fs::remove(removePath);
            save_cache_to_storage();
        }
    }
}

nlohmann::json ModelDownloader::get_cache_json()
{
    std::lock_guard<std::mutex> lock_guard(cache_json_mutex_);
    return cache_json_;
}

void ModelDownloader::load_cache_from_storage() {
    std::string down_path = wxGetApp().app_config->get("download_path");
    dest_folder_ = fs::path(down_path);
    dest_folder_.append(user_id_);

    cache_file_ = dest_folder_;
    cache_file_ =  dest_folder_ / "cloud_download_data.json";
    cache_file_bak_ = dest_folder_ / "cloud_download_data.json.bak"; 
    boost::filesystem::path cache_id_file_ = dest_folder_;
    try{
        if (fs::exists(cache_file_)) {
            boost::nowide::ifstream ifs(cache_file_.string());
            ifs >> cache_json_;
        }
    }
    catch (...) {
        if (fs::exists(cache_file_bak_))
        {
            fs::remove(cache_file_);
            fs::rename(cache_file_bak_, cache_file_);

        }else{
            fs::remove(cache_file_);
        }
        
    }
    cache_id_file_.append("3mfs");
    if (!fs::exists(cache_id_file_)) {
        fs::create_directories(cache_id_file_);
    }

    cache_3mf_folder_ = cache_id_file_;

    dest_folder_.append("models");
    if (!fs::exists(dest_folder_)) {
        fs::create_directories(dest_folder_);
    }
}

void ModelDownloader::save_cache_to_storage()
{
    boost::nowide::ofstream c;
    if (fs::exists(cache_file_)) {
        fs::remove(cache_file_bak_);
        fs::rename(cache_file_, cache_file_bak_);
    }
    c.open(cache_file_.string(), std::ios::out | std::ios::trunc);
    c << std::setw(4) << cache_json_ << std::endl;
    c.close();
}

}} // namespace Slic3r::GUI
