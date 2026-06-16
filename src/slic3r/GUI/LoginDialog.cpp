#include "LoginDialog.hpp"

#include "I18N.hpp"
#include "slic3r/GUI/wxExtensions.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/MainFrame.hpp"
#include "libslic3r_version.h"
#include "libslic3r/Utils.hpp"
#include "libslic3r/common_header/common_header.h"

#include <wx/sizer.h>
#include <wx/button.h>
#include <wx/panel.h>
#include <wx/msgdlg.h>
#include <wx/webview.h>
#include <wx/utils.h>
#include <wx/uri.h>
#include <wx/hyperlink.h>
#include <wx/stattext.h>

#include <slic3r/GUI/Widgets/WebView.hpp>
#include <nlohmann/json.hpp>
#include <boost/log/trivial.hpp>

namespace Slic3r {
    namespace GUI {

        // Event table
wxBEGIN_EVENT_TABLE(LoginDialog, wxDialog)
    EVT_WEBVIEW_NAVIGATING(wxID_ANY, LoginDialog::OnWebViewNavigating)
    EVT_WEBVIEW_NEWWINDOW(wxID_ANY, LoginDialog::OnWebViewNewWindow)
    EVT_WEBVIEW_LOADED(wxID_ANY, LoginDialog::OnWebViewLoaded)
    EVT_WEBVIEW_ERROR(wxID_ANY, LoginDialog::OnWebViewError)
    EVT_CLOSE(LoginDialog::OnClose)
wxEND_EVENT_TABLE()

        LoginDialog::LoginDialog(wxWindow* parent, const wxString& title)
    : DPIDialog(parent, wxID_ANY, title, wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
    , m_webView(nullptr)
    , m_panel(nullptr)
    , m_mainSizer(nullptr)
    , m_openSystemBrowserLink(nullptr)
{
    InitializeUI();

    // Set the initial window size (use FromDIP only after construction to avoid a null-pointer crash during base-class initialization)
    SetSize(FromDIP(wxSize(630, 780)));
    SetMinSize(FromDIP(wxSize(520, 600)));

    // Set the dialog icon
    std::string icon_path = (boost::format("%1%/images/%2%.ico") % resources_dir() % Slic3r::CxBuildInfo::getIconName()).str();
    SetIcon(wxIcon(encode_path(icon_path.c_str()), wxBITMAP_TYPE_ICO));

    // Center on screen
    CenterOnParent();
}

        LoginDialog::~LoginDialog()
        {
            if (m_webView) {
                m_webView->Destroy();
                m_webView = nullptr;
            }
        }

        void LoginDialog::InitializeUI()
        {
            auto dark = Slic3r::GUI::wxGetApp().dark_mode();
            this->SetBackgroundColour(dark ? wxColour("#1c1e22") :wxColour("#f4f7fb") );
            // Create the main panel
            m_panel = new wxPanel(this, wxID_ANY);

            // Create the main layout
            m_mainSizer = new wxBoxSizer(wxVERTICAL);

            // Create the WebView (without loading any URL)
            m_webView = WebView::CreateWebView(m_panel, wxEmptyString);
            if (m_webView == nullptr) {
                BOOST_LOG_TRIVIAL(error) << "Could not create WebView for login dialog";

                // Show an error message to the user
                wxStaticText* errorText = new wxStaticText(m_panel, wxID_ANY, 
                    _("Failed to initialize web browser component.\nPlease ensure Microsoft Edge WebView2 is installed."));
                errorText->SetForegroundColour(*wxRED);
                m_mainSizer->Add(errorText, 1, wxEXPAND | wxALL | wxALIGN_CENTER, FromDIP(20));
            } else {
                // Enable developer tools (for debugging)
                m_webView->EnableAccessToDevTools();

                // Add the WebView to the layout
                m_mainSizer->Add(m_webView, 1, wxEXPAND | wxALL, FromDIP(5));

                // Bind WebView events
                m_webView->Bind(wxEVT_WEBVIEW_SCRIPT_MESSAGE_RECEIVED, &LoginDialog::OnWebViewScriptMessage, this, m_webView->GetId());
                // Bind CXSWGroupInterface as the script message channel
                m_webView->RemoveScriptMessageHandler("wx");
                CallAfter([this]() {
                    if (!m_webView)
                        return;
                    if (!m_webView->AddScriptMessageHandler("CXSWGroupInterface")) {
                        BOOST_LOG_TRIVIAL(error) << "Failed to add script message handler 'CXSWGroupInterface' for LoginDialog";
                    } else {
                        BOOST_LOG_TRIVIAL(info) << "Successfully added script message handler 'CXSWGroupInterface' for LoginDialog";
                    }
                });
            }

            // Set the panel layout
            m_panel->SetSizer(m_mainSizer);

            // Use static text to mimic a link style: no underline by default, underline on hover
            m_openSystemBrowserLink = new wxStaticText(
                m_panel,
                wxID_ANY,
                _L("System Browser Login"),
                wxDefaultPosition,
                wxDefaultSize);

            {
                wxFont f = GetFont();
                f.SetPointSize(f.GetPointSize() + 2);
                f.SetUnderlined(false);
                m_openSystemBrowserLink->SetFont(f);

                const bool is_dark = Slic3r::GUI::wxGetApp().dark_mode();
                const wxColour normal = wxColour("#2E86C1");//is_dark ? wxColour(0x67, 0xC2, 0x3A) : wxColour(0x19, 0x90, 0xFF);
                m_openSystemBrowserLink->SetForegroundColour(normal);
                m_openSystemBrowserLink->SetCursor(wxCursor(wxCURSOR_HAND));
            }

            m_openSystemBrowserLink->Bind(wxEVT_ENTER_WINDOW, &LoginDialog::OnLinkMouseEnter, this);
            m_openSystemBrowserLink->Bind(wxEVT_LEAVE_WINDOW, &LoginDialog::OnLinkMouseLeave, this);
            m_openSystemBrowserLink->Bind(wxEVT_LEFT_UP, &LoginDialog::OnOpenSystemBrowser, this);
            m_mainSizer->Add(m_openSystemBrowserLink, 0, wxALIGN_CENTER_HORIZONTAL | wxBOTTOM | wxTOP, FromDIP(20));

            // Create the dialog layout
            wxBoxSizer* dialogSizer = new wxBoxSizer(wxVERTICAL);
            dialogSizer->Add(m_panel, 1, wxEXPAND);
            SetSizer(dialogSizer);

            Layout();

        }

        void LoginDialog::ShowLoginDialog(const wxString& loginUrl)
        {
            wxString urlToLoad = loginUrl.IsEmpty() ? GetLoginUrl() : loginUrl;
            // Embedded WebView login: append the webview=1 parameter as required by the documentation
            auto append_param = [](const wxString& url, const wxString& key, const wxString& value) {
                if (url.IsEmpty())
                    return url;
                // Preserve the fragment part to avoid concatenating it incorrectly
                wxString base = url;
                wxString fragment;
                int      fragPos = url.Find('#');
                if (fragPos != wxNOT_FOUND) {
                    base     = url.Left(fragPos);
                    fragment = url.Mid(fragPos);
                }
                wxString lower = base.Lower();
                if (lower.Contains(key.Lower() + "="))
                    return url; // Parameter already present, return the original URL as-is
                wxString sep = base.Contains("?") ? "&" : "?";
                return base + sep + key + "=" + value + fragment;
            };
            wxURI    parsed(urlToLoad);
            wxString host = parsed.GetServer().Lower();
            if (!host.IsEmpty() && (host.Contains("creality.com") || host.Contains("creality.cn")) ) {
                urlToLoad = append_param(urlToLoad, wxT("webview"), wxT("1"));
            }
            m_loginUrl = urlToLoad;
            
            if (m_webView) {
                BOOST_LOG_TRIVIAL(error) << "Loading login URL: " << urlToLoad.ToStdString();
                m_webView->LoadURL(m_loginUrl);
            } else {
                BOOST_LOG_TRIVIAL(error) << "LoginDialog::WebView is not initialized";
                CallAfter([this]() {
                    wxMessageBox(_("Failed to initialize web browser component."), _("Login Error"), wxOK | wxICON_ERROR, this);
                });
            }
            // The static text does not need to sync the URL; the click event reads m_loginUrl directly
        }

        void LoginDialog::MarkLoginSucceeded()
        {
            m_login_succeeded = true;
        }

        wxString LoginDialog::GetLoginUrl()
        {
            // Return the URL of the login page here
            // Modify this to the correct login URL as needed
            return wxT("");
        }

        void LoginDialog::OnWebViewNavigating(wxWebViewEvent& evt)
        {
            wxString url = evt.GetURL();
            BOOST_LOG_TRIVIAL(error) << "WebView navigating to: " << url.ToStdString();

            // Extract and normalize the host
            wxURI    parsed(url);
            wxString host = parsed.GetServer().Lower();
            if (host.IsEmpty()) {
                int schemePos = url.Find("://");
                if (schemePos != wxNOT_FOUND) {
                    wxString rest      = url.Mid(schemePos + 3);
                    int      slashPos  = rest.Find('/');
                    wxString hostGuess = (slashPos == wxNOT_FOUND) ? rest : rest.Left(slashPos);
                    if (!hostGuess.IsEmpty())
                        host = hostGuess.Lower();
                }
            }

            auto is_internal_auth_host = [](const wxString& h) {
                // Internal account / phone login domains (do not redirect to the system browser)
                return  h.Contains("id.creality") || h.Contains("id-dev.creality") || h.Contains("www.creality") || h.Contains("pre.creality")  ;
            };

            auto is_third_party_host = [](const wxString& h) {
                // Common third-party login domains (require redirecting to the system browser)
                return h.Contains("open.weixin") || h.Contains("weixin.qq.com") || h.Contains("connect.qq.com") ||
                       h.Contains("graph.qq.com") || h.Contains("facebook.com") || h.Contains("accounts.google.com") ||
                       h.Contains("google.com") || h.Contains("github.com") || h.Contains("apple.com");
            };

            // Local callback: only recognized when host is localhost/127.0.0.1 and the path starts with /login
            {
                wxString path = parsed.GetPath();
                const bool is_localhost = (host == "localhost" || host == "127.0.0.1");
                // Using Find("/login")==0 is more compatible with older versions of wxWidgets
                if (is_localhost && path.Find(wxT("/login")) == 0) {
                    BOOST_LOG_TRIVIAL(error) << "Local OAuth callback navigating to: host=" << host.ToStdString()
                                             << " path=" << path.ToStdString();
                    // Allow navigation to continue so the request reaches the local callback server; do not close the window here
                    return;
                }
            }

            // Internal account / phone login: allow navigation within the embedded WebView
            if (is_internal_auth_host(host) || ( url.Contains("/oauth?code") && url.Contains("redirect_uri"))) {
                BOOST_LOG_TRIVIAL(error) << "Internal auth host detected, keep in-webview: " << host.ToStdString();
                return; // Allow navigation to continue
            }

            // Third-party login: redirect to the system browser and close the current window
            if (is_third_party_host(host)) {
                BOOST_LOG_TRIVIAL(error) << "Third-party auth host detected, redirecting to browser and closing: " << host.ToStdString();
                wxLaunchDefaultBrowser(url);
                evt.Veto();
                return;
            }

            // Other unknown domains: open in the external browser by default to keep the embedded WebView from leaving the login flow, and close the window
            BOOST_LOG_TRIVIAL(error) << "Unknown host, default to external browser and close: " << host.ToStdString();
            wxLaunchDefaultBrowser(url);
            evt.Veto();
        }

        void LoginDialog::OnWebViewNewWindow(wxWebViewEvent& evt)
        {
            // Third-party login already redirects to the system browser directly on click via the CXSWGroupInterface JSON message,
            // so no extra handling is done here; uniformly prevent new windows from popping up inside the WebView to keep the login flow simple.
            BOOST_LOG_TRIVIAL(info) << "New-window requested, veto under external-login flow: " << evt.GetURL().ToStdString();
            evt.Veto();
            return;
            wxString url = evt.GetURL();
            BOOST_LOG_TRIVIAL(error) << "WebView new-window requested: " << url.ToStdString();

            // Normalize host
            wxURI    parsed(url);
            wxString host = parsed.GetServer().Lower();
            if (host.IsEmpty()) {
                int schemePos = url.Find("://");
                if (schemePos != wxNOT_FOUND) {
                    wxString rest      = url.Mid(schemePos + 3);
                    int      slashPos  = rest.Find('/');
                    wxString hostGuess = (slashPos == wxNOT_FOUND) ? rest : rest.Left(slashPos);
                    if (!hostGuess.IsEmpty())
                        host = hostGuess.Lower();
                }
            }

            // Local OAuth callback: if the target is localhost/127.0.0.1 and the path starts with /login,
            // load it within the current dialog to ensure the callback reaches the local server, avoiding the external browser intercepting it and leaving the state un-updated.
            {
                wxString path = parsed.GetPath();
                const bool is_localhost = (host == "localhost" || host == "127.0.0.1");
                if (is_localhost && path.Find(wxT("/login")) == 0) {
                    BOOST_LOG_TRIVIAL(error) << "New-window to local callback detected, loading in-webview: host="
                                              << host.ToStdString() << " path=" << path.ToStdString();
                    if (m_webView) {
                        m_webView->LoadURL(url);
                    }
                    evt.Veto();
                    return;
                }
            }

            auto is_internal_auth_host = [](const wxString& h) {
                return h.Contains("id.creality") || h.Contains("id-dev.creality") || h.Contains("www.creality");
             };
            auto is_third_party_host = [](const wxString& h) {
                return h.Contains("open.weixin") || h.Contains("weixin.qq.com") || h.Contains("connect.qq.com") ||
                       h.Contains("graph.qq.com") || h.Contains("facebook.com") || h.Contains("accounts.google.com") ||
                       h.Contains("google.com") || h.Contains("github.com") || h.Contains("apple.com");
            };

            // Internal auth flows: load inside dialog
            if (is_internal_auth_host(host) || ( url.Contains("/oauth?code") && url.Contains("redirect_uri") )) {
                m_webView->LoadURL(url);
                //evt.Veto();
                return;
            }

            // Third-party auth: launch external browser
            if (is_third_party_host(host)) {
                wxLaunchDefaultBrowser(url);
                evt.Veto();
                return;
            }

            // Unknown: default to external browser to avoid breaking login flow
            wxLaunchDefaultBrowser(url);
            evt.Veto();
        }

        void LoginDialog::OnWebViewLoaded(wxWebViewEvent& evt)
        {
            // Page finished loading
            BOOST_LOG_TRIVIAL(error) << "WebView page loaded successfully";
        }

        void LoginDialog::OnWebViewError(wxWebViewEvent& evt)
        {
            wxString technicalError = evt.GetString();
            wxLogError("WebView error: %s", technicalError);


            //The alpha + dev versions require a second login to the internal account, so this is suppressed
            //// Provide a user-friendly error message
            //wxString userFriendlyMsg = _("Failed to load the login page. Please check your internet connection and try again.");
            //
            //// Use CallAfter to avoid reentrancy issues from creating a modal dialog inside the WebView2 event handler
            //CallAfter([this, userFriendlyMsg, technicalError]() {
            //    wxString fullMsg = userFriendlyMsg + "\n\n" + _("Technical details: ") + technicalError;
            //    wxMessageBox(fullMsg, _("Login Error"), wxOK | wxICON_ERROR, this);
            //    EndModal(wxID_CANCEL);
            //});
        }

        void LoginDialog::OnWebViewScriptMessage(wxWebViewEvent& evt)
        {
            wxString message = evt.GetString();

            // Use CallAfter to avoid WebView event reentrancy
            CallAfter([this, message]() {
                try {
                    auto j = nlohmann::json::parse(message.ToStdString());

                    // Only handle the { action: "toNative", message: { ... } } format
                    if (j.contains("action") && j["action"].is_string() && j["action"].get<std::string>() == "toNative" &&
                        j.contains("message") && j["message"].is_object()) {
                        const auto& msg = j["message"];

                        // Handle the callback field first (the local callback address returned by the third-party login button)
                        if (msg.contains("callback") && msg["callback"].is_string()) {
                            std::string cb = msg["callback"].get<std::string>();
                            if (!cb.empty()) {
                                BOOST_LOG_TRIVIAL(info) << "LoginDialog: opening callback URL in browser: " << cb;
                                wxLaunchDefaultBrowser(wxString::FromUTF8(cb));
                                return;
                            }
                        }
                    }

                    // No matching target message, log it
                    BOOST_LOG_TRIVIAL(error) << "LoginDialog: script message ignored: " << message.ToStdString();
                } catch (const std::exception& e) {
                    BOOST_LOG_TRIVIAL(error) << "LoginDialog::OnWebViewScriptMessage JSON parse error: " << e.what();
                    BOOST_LOG_TRIVIAL(error) << "Raw message: " << message.ToStdString();
                }
            });
        }

        void LoginDialog::OnClose(wxCloseEvent& evt)
        {
            if (!m_login_succeeded && !m_close_event_sent) {
                m_close_event_sent = true;
                nlohmann::json event_json = {
                    {"command", "login_dialog_event"},
                    {"event", "close"}
                };
                wxString strJS = wxString::Format("window.handleStudioCmd(%s)", event_json.dump(-1, ' ', true, nlohmann::json::error_handler_t::ignore));
                GUI::wxGetApp().run_script(strJS);
            }
            evt.Skip();
        }

        void LoginDialog::OnOpenSystemBrowser(wxMouseEvent& evt)
        {
            evt.Skip(false);
            wxString urlToOpen = m_loginUrl.IsEmpty() ? GetLoginUrl() : m_loginUrl;
            // System browser login does not need webview=1, so remove that parameter
            auto remove_param = [](const wxString& url, const wxString& key) {
                if (url.IsEmpty())
                    return url;
                // Split off the fragment to prevent contamination
                wxString base = url;
                wxString fragment;
                int      fragPos = url.Find('#');
                if (fragPos != wxNOT_FOUND) {
                    base     = url.Left(fragPos);
                    fragment = url.Mid(fragPos);
                }
                int qPos = base.Find('?');
                if (qPos == wxNOT_FOUND)
                    return url; // No query parameters
                wxString      path  = base.Left(qPos);
                wxString      query = base.Mid(qPos + 1);
                wxArrayString parts = wxSplit(query, '&');
                wxString      newQuery;
                for (auto& p : parts) {
                    wxString lower = p.Lower();
                    if (lower.StartsWith(key.Lower() + "="))
                        continue; // Filter out the target parameter
                    if (!newQuery.IsEmpty())
                        newQuery += "&";
                    newQuery += p;
                }
                wxString rebuilt = path;
                if (!newQuery.IsEmpty())
                    rebuilt += "?" + newQuery;
                return rebuilt + fragment;
            };
            //urlToOpen = remove_param(urlToOpen, wxT("webview"));
            if (!urlToOpen.IsEmpty())
                wxLaunchDefaultBrowser(urlToOpen);
        }

        void LoginDialog::OnLinkMouseEnter(wxMouseEvent& evt)
        {
            if (!m_openSystemBrowserLink) return;
            wxFont f = m_openSystemBrowserLink->GetFont();
            f.SetUnderlined(true);
            m_openSystemBrowserLink->SetFont(f);

            evt.Skip();
        }

        void LoginDialog::OnLinkMouseLeave(wxMouseEvent& evt)
        {
            if (!m_openSystemBrowserLink) return;
            wxFont f = m_openSystemBrowserLink->GetFont();
            f.SetUnderlined(false);
            m_openSystemBrowserLink->SetFont(f);

            evt.Skip();
        }

        void LoginDialog::on_dpi_changed(const wxRect &suggested_rect)
        {
            if (m_mainSizer) {
                m_mainSizer->Layout();
            }
            if (m_openSystemBrowserLink) {
                wxFont f = GetFont();
                f.SetPointSize(f.GetPointSize() + 2);
                f.SetUnderlined(false);
                m_openSystemBrowserLink->SetFont(f);
            }
            SetSize(suggested_rect.GetSize());
            Refresh();
        }
    }
}// namespace Slic3r::GU