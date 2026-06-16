#include "LanPrinterInterface.hpp"
#include "../RemotePrinterManager.hpp"
#include <curl/curl.h>
#include <fstream>
#include <iostream>
#include <filesystem>

namespace RemotePrint {
LanPrinterInterface::LanPrinterInterface() { curl_global_init(CURL_GLOBAL_DEFAULT); }

LanPrinterInterface::~LanPrinterInterface() { curl_global_cleanup(); }

std::future<void> LanPrinterInterface::sendFileToDevice(const std::string&         strIp,
                                                        const std::string&         fileName,
                                                        const std::string&         filePath,
                                                        std::function<void(float,double)> callback,
                                                        std::function<void(int)>   errorCallback, std::function<void(std::string)> onCompleteCallback)
{
    return std::async(std::launch::async, [=]() {
        // 1. initialize CURL

        CURL* curl = curl_easy_init();
        if (!curl)
        {
            if (errorCallback)
                errorCallback(CURLE_FAILED_INIT);
            return;
        }

        // 2. RAII guard for the CURL handle
        struct CurlGuard
        {
            CURL* handle;
            ~CurlGuard()
            {
                if (handle) curl_easy_cleanup(handle);
            }
        } guard{ curl };


        FILE* fd = nullptr;
#if defined(_MSC_VER) || defined(__MINGW64__)
        fd = boost::nowide::fopen(filePath.c_str(), "rb");
#elif defined(__GNUC__) && defined(_LARGEFILE64_SOURCE)
        fd = fopen64(filePath.c_str(), "rb");
#else
        fd = fopen(filePath.c_str(), "rb");
#endif

        if (fd == nullptr)
        {
            if (errorCallback)
                errorCallback(CURLE_READ_ERROR);
            return;
        }

        struct FileGuard
        {
            FILE* fd;
            ~FileGuard()
            {
                if (fd) fclose(fd);
            }
        } fileGuard{ fd };

        fseek(fd, 0, SEEK_END);  
        curl_off_t fileSize = ftell(fd);  
        fseek(fd, 0, SEEK_SET);

        callback(10,0);

        std::string sFileName = fileName;
        sFileName = std::regex_replace(sFileName, std::regex("[\\\\/:*?\"'<>|#&=+]"), "");
        // 4. configure basic FTP options
        const std::string url = "ftp://" + strIp + "/mmcblk0p1/creality/gztemp/" + sFileName;
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);
        curl_easy_setopt(curl, CURLOPT_READDATA, fd);
        curl_easy_setopt(curl, CURLOPT_INFILESIZE_LARGE, fileSize);

        // 5. set the read callback
        //curl_easy_setopt(curl, CURLOPT_READFUNCTION, [](void* ptr, size_t size, size_t nmemb, void* stream) {
        //    int n;
        //    FILE* f = (FILE*)stream;
        //    if (ferror(f))
        //    {
        //        return CURL_READFUNC_ABORT;
        //    }

        //    n = fread(ptr, size, nmemb, f) * size;
        //    return n;
        //    });

        // set timeout
        curl_easy_setopt(curl, CURLOPT_FTP_RESPONSE_TIMEOUT, 30L);

        // 6. perform the transfer
        CURLcode res = curl_easy_perform(curl);


        if (res != CURLE_OK)
        {
            if (errorCallback)
            {
                errorCallback(res);
            }
        }
        else
        {
            if (callback) 
                callback(100.0f,0.0f); // Assuming the upload is complete

            std::string body = "{ \"code\": 200, \"message\" : \"OK\" }";
            if(onCompleteCallback)
                onCompleteCallback(body);
        }
     });
}

} // namespace RemotePrint
