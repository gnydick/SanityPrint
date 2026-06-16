#pragma once

#include <wx/app.h>
#include <wx/wx.h>
#include <wx/button.h>
#include <wx/dialog.h>
#include <wx/sizer.h>
#include <wx/string.h>
#include <wx/statbmp.h>
#include <wx/artprov.h>
#include <wx/statline.h>
#include <wx/filename.h>
#include <filesystem>
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <curl/curl.h>
#include <iostream>
#include <fstream>
#include "slic3r/GUI/GUI_Utils.hpp"
#include "miniz/miniz.h"


struct SystemInfo {
    wxString osDescription;
    wxString cpuModel;
    wxString graphicsCardVendor;
    wxString openGLVersion;
    wxString build;
    wxString uuid;
};
class TextInput;
class ErrorReportDialog : public Slic3r::GUI::DPIDialog
{
public:
    // Constructor, takes the parent window pointer and the dialog title
    ErrorReportDialog(wxWindow* parent, const wxString& title);
    wxString getSystemInfo();
    void sendReport();
    wxString zipFiles();
    void     setDumpFilePath(wxString dumpFilePath);
	bool addLogFiles(mz_zip_archive& archive);

protected:
    void on_dpi_changed(const wxRect& suggested_rect) override;
    void on_sys_color_changed() override {}

private:
    TextInput* m_InfoInput = nullptr;
    SystemInfo m_info;
    wxString m_dumpFilePath;
    wxString m_systemInfoFilePath;
    void sendEmail(wxString zipFilePath);
    void       GetErrorReport();

    // Collect log files and sort them by last modified time
    std::vector<std::filesystem::path> collectLogFiles(const std::filesystem::path& log_path);

    // Create the temporary path for the log archive
    wxString createLogZipPath();

    // Compress log files to the specified path
    bool compressLogFiles(const std::vector<std::filesystem::path>& log_files, const wxString& logZipPath);

    // Try to add a single log file to the archive
    bool tryAddLogFileToArchive(const std::filesystem::path& log_file, mz_zip_archive& archive);

    // Try to copy the file and add it to the archive
    bool tryCopyAndAddFile(const std::filesystem::path& srcPath, mz_zip_archive& zip, const char* entryName);

    // Add the log archive to the main archive
    bool addLogZipToMainArchive(mz_zip_archive& archive, const wxString& logZipPath);

};
