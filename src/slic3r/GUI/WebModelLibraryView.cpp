// [FORMATTED BY CLANG-FORMAT 2025-10-24 10:54:21]
#include "WebModelLibraryView.hpp"
#include "GUI_App.hpp"
#include "I18N.hpp"
#include "MainFrame.hpp"
#include "BitmapCache.hpp"
#include "print_manage/AppMgr.hpp"
#include "print_manage/AppUtils.hpp"
#include "Widgets/WebView.hpp"

#include <wx/webview.h>
#include <wx/webviewfshandler.h>
#include <wx/webviewarchivehandler.h>
#include <wx/fs_arc.h>
#include <wx/fs_mem.h>
#include <wx/stdpaths.h>
#include <wx/dir.h>
#include <wx/uri.h>
#include <wx/artprov.h>
#include <boost/log/trivial.hpp>

#ifdef __WIN32__
#include <WebView2.h>
#endif

namespace Slic3r { namespace GUI {

wxBEGIN_EVENT_TABLE(WebModelLibraryView, wxPanel) EVT_WEBVIEW_NAVIGATING(wxID_ANY, WebModelLibraryView::OnNavigating)
    EVT_WEBVIEW_NAVIGATED(wxID_ANY, WebModelLibraryView::OnNavigated) EVT_WEBVIEW_LOADED(wxID_ANY, WebModelLibraryView::OnLoaded)
        EVT_WEBVIEW_ERROR(wxID_ANY, WebModelLibraryView::OnError) EVT_WEBVIEW_NEWWINDOW(wxID_ANY, WebModelLibraryView::OnNewWindow)
            EVT_WEBVIEW_TITLE_CHANGED(wxID_ANY, WebModelLibraryView::OnTitleChanged)
                EVT_BUTTON(wxID_BACKWARD, WebModelLibraryView::OnBackButton) EVT_BUTTON(wxID_FORWARD, WebModelLibraryView::OnForwardButton)
                    EVT_BUTTON(wxID_REFRESH, WebModelLibraryView::OnRefreshButton) EVT_BUTTON(wxID_HOME, WebModelLibraryView::OnHomeButton)
                        wxEND_EVENT_TABLE()

                            WebModelLibraryView::WebModelLibraryView(wxWindow* parent, const wxString& url)
    : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize)
    , m_browser(nullptr)
    , m_toolbar_panel(nullptr)
    , m_back_btn(nullptr)
    , m_forward_btn(nullptr)
    , m_refresh_btn(nullptr)
    , m_home_btn(nullptr)
    , m_start_url(url)
    , m_current_url(url)
    , m_main_sizer(nullptr)
    , m_toolbar_sizer(nullptr)
{
    SetBackgroundColour(wxColour(54, 54, 56)); // Match the dark theme background

    CreateControls();
    CreateWebView();
    BindEvents();

    // Register with AppMgr for Routes system integration
    if (m_browser) {
        DM::AppMgr::Ins().Register(m_browser);
    }

    if (!m_start_url.IsEmpty()) {
        load_url(m_start_url);
    }
}

WebModelLibraryView::~WebModelLibraryView()
{
    // Unregister from AppMgr
    if (m_browser) {
        DM::AppMgr::Ins().UnRegister(m_browser);
        m_browser->Destroy();
        m_browser = nullptr;
    }
}

void WebModelLibraryView::CreateControls()
{
    m_main_sizer = new wxBoxSizer(wxVERTICAL);

    // Create toolbar panel (but don't add it to layout to hide it)
    m_toolbar_panel = new wxPanel(this, wxID_ANY);
    m_toolbar_panel->SetBackgroundColour(wxColour(238, 238, 238));

    m_toolbar_sizer = new wxBoxSizer(wxHORIZONTAL);

    // Create navigation buttons
    m_back_btn    = new wxButton(m_toolbar_panel, wxID_BACKWARD, _(L("Back")), wxDefaultPosition, wxSize(60, 30));
    m_forward_btn = new wxButton(m_toolbar_panel, wxID_FORWARD, _(L("Forward")), wxDefaultPosition, wxSize(60, 30));
    m_refresh_btn = new wxButton(m_toolbar_panel, wxID_REFRESH, _(L("Refresh")), wxDefaultPosition, wxSize(60, 30));
    m_home_btn    = new wxButton(m_toolbar_panel, wxID_HOME, _(L("Home")), wxDefaultPosition, wxSize(60, 30));

    // Add buttons to toolbar
    m_toolbar_sizer->Add(m_back_btn, 0, wxALL | wxALIGN_CENTER_VERTICAL, 2);
    m_toolbar_sizer->Add(m_forward_btn, 0, wxALL | wxALIGN_CENTER_VERTICAL, 2);
    m_toolbar_sizer->Add(m_refresh_btn, 0, wxALL | wxALIGN_CENTER_VERTICAL, 2);
    m_toolbar_sizer->Add(m_home_btn, 0, wxALL | wxALIGN_CENTER_VERTICAL, 2);
    m_toolbar_sizer->AddStretchSpacer();

    m_toolbar_panel->SetSizer(m_toolbar_sizer);

    // Hide the toolbar panel
    m_toolbar_panel->Hide();

    // Don't add toolbar to main sizer to completely hide it
    // m_main_sizer->Add(m_toolbar_panel, 0, wxEXPAND | wxALL, 0);

    SetSizer(m_main_sizer);
}

void WebModelLibraryView::CreateWebView()
{
    // Create webview using WebView::CreateWebView for proper user-agent handling
    m_browser = WebView::CreateWebView(this, wxEmptyString);

    if (m_browser) {
        // Add webview to main sizer
        m_main_sizer->Add(m_browser, 1, wxEXPAND, 0); // Fill entire space with no border

        // Set user agent (will be applied correctly due to WebView::CreateWebView usage)
        UpdateUserAgent();
        // Bind script message event
        m_browser->Bind(wxEVT_WEBVIEW_SCRIPT_MESSAGE_RECEIVED, &WebModelLibraryView::OnScriptMessage, this, m_browser->GetId());

        m_browser->RemoveScriptMessageHandler("wx");
        // Add script message handler for "CXSWGroupInterface" to receive postMessage from JavaScript
        CallAfter([this]() {
            if (!m_browser->AddScriptMessageHandler("CXSWGroupInterface")) {
                BOOST_LOG_TRIVIAL(error) << "Failed to add script message handler 'CXSWGroupInterface' for WebModelLibraryView";
            } else {
                BOOST_LOG_TRIVIAL(info) << "Successfully added script message handler 'CXSWGroupInterface' for WebModelLibraryView";
            }
        });

        // Enable developer tools in debug mode
#ifdef _DEBUG
        m_browser->EnableContextMenu(true);
        m_browser->EnableAccessToDevTools(); // Enable F12 developer tools
#else
        m_browser->EnableContextMenu(false);
        m_browser->EnableAccessToDevTools(); // Enable F12 developer tools for debugging
#endif
    }

    Layout();
}

void WebModelLibraryView::BindEvents()
{
    // Events are bound through the event table
}

void WebModelLibraryView::load_url(const wxString& url)
{
    if (m_browser && !url.IsEmpty()) {
        // Set cookies before navigation so the request carries expected state
        SetCookiesForUrl(url);
        // Do not update the UA here; the UA is only set during initialization, and other cases only refresh cookies
        m_current_url = url;
        m_browser->LoadURL(url);
        BOOST_LOG_TRIVIAL(info) << "load_url: Loading URL: " << url.ToStdString();
    }
}

void WebModelLibraryView::SetStartPage(const wxString& url)
{
    m_start_url   = url;
    m_current_url = url;
}

void WebModelLibraryView::RunScript(const wxString& javascript)
{
    if (m_browser) {
        m_browser->RunScript(javascript);
    }
}

void WebModelLibraryView::Reload()
{
    if (m_browser) {
        m_browser->Reload();
    }
}

void WebModelLibraryView::GoBack()
{
    if (m_browser && m_browser->CanGoBack()) {
        m_browser->GoBack();
    }
}

void WebModelLibraryView::GoForward()
{
    if (m_browser && m_browser->CanGoForward()) {
        m_browser->GoForward();
    }
}

bool WebModelLibraryView::CanGoBack() { return m_browser ? m_browser->CanGoBack() : false; }

bool WebModelLibraryView::CanGoForward() { return m_browser ? m_browser->CanGoForward() : false; }

bool WebModelLibraryView::Show(bool show) { return wxPanel::Show(show); }

void WebModelLibraryView::Hide() { wxPanel::Hide(); }

bool WebModelLibraryView::IsShown() const { return wxPanel::IsShown(); }

bool WebModelLibraryView::IsInitialized() const
{
    // Consider the first load complete once m_current_url is set (and is not the default empty value)
    return !m_current_url.IsEmpty() && m_current_url != wxWebViewDefaultURLStr;
}

// Event handlers
void WebModelLibraryView::OnNavigating(wxWebViewEvent& evt)
{
    // Allow navigation
    evt.Skip();
}

void WebModelLibraryView::OnNavigated(wxWebViewEvent& evt)
{
    m_current_url = evt.GetURL();

    // Update button states
    if (m_back_btn) {
        m_back_btn->Enable(CanGoBack());
    }
    if (m_forward_btn) {
        m_forward_btn->Enable(CanGoForward());
    }

    evt.Skip();
}

void WebModelLibraryView::OnLoaded(wxWebViewEvent& evt)
{
    // Page loaded successfully
    BOOST_LOG_TRIVIAL(info) << "DEBUG: ModelLibrary page loaded, requesting account info";
    // Linux: inject cookies via JS to sync login state, avoiding the WebKit assertion triggered when the UA carries a custom segment
#if defined(__linux__) || defined(__WXMAC__)
    if (m_browser) {
        wxString current_url = m_browser->GetCurrentURL();
        if (!current_url.IsEmpty() && current_url != wxWebViewDefaultURLStr) {
            wxURI    uri(current_url);
            wxString host = uri.GetServer();
            wxString domain;
            wxString lowerHost = host.Lower();
            if (lowerHost.Contains("crealitycloud.cn"))
                domain = ".crealitycloud.cn";
            else if (lowerHost.Contains("crealitycloud.com"))
                domain = ".crealitycloud.com";
            else if (lowerHost.Contains("creality.cn"))
                domain = ".creality.cn";
            else
                domain = host;

            std::string domain_std = domain.ToStdString();
            if (m_cookie_injected_domains.find(domain_std) == m_cookie_injected_domains.end()) {
                m_cookie_injected_domains.insert(domain_std);

                std::map<std::string, std::string> header_map = wxGetApp().get_modellibrary_header();
                wxString                           js;
                js += wxString::Format("try{document.cookie='_DARK_MODE=%s;domain=%s;path=/;SameSite=Lax;Secure';}catch(e){};",
                                       Slic3r::GUI::wxGetApp().dark_mode() ? "1" : "0", domain);
                for (const auto& kv : header_map) {
                    wxString name  = wxString::FromUTF8(kv.first.c_str());
                    wxString value = wxString::FromUTF8(kv.second.c_str());
                    js += wxString::Format("try{document.cookie='%s=%s;domain=%s;path=/;SameSite=Lax;Secure';}catch(e){};", name, value,
                                           domain);
                }
                BOOST_LOG_TRIVIAL(info) << "Linux/Mac cookie injection for domain: " << domain_std;
                m_browser->RunScript(js);
                m_browser->Reload();
            }
        }
    }
#endif
    // Send account info to ModelLibrary page to sync login status
    evt.Skip();
}

void WebModelLibraryView::OnError(wxWebViewEvent& evt)
{
    // Handle navigation error
    wxLogError("WebView navigation error: %s", evt.GetString());
    evt.Skip();
}

void WebModelLibraryView::OnNewWindow(wxWebViewEvent& evt)
{
    // Handle new window request - load in current window
    load_url(evt.GetURL());
    evt.Veto();
}

void WebModelLibraryView::OnTitleChanged(wxWebViewEvent& evt)
{
    // Handle title change
    evt.Skip();
}

bool WebModelLibraryView::needLogin(std::string cmd)
{
    try {
        // parse the JSON string
        json j = json::parse(cmd);

        // check whether the action field is "toNative"
        if (j.contains("action") && j["action"].is_string()) {
            std::string action = j["action"].get<std::string>();
            if (action != "toNative") {
                return false;
            }
        } else {
            return false;
        }

        // check whether the message field exists and is an object
        if (j.contains("message") && j["message"].is_object()) {
            json message = j["message"];

            // check the linkType field
            if (message.contains("linkType") && message["linkType"].is_number()) {
                int linkType = message["linkType"].get<int>();

                // linkType=1 means login is required
                if (linkType == 1) {
                    // send the command that triggers a login check to the JavaScript side
                    std::string message = "{\"command\":\"trigger_login_check\"}";
                     // send it to JavaScript through the existing message mechanism
                    std::string result = wxGetApp().handle_web_request(message);

                    // return true to indicate login is required
                    return true;
                }
                // linkType=403 is not handled here; return false to indicate login is not required
            }
        }

        // by default login is not required
        return false;

    } catch (const std::exception& e) {
        // JSON parsing failed, log the error
        BOOST_LOG_TRIVIAL(error) << "Failed to parse needLogin command: " << e.what();
        return false;
    }
}

bool WebModelLibraryView::handleLinkType403(std::string cmd)
{
    try {
        // parse the JSON string
        json j = json::parse(cmd);

        // check whether the action field is "toNative"
        if (j.contains("action") && j["action"].is_string()) {
            std::string action = j["action"].get<std::string>();
            if (action != "toNative") {
                return false;
            }
        } else {
            return false;
        }

        // check whether the message field exists and is an object
        if (j.contains("message") && j["message"].is_object()) {
            json message = j["message"];

            // check the linkType field
            if (message.contains("linkType") && message["linkType"].is_number()) {
                int linkType = message["linkType"].get<int>();

                // handle the linkType=403 case
                if (linkType == 403) {
                    if (message.contains("linkUrl") && message["linkUrl"].is_object()) {
                        json linkJson = message["linkUrl"];
                        if (linkJson.contains("url") && linkJson["url"].is_string()) {
                            std::string url = linkJson["url"].get<std::string>();
                            wxLaunchDefaultBrowser(wxString::FromUTF8(url));
                            return true; // indicates it has been handled
                        }
                    }
                }
            }
        }

        return false; // not linkType=403 or handling failed

    } catch (const std::exception& e) {
        // JSON parsing failed, log the error
        BOOST_LOG_TRIVIAL(error) << "Failed to parse handleLinkType403 command: " << e.what();
        return false;
    }
}
bool WebModelLibraryView::handleLinkType404(std::string cmd)
{
    try {
        json j = json::parse(cmd);
        if (!j.contains("action") || !j["action"].is_string() || j["action"].get<std::string>() != "toNative")
            return false;
        if (!j.contains("message") || !j["message"].is_object())
            return false;

        json message = j["message"];
        if (!message.contains("linkType") || !message["linkType"].is_number())
            return false;

        int linkType = message["linkType"].get<int>();
        if (linkType != 404)
            return false;

        // forward 404 to the community WebView, triggering the community-side token-expired popup
        wxGetApp().mainframe->select_tab(MainFrame::tpHome);
        wxGetApp().swith_community_sub_page("token_expired");
        BOOST_LOG_TRIVIAL(info) << "WebModelLibraryView: forward linkType=404 to community (token_expired).";
        return true;
    } catch (const std::exception &e) {
        BOOST_LOG_TRIVIAL(error) << "Failed to parse handleLinkType404 command: " << e.what();
        return false;
    }
}
void WebModelLibraryView::OnScriptMessage(wxWebViewEvent& evt)
{
    // Handle script messages from web page
    std::string request = evt.GetString().ToUTF8().data();
    BOOST_LOG_TRIVIAL(info) << "DEBUG: OnScriptMessage received: " << request;

    if (needLogin(request)) {
        return;
    }

    // Handle linkType=403 case (open URL in browser)
    if (handleLinkType403(request)) {
        return;
    }
    if (handleLinkType404(request)) {
        return;
    }
    // Try to use AppMgr's Routes system first, fallback to original method
    if (DM::AppMgr::Ins().Invoke(m_browser, request)) {
        return; // Routes system handled the message
    }

    // Fallback to original method if Routes system didn't handle it
    std::string response = wxGetApp().handle_web_request(request);
    BOOST_LOG_TRIVIAL(info) << "DEBUG: OnScriptMessage response: " << response;

    if (!response.empty()) {
        wxString strJS = wxString::Format("window.handleStudioCmd(%s)", response);
        BOOST_LOG_TRIVIAL(info) << "DEBUG: OnScriptMessage sending JS: " << strJS.ToStdString();
        m_browser->RunScript(strJS);
    }
    evt.Skip();
}

// Button event handlers
void WebModelLibraryView::OnBackButton(wxCommandEvent& evt) { GoBack(); }

void WebModelLibraryView::OnForwardButton(wxCommandEvent& evt) { GoForward(); }

void WebModelLibraryView::OnRefreshButton(wxCommandEvent& evt) { Reload(); }

void WebModelLibraryView::OnHomeButton(wxCommandEvent& evt)
{
    if (!m_start_url.IsEmpty()) {
        load_url(m_start_url);
    }
}

bool WebModelLibraryView::UpdateUserAgent()
{
    if (m_browser) {
        wxString current_url = m_browser->GetCurrentURL();
        BOOST_LOG_TRIVIAL(error) << "UpdateUserAgent: current_url=" << current_url.ToStdString() 
                                << ", m_current_url=" << m_current_url.ToStdString();
#ifdef __linux__
        // the UA is set only on first initialization; later calls no longer change the UA
        if (!m_ua_initialized) {
            wxString user_agent = wxString::Format(
                "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) "
                "sanity_print_slice/%s WebView/%s",
                SANITYPRINT_VERSION, "141.0.0.0");
            m_browser->SetUserAgent(user_agent);
            m_ua_initialized   = true;
            BOOST_LOG_TRIVIAL(info) << "UpdateUserAgent: UA initialized (Linux): " << user_agent.ToStdString();
        } else {
            BOOST_LOG_TRIVIAL(info) << "UpdateUserAgent: UA already initialized, skipping UA change (Linux)";
        }
#else
        if (!m_ua_initialized) {
            wxString user_agent = wxString::Format(
                "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) "
                "sanity_print_slice/%s webview/%s;",
                SANITYPRINT_VERSION, "141.0.0.0");
            m_browser->SetUserAgent(user_agent);
            m_ua_initialized   = true;
            BOOST_LOG_TRIVIAL(info) << "UpdateUserAgent: UA initialized (Windows): " << user_agent.ToStdString();
        } else {
            BOOST_LOG_TRIVIAL(info) << "UpdateUserAgent: UA already initialized, skipping UA change (Windows)";
        }
#endif

        // use the browser's current URL to set cookies, ensuring cookies are set on the correct domain
        // fix: cookies not updated after login; use current_url instead of m_current_url
        if (!current_url.IsEmpty() && current_url != wxWebViewDefaultURLStr)
            SetCookiesForUrl(current_url);
#if defined(__linux__) || defined(__WXMAC__)
        if (!current_url.IsEmpty() && current_url != wxWebViewDefaultURLStr) {
            wxURI    uri(current_url);
            wxString host = uri.GetServer();
            wxString domain;
            wxString lowerHost = host.Lower();
            if (lowerHost.Contains("crealitycloud.cn"))
                domain = ".crealitycloud.cn";
            else if (lowerHost.Contains("crealitycloud.com"))
                domain = ".crealitycloud.com";
            else if (lowerHost.Contains("creality.cn"))
                domain = ".creality.cn";
            else
                domain = host;

            std::map<std::string, std::string> header_map = wxGetApp().get_modellibrary_header();
            wxString                           js;
            js += wxString::Format("try{document.cookie='_DARK_MODE=%s;domain=%s;path=/;SameSite=Lax;Secure';}catch(e){};",
                                   Slic3r::GUI::wxGetApp().dark_mode() ? "1" : "0", domain);
            for (const auto& kv : header_map) {
                wxString name  = wxString::FromUTF8(kv.first.c_str());
                wxString value = wxString::FromUTF8(kv.second.c_str());
                js += wxString::Format("try{document.cookie='%s=%s;domain=%s;path=/;SameSite=Lax;Secure';}catch(e){};", name, value, domain);
            }
            BOOST_LOG_TRIVIAL(info) << "Linux/Mac cookie refresh in UpdateUserAgent for domain: " << domain.ToStdString();
            m_browser->RunScript(js);
        }
#endif
        // Force reload to apply new user agent and refreshed cookies
        if (!current_url.IsEmpty() && current_url != wxWebViewDefaultURLStr) {
            m_browser->Reload();
            BOOST_LOG_TRIVIAL(info) << "UpdateUserAgent: Reloading once to apply initialized UA";
        }
    }
    return true;
}


bool WebModelLibraryView::SetCookiesForUrl(const wxString& url)
{
    if (!m_browser || url.IsEmpty()) {
        BOOST_LOG_TRIVIAL(error) << "SetCookiesForUrl: browser or url is empty";
        return false;
    }

#ifdef __WIN32__
    BOOST_LOG_TRIVIAL(error) << "SetCookiesForUrl: Setting cookies for URL: " << url.ToStdString();

    // Extract domain for cookie binding
    std::string domain = DM::AppUtils::extractDomain(url.ToStdString());
    if (domain.empty()) {
        BOOST_LOG_TRIVIAL(error) << "SetCookiesForUrl: failed to extract domain from URL";
        return false;
    }
    BOOST_LOG_TRIVIAL(error) << "SetCookiesForUrl: extracted domain: " << domain;

    // Promote to base domain for subdomain sharing on Creality sites
    wxString dwx = wxString::FromUTF8(domain);
    wxString lower = dwx.Lower();
    if (lower.Contains("crealitycloud.cn"))
        domain = ".crealitycloud.cn";
    else if (lower.Contains("crealitycloud.com"))
        domain = ".crealitycloud.com";
    else if (lower.Contains("creality.cn"))
        domain = ".creality.cn";

    ICoreWebView2* webView2 = (ICoreWebView2*) m_browser->GetNativeBackend();
    if (!webView2)
        return false;

    ICoreWebView2_2* webView2_2 = nullptr;
    HRESULT          hr         = webView2->QueryInterface(&webView2_2);
    if (hr != S_OK || !webView2_2)
        return false;

    ICoreWebView2CookieManager* cookieManager = nullptr;
    hr                                        = webView2_2->get_CookieManager(&cookieManager);
    if (hr != S_OK || !cookieManager) {
        webView2_2->Release();
        return false;
    }

    auto addCookie = [&](const wxString& name, const wxString& value) {
        ICoreWebView2Cookie* cookie = nullptr;
        HRESULT hr2 = cookieManager->CreateCookie(name.wc_str(), value.wc_str(), wxString::FromUTF8(domain).wc_str(), L"/", &cookie);
        if (hr2 == S_OK && cookie) {
            // LAX same-site keeps requests working while preventing cross-site issues
            cookie->put_SameSite(COREWEBVIEW2_COOKIE_SAME_SITE_KIND_LAX);
            // Not HttpOnly so page scripts can read if needed
            cookie->put_IsHttpOnly(FALSE);
            cookie->put_IsSecure(FALSE);
            HRESULT hrAdd = cookieManager->AddOrUpdateCookie(cookie);
            if (hrAdd != S_OK) {
                BOOST_LOG_TRIVIAL(error) << "SetCookiesForUrl: failed to add cookie " << name.ToStdString() << ", hr=" << hrAdd;
            }
            cookie->Release();
        } else {
            BOOST_LOG_TRIVIAL(error) << "SetCookiesForUrl: failed to create cookie " << name.ToStdString() << ", hr=" << hr2;
        }
    };

    // Dark mode cookie expected by front-end: 0 light, 1 dark
    addCookie("_DARK_MODE", Slic3r::GUI::wxGetApp().dark_mode() ? "1" : "0");

    // Also mirror header values (token, uid, lang) into cookies if present
    std::map<std::string, std::string> header_map = wxGetApp().get_modellibrary_header();
    BOOST_LOG_TRIVIAL(error) << "SetCookiesForUrl: setting " << header_map.size() + 1 << " cookies";
    for (const auto& kv : header_map) {
        addCookie(wxString::FromUTF8(kv.first), wxString::FromUTF8(kv.second));
    }

    cookieManager->Release();
    webView2_2->Release();
    BOOST_LOG_TRIVIAL(error) << "SetCookiesForUrl: cookies set successfully for domain: " << domain;
    return true;
#else
    (void) url;
    return false;
#endif
}

}} // namespace Slic3r::GUI
