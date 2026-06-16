#ifndef slic3r_GUI_CurlConnectionPool_hpp_
#define slic3r_GUI_CurlConnectionPool_hpp_
#include <iostream>
#include <string>
#include <vector>
#include <fstream>
#include <curl/curl.h>

class CurlConnectionPool {
public:
    // Constructor, specifies the maximum number of concurrent connections
    explicit CurlConnectionPool(int max_connections = 5);

    // Destructor
    ~CurlConnectionPool();

    // Add a download task
    bool addDownload(const std::string url, const std::string filename);

    // Execute all download tasks
    void performDownloads();

    // Disable copy and assignment
    CurlConnectionPool(const CurlConnectionPool&) = delete;
    CurlConnectionPool& operator=(const CurlConnectionPool&) = delete;

private:
    // Download item structure
    struct DownloadItem {
        std::string url;
        std::string filename;
        boost::nowide::ofstream stream;
        
        DownloadItem(const std::string& u, const std::string& f) 
            : url(u), filename(f), stream(f,boost::nowide::ofstream::binary) {}
        
        // Move constructor
        DownloadItem(DownloadItem&& other) noexcept
            : url(std::move(other.url)),
              filename(std::move(other.filename)){}

        // Disable copy
        DownloadItem(const DownloadItem&) = delete;
        DownloadItem& operator=(const DownloadItem&) = delete;
    };

    // Static callback function
    static size_t writeDataCallback(void* ptr, size_t size, size_t nmemb, void* userdata);

    // Clean up completed download items
    void cleanupCompletedDownloads();

    // libcurl multi handle
    CURLM* multi_handle_;

    // Number of downloads currently running
    int still_running_;

    // Maximum number of concurrent connections
    int max_connections_;

    // All download items
    std::vector<std::shared_ptr<DownloadItem>> download_items_;

    // All easy handles
    std::vector<CURL*> easy_handles_;
};
#endif