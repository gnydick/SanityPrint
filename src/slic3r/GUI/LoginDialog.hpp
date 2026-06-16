#ifndef slic3r_LoginDialog_hpp_
#define slic3r_LoginDialog_hpp_

#include <wx/dialog.h>
#include <wx/webview.h>
#include <wx/sizer.h>
#include <wx/button.h>
#include <wx/hyperlink.h>
#include <wx/stattext.h>
#include <wx/panel.h>

#include "I18N.hpp"
#include "GUI_Utils.hpp"

namespace Slic3r {
namespace GUI {

class LoginDialog : public DPIDialog
{
public:
    LoginDialog(wxWindow* parent, const wxString& title = _L("Login"));
    virtual ~LoginDialog();

    // Show the login dialog
    void ShowLoginDialog(const wxString& loginUrl = wxEmptyString);
    void MarkLoginSucceeded();

private:
    // Event handlers
    void OnWebViewNavigating(wxWebViewEvent& evt);
    void OnWebViewNewWindow(wxWebViewEvent& evt);
    void OnWebViewLoaded(wxWebViewEvent& evt);
    void OnWebViewError(wxWebViewEvent& evt);
    void OnWebViewScriptMessage(wxWebViewEvent& evt);
    void OnClose(wxCloseEvent& evt);
    void OnOpenSystemBrowser(wxMouseEvent& evt);
    void OnLinkMouseEnter(wxMouseEvent& evt);
    void OnLinkMouseLeave(wxMouseEvent& evt);
    
    // Initialize the UI
    void InitializeUI();

    // Get the login URL
    wxString GetLoginUrl();

protected:
    // Implements the DPIAware pure virtual function
    void on_dpi_changed(const wxRect &suggested_rect) override;

private:
    wxWebView* m_webView;
    wxPanel* m_panel;
    wxBoxSizer* m_mainSizer;
    wxStaticText* m_openSystemBrowserLink;

    
    wxString m_loginUrl;
    bool     m_login_succeeded { false };
    bool     m_close_event_sent { false };
    
    // Declare the event table
    wxDECLARE_EVENT_TABLE();
};

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_LoginDialog_hpp_