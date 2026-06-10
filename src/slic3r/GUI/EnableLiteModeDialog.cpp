#include "EnableLiteModeDialog.hpp"
#include "GUI_App.hpp"
#include "BitmapCache.hpp"
#include <wx/dcgraph.h>
#include <slic3r/GUI/I18N.hpp>
#include <wx/html/htmlwin.h>
#include "format.hpp"
#include "libslic3r/common_header/common_header.h"

namespace Slic3r { namespace GUI {

wxDEFINE_EVENT(EVT_LITE_MODE_CONFIRM, wxCommandEvent);
wxDEFINE_EVENT(EVT_LITE_MODE_CANCEL, wxCommandEvent);

static std::string url_encode(const std::string& value) {
	std::ostringstream escaped;
	escaped.fill('0');
	escaped << std::hex;
	for (std::string::const_iterator i = value.begin(), n = value.end(); i != n; ++i) {
		std::string::value_type c = (*i);

		// Keep alphanumeric and other accepted characters intact
		if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
			escaped << c;
			continue;
		}

		// Any other characters are percent-encoded
		escaped << std::uppercase;
		escaped << '%' << std::setw(2) << int((unsigned char)c);
		escaped << std::nouppercase;
	}
	return escaped.str();
}

void EnableLiteModeDialog::onLinkClicked(wxHtmlLinkEvent& event)
{
    wxGetApp().open_browser_with_warning_dialog(event.GetLinkInfo().GetHref());
    event.Skip(false);
}

EnableLiteModeDialog::EnableLiteModeDialog(wxWindow* parent, wxWindowID id, const wxString& title, enum ButtonStyle btn_style, const wxPoint& pos, const wxSize& size, long style)
    :DPIDialog(parent, id, title, pos, size, style)
{
    std::string icon_path = (boost::format("%1%/images/%2%.ico") % resources_dir() % Slic3r::CxBuildInfo::getIconName()).str();
    SetIcon(wxIcon(encode_path(icon_path.c_str()), wxBITMAP_TYPE_ICO));

    SetBackgroundColour(*wxWHITE);
    m_sizer_main = new wxBoxSizer(wxVERTICAL);
    auto        m_line_top = new wxPanel(this, wxID_ANY, wxDefaultPosition, wxSize(FromDIP(540), 1));
    m_line_top->SetBackgroundColour(wxColour(166, 169, 170));
    m_sizer_main->Add(m_line_top, 0, wxEXPAND, 0);
    m_sizer_main->Add(0, 0, 0, wxTOP, FromDIP(5));

    wxBoxSizer* m_sizer_right = new wxBoxSizer(wxVERTICAL);

    m_sizer_right->Add(0, 0, 1, wxTOP, FromDIP(15));

    wxHtmlWindow* m_website_html = new wxHtmlWindow(this, wxID_ANY, wxDefaultPosition, wxSize(FromDIP(480),FromDIP(100)), 0x0002 /*NEVER*/);
     
    {
        wxFont    font   = get_default_font(this);
        const int fs     = font.GetPointSize()+0.9;
        int       size[] = {fs, fs, fs, fs, fs, fs, fs};

        m_website_html->SetFonts(font.GetFaceName(), font.GetFaceName(), size);
        m_website_html->SetMinSize(wxSize(FromDIP(480), FromDIP(100)));
        m_website_html->SetMaxSize(wxSize(FromDIP(480), FromDIP(100)));
        m_website_html->SetBorders(2);
       
        wxString content1 = format_wxstr(
            _L("In the slicing preview, Lite Mode displays only the essential toolpath data and hides internal infill structures, effectively preventing unresponsiveness caused by excessive rendering. You can disable this mode at any time in the preview interface."));
        
        // "[Learn more]" hyperlink removed: it opened the Creality wiki.
        const wxString color = wxGetApp().app_config->get("dark_color_mode") == "1" ? wxString("#FFFFFF") : wxString("#000000");
        const auto  text =
            wxString::Format("<html> <body><font style=\"color: %s;font-size:20px;\">%s</font></body></html>", color.c_str(), content1.c_str());

        m_website_html->SetPage(text);
    }
    m_sizer_right->Add(m_website_html, 0, wxEXPAND | wxCENTER, FromDIP(15));

    auto sizer_button = new wxBoxSizer(wxHORIZONTAL);
    StateColor btn_bg_green(std::pair<wxColour, int>(wxColour(26, 111, 163), StateColor::Pressed), std::pair<wxColour, int>(wxColour(93, 173, 226), StateColor::Hovered),
        std::pair<wxColour, int>(wxColour(46, 134, 193), StateColor::Normal));

    StateColor btn_bg_white(std::pair<wxColour, int>(wxColour(206, 206, 206), StateColor::Pressed), std::pair<wxColour, int>(wxColour(220, 220, 220), StateColor::Hovered),
        std::pair<wxColour, int>(wxColour(238, 238, 238), StateColor::Normal));

    m_button_ok = new Button(this, _L("Enable"));
    m_button_ok->SetBackgroundColor(wxColour("#3498DB"));
    m_button_ok->SetBorderColor(*wxWHITE);
    m_button_ok->SetTextColor(wxColour("#FFFFFE"));
    m_button_ok->SetFont(Label::Body_12);
    m_button_ok->SetSize(wxSize(FromDIP(100), FromDIP(36)));
    m_button_ok->SetMinSize(wxSize(FromDIP(100), FromDIP(36)));
    m_button_ok->SetCornerRadius(FromDIP(18));

    m_button_ok->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent& e) {
        wxCommandEvent evt(EVT_LITE_MODE_CONFIRM, GetId());
        e.SetEventObject(this);
        GetEventHandler()->ProcessEvent(evt);
        EndModal(wxID_YES);
    });

    m_button_cancel = new Button(this, _L("Cancel"));
    m_button_cancel->SetBackgroundColor(btn_bg_white);
    m_button_cancel->SetBorderColor(*wxWHITE);
    m_button_cancel->SetFont(Label::Body_12);
    m_button_cancel->SetSize(wxSize(FromDIP(100), FromDIP(36)));
    m_button_cancel->SetMinSize(wxSize(FromDIP(100), FromDIP(36)));
    m_button_cancel->SetCornerRadius(FromDIP(18));

    m_button_cancel->Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent& e) {
        wxCommandEvent evt(EVT_LITE_MODE_CANCEL);
        e.SetEventObject(this);
        GetEventHandler()->ProcessEvent(evt);
        EndModal(wxID_NO);
        });

    Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent& e) {e.Veto(); });

    if (btn_style != CONFIRM_AND_CANCEL)
        m_button_cancel->Hide();
    else
        m_button_cancel->Show();

    wxBoxSizer* sizer_button_container = new wxBoxSizer(wxHORIZONTAL);
    sizer_button_container->AddStretchSpacer(1);
    sizer_button_container->Add(m_button_cancel, 0, wxRIGHT, FromDIP(10));
    sizer_button_container->Add(m_button_ok, 0, wxLEFT, FromDIP(10));

    m_sizer_right->Add(sizer_button_container, 1, wxLEFT | wxRIGHT | wxALIGN_CENTER, FromDIP(5));
    m_sizer_right->Add(0, 0, 0, wxTOP, FromDIP(10));

    m_sizer_main->Add(m_sizer_right, 0, wxBOTTOM | wxEXPAND, FromDIP(5));

    SetSizer(m_sizer_main);
    Layout();
    m_sizer_main->Fit(this);

    CenterOnParent();
    wxGetApp().UpdateDlgDarkUI(this);
}

wxWebView* EnableLiteModeDialog::CreateTipView(wxWindow* parent)
{
	wxWebView* tipView = WebView::CreateWebView(parent, "");
	return tipView;
}

void EnableLiteModeDialog::OnNavigating(wxWebViewEvent& event)
{
    wxString jump_url = event.GetURL();
    if (jump_url != m_host_url) {
        event.Veto();
        wxLaunchDefaultBrowser(jump_url);
    }
    else {
        event.Skip();
    }
}

bool EnableLiteModeDialog::ShowReleaseNote(std::string content)
{
	auto script = "window.showMarkdown('" + url_encode(content) + "', true);";
    RunScript(script);
    return true;
}

void EnableLiteModeDialog::RunScript(std::string script)
{
    WebView::RunScript(m_vebview_release_note, script);
    std::string switch_dark_mode_script = "SwitchDarkMode(";
    switch_dark_mode_script += wxGetApp().app_config->get("dark_color_mode") == "1" ? "true" : "false";
    switch_dark_mode_script += ");";
    WebView::RunScript(m_vebview_release_note, switch_dark_mode_script);
    script.clear();
}

void EnableLiteModeDialog::on_show()
{
    wxGetApp().UpdateDlgDarkUI(this);
    this->ShowModal();
}

void EnableLiteModeDialog::on_hide()
{
    EndModal(wxID_OK);
}

void EnableLiteModeDialog::update_btn_label(wxString ok_btn_text, wxString cancel_btn_text)
{
    m_button_ok->SetLabel(ok_btn_text);
    m_button_cancel->SetLabel(cancel_btn_text);
    rescale();
}

EnableLiteModeDialog::~EnableLiteModeDialog()
{

}

void EnableLiteModeDialog::on_dpi_changed(const wxRect& suggested_rect)
{
    rescale();
}

void EnableLiteModeDialog::rescale()
{
    m_button_ok->Rescale();
    m_button_cancel->Rescale();
}

}} // namespace Slic3r::GUI
