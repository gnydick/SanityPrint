#include "slic3r/GUI/FileDownloader.hpp"
#include <exception>
// Implementation
CurlConnectionPool::CurlConnectionPool(int max_connections)
    : multi_handle_(curl_multi_init()),
      still_running_(0),
      max_connections_(max_connections) {
    if (!multi_handle_) {
        throw std::runtime_error("Failed to initialize curl multi handle");
    }
}

CurlConnectionPool::~CurlConnectionPool() {
    // Clean up all easy handles
    for (auto handle : easy_handles_) {
        curl_multi_remove_handle(multi_handle_, handle);
        curl_easy_cleanup(handle);
    }
    
    // Clean up the multi handle
    if (multi_handle_) {
        curl_multi_cleanup(multi_handle_);
    }
}

bool CurlConnectionPool::addDownload(const std::string url, const std::string filename) {
    try {
        // Create the download item
        std::shared_ptr<DownloadItem> item(new DownloadItem(url, filename));
        download_items_.emplace_back(item);
        
        if (!item->stream.is_open()) {
            download_items_.pop_back();
            std::cerr << "Failed to open file: " << filename << std::endl;
            return false;
        }
        
        // Create the easy handle
        CURL* handle = curl_easy_init();
        if (!handle) {
            download_items_.pop_back();
            return false;
        }
        
        // Set easy handle options
        curl_easy_setopt(handle, CURLOPT_URL, item->url.c_str());
        curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, writeDataCallback);
        curl_easy_setopt(handle, CURLOPT_WRITEDATA, &item->stream);
        curl_easy_setopt(handle, CURLOPT_PRIVATE, item->filename.c_str());
        curl_easy_setopt(handle, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(handle, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(handle, CURLOPT_SSL_VERIFYHOST, 0L);
        
        // Enable connection reuse
        curl_easy_setopt(handle, CURLOPT_FRESH_CONNECT, 0L);
        curl_easy_setopt(handle, CURLOPT_FORBID_REUSE, 0L);
        
        // Add to the multi handle
        curl_multi_add_handle(multi_handle_, handle);
        easy_handles_.push_back(handle);
        
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Error adding download: " << e.what() << std::endl;
        return false;
    }
}

void CurlConnectionPool::performDownloads() {
    // Initial perform
    curl_multi_perform(multi_handle_, &still_running_);
    
    // Main download loop
    while (still_running_) {
        fd_set fdread, fdwrite, fdexcep;
        int maxfd = -1;
        long curl_timeo = -1;
        
        FD_ZERO(&fdread);
        FD_ZERO(&fdwrite);
        FD_ZERO(&fdexcep);
        
        struct timeval timeout;
        timeout.tv_sec = 10;
        timeout.tv_usec = 0;
        
        // Get the timeout setting
        curl_multi_timeout(multi_handle_, &curl_timeo);
        if (curl_timeo >= 0) {
            timeout.tv_sec = curl_timeo / 1000;
            if (timeout.tv_sec > 1) {
                timeout.tv_sec = 1;
            } else {
                timeout.tv_usec = (curl_timeo % 1000) * 1000;
            }
        }
        
        // Get the file descriptor sets
        curl_multi_fdset(multi_handle_, &fdread, &fdwrite, &fdexcep, &maxfd);
        
        // Wait for activity or timeout
        if (maxfd == -1) {
            // No file descriptors, wait for a while
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        } else {
            // Use select to wait for I/O activity
            select(maxfd + 1, &fdread, &fdwrite, &fdexcep, &timeout);
        }
        
        // Perform the transfers
        curl_multi_perform(multi_handle_, &still_running_);
        
        // Clean up completed downloads
        cleanupCompletedDownloads();
    }
    download_items_.clear();
}

size_t CurlConnectionPool::writeDataCallback(void* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* stream = static_cast<std::ofstream*>(userdata);
    size_t written = 0;
    
    try{
        stream->write(static_cast<char*>(ptr), size * nmemb);
        written = size * nmemb;
    }catch(std::exception e)
    {
        
    }

    return written;
}

void CurlConnectionPool::cleanupCompletedDownloads() {
    CURLMsg* msg = nullptr;
    int msgs_left = 0;
    
    while ((msg = curl_multi_info_read(multi_handle_, &msgs_left))) {
        if (msg->msg == CURLMSG_DONE) {
            CURL* handle = msg->easy_handle;
            char* filename = nullptr;
            
            // Get the file name
            curl_easy_getinfo(handle, CURLINFO_PRIVATE, &filename);
            
            // Output the download result
            if (msg->data.result == CURLE_OK) {
                std::cout << "Download completed: " << filename << std::endl;
            } else {
                std::cerr << "Download failed: " << filename 
                          << " - " << curl_easy_strerror(msg->data.result) << std::endl;
            }
            
            // Remove from the multi handle and clean up
            curl_multi_remove_handle(multi_handle_, handle);
            
            // Remove from easy_handles_
            auto it = std::find(easy_handles_.begin(), easy_handles_.end(), handle);
            if (it != easy_handles_.end()) {
                easy_handles_.erase(it);
            }
            
            curl_easy_cleanup(handle);
        }
    }
}