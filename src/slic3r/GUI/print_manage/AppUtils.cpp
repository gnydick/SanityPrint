#include "AppUtils.hpp"
#include "../Widgets/WebView.hpp"
#include "../GUI.hpp"

#include <boost/uuid/detail/md5.hpp>
#include "libslic3r/Utils.hpp"
#include <algorithm>
#include <cctype>
#include <fstream>
#pragma execution_character_set("utf-8")
using namespace Slic3r::GUI;
using namespace boost::uuids::detail;
using namespace Slic3r;
namespace DM{

    bool is_uos_system()
    {
#ifdef __WXGTK__
        static int cached = -1;
        if (cached != -1)
            return cached == 1;
        std::ifstream f("/etc/os-release");
        if (!f.is_open()) {
            cached = 0;
            return false;
        }
        std::string line;
        while (std::getline(f, line)) {
            std::string lower = line;
            std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return std::tolower(c); });
            if (lower.find("uos") != std::string::npos || lower.find("uniontech") != std::string::npos) {
                cached = 1;
                return true;
            }
        }
        cached = 0;
        return false;
#else
        return false;
#endif
    }

    void AppUtils::PostMsg(wxWebView* browse, const std::string& data)
    {
        if (browse == nullptr || browse->IsBeingDeleted())
            return;
        WebView::RunScript(browse, from_u8(data));
    }

    void AppUtils::PostMsg(wxWebView* browse, nlohmann::json& data)
    {
        if (browse == nullptr || browse->IsBeingDeleted())
            return;
        std::string json   = data.dump(-1, ' ', true);
        std::string script = "window.handleStudioCmd(" + json + ");";
        WebView::RunScript(browse, from_u8(script));
    }

    std::string AppUtils::MD5(const std::string& file)
    {
        std::string ret;
        std::string filePath = std::string(file);
        Slic3r::bbl_calc_md5(filePath, ret);
        return ret;
    }

    std::string AppUtils::extractDomain(const std::string& url)
    {
        std::string domain;
        size_t start = 0;

        // Check whether there is a protocol prefix (such as http:// or https://)
        if (url.find("://") != std::string::npos) {
            start = url.find("://") + 3;
        }

        // Find where the domain ends, i.e. the position of the first /, ? or # character
        size_t end = url.find_first_of("/?#:", start);
        if (end == std::string::npos) {
            // If no terminating character is found, the domain runs to the end of the string
            domain = url.substr(start);
        }
        else {
            // Extract the domain portion
            domain = url.substr(start, end - start);
        }

        return domain;
    }
    bool LANConnectCheck::pingHostWithRetry(const std::string& ip, ThreadController& ctrl, int retries, int timeout_ms, int delay_ms) {
        int attempt = 0;

        while (attempt < retries) {
            if (ctrl.isStopRequested()) return false;  // Interruption point 1: check before execution[3](@ref)
            // Build the ping command
#ifdef _WIN32
            std::string command = "ping -n 4 -w 2000 " + ip;            //ping ip
#else
            std::string cmd = "ping -c 4 -W " + std::to_string(timeout_ms / 1000) + " " + ip + " > /dev/null 2>&1";
#endif

#ifdef _WIN32
            // Configure the process startup information
            STARTUPINFOA si{};
            PROCESS_INFORMATION pi{};
            si.cb = sizeof(si);
            si.dwFlags = STARTF_USESHOWWINDOW;
            si.wShowWindow = SW_HIDE;

            if (CreateProcessA(NULL, const_cast<char*>(command.c_str()),
                NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi))
            {
                // Non-blocking wait while checking for interruption
                while (WaitForSingleObject(pi.hProcess, 50) == WAIT_TIMEOUT) {
                    if (ctrl.isStopRequested()) {
                        TerminateProcess(pi.hProcess, 1);
                        break;
                    }
                }

                DWORD exitCode;
                GetExitCodeProcess(pi.hProcess, &exitCode);
                CloseHandle(pi.hProcess);
                CloseHandle(pi.hThread);

                if (exitCode == 0) return true;
            }
#else
            // Linux/macOS implementation
            if (system(cmd.c_str()) == 0) {
                return true; // Ping succeeded
            }
#endif

            // Wait before retrying
            // Wait before retrying while checking for interruption
            if (ctrl.waitForStop(std::chrono::milliseconds(delay_ms)))
                return false;

            attempt++;

            // Increase the wait time for the next attempt (exponential backoff)
            if (attempt < retries) {
                delay_ms *= 2; // Increase the wait time
            }
        }
        int x = 0;
        return false; // All attempts failed
    }
    // Use Boost.Asio to check the port
    bool LANConnectCheck::isPortOpen(const std::string& ip, int port, ThreadController& ctrl) {
        using boost::asio::deadline_timer;
        using boost::asio::ip::tcp;
        try {
            boost::asio::io_service io_service;
            tcp::socket socket(io_service);
            tcp::endpoint endpoint(boost::asio::ip::address::from_string(ip), port);

            // Asynchronous connect with timeout control
            bool connected = false;
            boost::system::error_code ec;
            socket.async_connect(endpoint, [&](const boost::system::error_code& error) {
                ec = error;
                io_service.stop();
                });

            // Interruptible IO wait
            std::thread io_thread([&] { io_service.run(); });
            while (io_service.run_one()) {
                if (ctrl.waitForStop(std::chrono::milliseconds(100))) {
                    socket.close();
                    break;
                }
            }
            io_thread.join();
            return !ec;
        }
        catch (const std::exception& e) {
            std::cerr << "Fuction isPortOpen Exception: " << e.what() << std::endl;
            return false;
        }
    }

#ifdef _WIN32
#include <Windows.h>
#include <string>
#include <cctype>

    float getWinPingLatency(const std::string& ip, ThreadController& ctrl) {
        std::string cmd = "ping -n 5 " + ip;

        // Use the STARTUPINFO structure to hide the window
        STARTUPINFOA si;
        PROCESS_INFORMATION pi;
        SECURITY_ATTRIBUTES sa;

        ZeroMemory(&si, sizeof(si));
        si.cb = sizeof(si);
        si.dwFlags = STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_HIDE;  // Hide the window

        ZeroMemory(&pi, sizeof(pi));

        sa.nLength = sizeof(sa);
        sa.bInheritHandle = TRUE;
        sa.lpSecurityDescriptor = NULL;

        // Create a pipe to capture the output
        HANDLE hReadPipe, hWritePipe;
        if (!CreatePipe(&hReadPipe, &hWritePipe, &sa, 0)) {
            return -1.0f;
        }

        SetHandleInformation(hReadPipe, HANDLE_FLAG_INHERIT, 0);

        // Redirect the output
        si.hStdOutput = hWritePipe;
        si.hStdError = hWritePipe;
        si.dwFlags |= STARTF_USESTDHANDLES;

        // Create the process
        char command[256];
        sprintf_s(command, "cmd /C \"%s\"", cmd.c_str());

        if (!CreateProcessA(
            NULL,
            command,
            NULL,
            NULL,
            TRUE,
            CREATE_NO_WINDOW,
            NULL,
            NULL,
            &si,
            &pi))
        {
            CloseHandle(hWritePipe);
            CloseHandle(hReadPipe);
            return -1.0f;
        }

        // Close the write end
        CloseHandle(hWritePipe);

        // Read the output
        char buffer[1024];
        DWORD bytesRead;
        std::string output;
        float avgLatency = -1.0f;

       
        //while (ReadFile(hReadPipe, buffer, sizeof(buffer) - 1, &bytesRead, NULL) && bytesRead > 0) {
        //    buffer[bytesRead] = '\0';
        //    output += buffer;
        //}
        while (true) {
            if (ctrl.isStopRequested()) {
                TerminateProcess(pi.hProcess, 1);
                break;
            }

            if (!ReadFile(hReadPipe, buffer, sizeof(buffer) - 1, &bytesRead, NULL) || bytesRead == 0)
                break;

            buffer[bytesRead] = '\0';
            output += buffer;
        }
        BOOST_LOG_TRIVIAL(error) << "getWinPingLatency output : " << output;
        // Close the read end
        CloseHandle(hReadPipe);

        // Wait for the process to finish
        WaitForSingleObject(pi.hProcess, INFINITE);

        // Close the process handles
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);

        // Parse the output
        size_t pos = output.find(encode_path(std::string("Average =").c_str()));
        
        if (pos == std::string::npos) {
            pos = output.find("Average =");
        }

        if (pos != std::string::npos) {
            std::string avgStr;

            // Find the numeric portion after the equals sign
            size_t eqPos = output.find('=', pos);
            if (eqPos != std::string::npos) {
                for (size_t i = eqPos + 1; i < output.length(); i++) {
                    char c = output[i];
                    if (isdigit(c) || c == '.') {
                        avgStr += c;
                    }
                    else if (!avgStr.empty()) {
                        // Stop once a non-numeric character is hit after digits have been collected
                        break;
                    }
                }

                if (!avgStr.empty()) {
                    try {
                        avgLatency = std::stof(avgStr);
                    }
                    catch (...) {
                        avgLatency = -1.0f;
                    }
                }
            }
        }

        return avgLatency;
    } 
#else
    // Use the original Linux approach to get the average latency
    float getLinuxPingLatency(const std::string& ip) {
        // Stage three: network quality check (average latency of 5 pings)
        std::string cmd = "ping -c 5 -i 0.2 " + ip + " | tail -1 | awk -F '/' '{print $5}'";
        FILE* pipe = popen(cmd.c_str(), "r");
        if (!pipe) return -1;
        float avgLatency = 0.0;
        char buffer[32];
        if (fgets(buffer, sizeof(buffer), pipe)) {
            avgLatency = atof(buffer);
            pclose(pipe);
        }
        else {
            pclose(pipe);
            avgLatency = -1.0;
        }
        return avgLatency;
    }
#endif
#include <string>
#include <cstdlib>
    int LANConnectCheck::checkLan(const std::string& ip, ThreadController& ctrl)
    {
        std::string deviceIP = ip; // Replace with the target device IP
        std::string msg = "";
        int errorcode = 0;
        // Stage one: check whether the device is online (ping test)
        if (!pingHostWithRetry(deviceIP, ctrl)) {
            if (ctrl.isStopRequested()) return -1;  // Interruption code
            errorcode = 1;
            return errorcode;
            //return msg;
        }
        // Stage two: port connectivity check
        const int ports_to_check[] = { 80, 9999 };
        bool allPortsOpen = true;
        for (int port : ports_to_check) {
            if (!isPortOpen(deviceIP, port,ctrl)) {
                if (ctrl.isStopRequested()) return -1;  //
                return 2;  // Port not reachable
            }
        }
        if (!allPortsOpen) {
            errorcode = 2;
            return errorcode;
        }

        // Stage three: network quality check (average latency of 5 pings)
#ifdef _WIN32
        float avgLatency = getWinPingLatency(deviceIP,ctrl);
#else 
        float avgLatency = getLinuxPingLatency(deviceIP);
#endif
        if (avgLatency < 0) {
            errorcode = 31;
        }
        else if (avgLatency > 1000.0f) {
            errorcode = 32;
        }
        else {
            errorcode = 0;
        }
        return errorcode;
    }
}
