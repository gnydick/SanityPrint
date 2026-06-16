#include "slic3r/GUI/BBLStatusBar.hpp"
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
#include "libslic3r_version.h"
#include "nlohmann/json.hpp"
#include "miniz/miniz.h"
#include <curl/curl.h>
#include <iostream>
#include <fstream>
struct SystemInfo {
    wxString osDescription;
    wxString graphicsCardVendor;
    wxString openGLVersion;
    wxString build;
    wxString uuid;
};
class ErrorReportDialog : public wxDialog {
public:
    // Constructor that takes the parent window pointer and the dialog title
    ErrorReportDialog(wxWindow* parent, const wxString& title) : wxDialog(parent, wxID_ANY, title) {
        // Create a vertical layout manager
        wxBoxSizer* sizer = new wxBoxSizer(wxVERTICAL);

        // Create a horizontal layout manager to hold the title
        wxBoxSizer* titleSizer = new wxBoxSizer(wxHORIZONTAL);
        //std::filesystem::path imagePath = "resources\\images\\warning.png";
        wxIcon warningIcon = wxArtProvider::GetIcon(wxART_WARNING, wxART_MESSAGE_BOX);

    // Convert the icon to a bitmap
        wxBitmap bitmap(warningIcon);
        // Create a wxStaticBitmap control and add it to the window
        wxStaticBitmap* staticBitmap = new wxStaticBitmap(this, wxID_ANY, bitmap, wxPoint(50, 50));
        wxStaticText* text = new wxStaticText(this, wxID_ANY, "A serious error has occurred in Some App. Please send this error report to us to fix the problem.\nPlease click the \"Send Report\" button to automatically publish the error report to our server.", wxPoint(20, 20));
        titleSizer->Add(staticBitmap, 0, wxALIGN_CENTER | wxALL, 10);
        titleSizer->Add(text, 0, wxALIGN_CENTER | wxALL, 10);
        sizer->Add(titleSizer, 0, wxALIGN_CENTER | wxALL, 10);
        // Divider line
        wxStaticLine* line = new wxStaticLine(this, wxID_ANY);
        sizer->Add(line, 0, wxALL | wxEXPAND, 5);
        
        
        GetErrorReport();
        wxString formattedString = wxString::Format(wxT("OS: %s\nGraphicsCard: %s\nOpenGLVersion: %s\nVersion: %s\nUid: %s\n"), m_info.osDescription, m_info.graphicsCardVendor, m_info.openGLVersion, m_info.build,m_info.uuid);
        // Create a horizontal layout manager to hold the text box
        wxStaticText* vtext = new wxStaticText(this, wxID_ANY, formattedString, wxPoint(20, 20));
        sizer->Add(vtext, 0, wxALIGN_LEFT | wxALL, 10);

        // Create the send button
        wxButton* sendButton = new wxButton(this, wxID_OK, "SendReport");
        // Create the cancel button
        wxButton* cancelButton = new wxButton(this, wxID_CANCEL, "Cancel");

        // Create a horizontal layout manager to hold the buttons
        wxBoxSizer* buttonSizer = new wxBoxSizer(wxHORIZONTAL);
        buttonSizer->Add(sendButton, 0, wxALL, 5);
        buttonSizer->Add(cancelButton, 0, wxALL, 5);

        // Add the button layout manager to the main layout manager
        sizer->Add(buttonSizer, 0, wxALIGN_CENTER | wxALL, 10);

        // Set the dialog's layout manager
        SetSizer(sizer);
        // Resize the dialog to fit its content
        sizer->Fit(this);
    }
    wxString getSystemInfo() {
        nlohmann::json j;
        j["osDescription"] = m_info.osDescription.ToStdString();
        j["graphicsCardVendor"] = m_info.graphicsCardVendor.ToStdString();
        j["openGLVersion"] = m_info.openGLVersion.ToStdString();
        j["build"] = m_info.build.ToStdString();
        j["uuid"] = m_info.uuid.ToStdString();
         try {
            // Get the temporary directory path
            std::filesystem::path tempDir(wxFileName::GetTempDir().ToStdString());
            // Generate a unique temporary file name
            std::filesystem::path tempFilePath = tempDir / "system_info.json";

            // Open the temporary file to write JSON data
            std::ofstream tempFile(tempFilePath);
            if (tempFile.is_open()) {
                // Write the JSON object to the file, using dump(4) for formatted output
                tempFile << j.dump(4);
                tempFile.close();
                return tempFilePath.wstring();
                } else {
                    
                }
            } catch (const std::filesystem::filesystem_error& e) {
                
            } catch (const std::exception& e) {
                
            }

        return "";
    }
    void sendReport();
    wxString zipFiles();
    void setDumpFilePath(wxString dumpFilePath) {
        m_dumpFilePath = dumpFilePath;
    }
    private:
        SystemInfo m_info;
        wxString m_dumpFilePath;
        wxString m_systemInfoFilePath;
        void sendEmail(wxString zipFilePath);
        void GetErrorReport() {
            // Get the error report
            wxString osDescription = wxGetOsDescription();
            m_info.osDescription = osDescription;
            m_info.build = wxString(SANITYPRINT_VERSION, wxConvUTF8);
            m_info.uuid = wxDateTime::Now().Format("%Y%m%d%H%M%S");
            // Get graphics card information
           if (!glfwInit()) {
                std::cerr << "Failed to initialize GLFW!" << std::endl;
                return ;
            }

            // Create a hidden (invisible) window
            glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
            GLFWwindow* window = glfwCreateWindow(1, 1, "", nullptr, nullptr);

            if (!window) {
                std::cerr << "Failed to create hidden GLFW window!" << std::endl;
                glfwTerminate();
                return ;
            }

            glfwMakeContextCurrent(window);

            // Initialize GLEW
            if (glewInit() != GLEW_OK) {
                std::cerr << "Failed to initialize GLEW!" << std::endl;
                return ;
            }

            // Get graphics card information
            const GLubyte* renderer = glGetString(GL_RENDERER);
            const GLubyte* version = glGetString(GL_VERSION);

            std::cout << "Renderer: " << renderer << std::endl;
            std::cout << "OpenGL version: " << version << std::endl;
            m_info.graphicsCardVendor = wxString(reinterpret_cast<char*>(const_cast<GLubyte*>(renderer)), wxConvUTF8);
            m_info.openGLVersion = wxString(reinterpret_cast<char*>(const_cast<GLubyte*>(version)), wxConvUTF8);
            // Destroy the window and clean up
            glfwDestroyWindow(window);
            glfwTerminate();
            
            return ;
        }

};