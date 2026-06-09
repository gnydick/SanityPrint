#include "AppUpdateDialogs.hpp"
#include "GUI_App.hpp"
#include "AppUpdater.hpp"
#include "NotificationManager.hpp"
#include "Downloader.hpp"
#include "slic3r/GUI/I18N.hpp"
#include "Widgets/Label.hpp"
#include <algorithm>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/panel.h>
#include <wx/statbmp.h>
#include <wx/settings.h>
#include <wx/dcbuffer.h>

namespace Slic3r {
namespace GUI {

namespace {
wxString normalize_progress_msg(wxString msg)
{
    msg.Trim(true).Trim(false);

    if (msg.EndsWith("%")) {
        int i = static_cast<int>(msg.length()) - 2;
        while (i >= 0 && wxIsdigit(static_cast<unsigned int>(msg[i])))
            --i;
        if (i < static_cast<int>(msg.length()) - 2) {
            int j = i;
            while (j >= 0 && wxIsspace(static_cast<unsigned int>(msg[j])))
                --j;
            msg = msg.SubString(0, j);
        }
    }

    msg.Trim(true).Trim(false);

    if (msg.EndsWith("..."))
        msg.RemoveLast(3);
    while (msg.EndsWith("."))
        msg.RemoveLast();

    msg.Trim(true).Trim(false);
    return msg;
}

class TitleBarIconButton : public wxPanel
{
public:
    using ClickFn = std::function<void()>;

    TitleBarIconButton(wxWindow* parent, const wxBitmap& bmp, ClickFn on_click)
        : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL)
        , m_on_click(std::move(on_click))
    {
        SetBackgroundStyle(wxBG_STYLE_PAINT);
        m_bmp = bmp;
        SetMinSize(wxSize(FromDIP(28), FromDIP(24)));

        Bind(wxEVT_PAINT, &TitleBarIconButton::paintEvent, this);
        Bind(wxEVT_MOTION, &TitleBarIconButton::on_motion, this);
        Bind(wxEVT_ENTER_WINDOW, &TitleBarIconButton::on_enter, this);
        Bind(wxEVT_LEAVE_WINDOW, &TitleBarIconButton::on_leave, this);
        Bind(wxEVT_LEFT_DOWN, &TitleBarIconButton::on_left_down, this);
        Bind(wxEVT_LEFT_UP, &TitleBarIconButton::on_left_up, this);
        Bind(wxEVT_MOUSE_CAPTURE_LOST, &TitleBarIconButton::on_capture_lost, this);
    }

    void apply_theme(bool is_dark, const wxBitmap& bmp, const wxColour& normal_bg)
    {
        wxUnusedVar(is_dark);
        m_normal_bg = normal_bg;
        m_border_color = wxColour("#2E86C1");
        m_border_width = FromDIP(1);
        m_bmp = bmp;

        m_pressed = false;
        m_hover = false;
        SetBackgroundColour(m_normal_bg);
        Refresh();
    }

private:
    void on_enter(wxMouseEvent& e)
    {
        SetCursor(wxCursor(wxCURSOR_HAND));
        if (!m_hover) {
            m_hover = true;
            Refresh();
        }
        e.Skip(false);
    }

    void on_leave(wxMouseEvent& e)
    {
        SetCursor(wxCursor(wxCURSOR_ARROW));
        if (!m_pressed && m_hover) {
            m_hover = false;
            Refresh();
        }
        e.Skip(false);
    }

    void on_motion(wxMouseEvent& e)
    {
        const bool inside = wxRect(wxPoint(0, 0), GetClientSize()).Contains(e.GetPosition());
        if (inside && !m_hover) {
            SetCursor(wxCursor(wxCURSOR_HAND));
            m_hover = true;
            Refresh();
        } else if (!inside && m_hover && !m_pressed) {
            SetCursor(wxCursor(wxCURSOR_ARROW));
            m_hover = false;
            Refresh();
        }
        e.Skip(false);
    }

    void on_left_down(wxMouseEvent& e)
    {
        m_pressed = true;
        if (!HasCapture())
            CaptureMouse();
        Refresh();
        e.Skip(false);
    }

    void on_left_up(wxMouseEvent& e)
    {
        const bool inside = wxRect(wxPoint(0, 0), GetClientSize()).Contains(e.GetPosition());
        if (HasCapture())
            ReleaseMouse();
        m_pressed = false;
        m_hover = inside;

        if (inside && m_on_click)
            m_on_click();

        Refresh();
        e.Skip(false);
    }

    void on_capture_lost(wxMouseCaptureLostEvent&)
    {
        m_pressed = false;
        m_hover = false;
        Refresh();
    }

    void paintEvent(wxPaintEvent&)
    {
        wxAutoBufferedPaintDC dc(this);
        dc.SetBackground(wxBrush(m_normal_bg.IsOk() ? m_normal_bg : GetBackgroundColour()));
        dc.Clear();

        if (m_bmp.IsOk()) {
            const wxSize size = GetClientSize();
            const wxSize bmp_sz = m_bmp.GetSize();
            const int x = (size.x - bmp_sz.x) / 2;
            const int y = (size.y - bmp_sz.y) / 2;
            dc.DrawBitmap(m_bmp, x, y, true);
        }

        if (m_hover || m_pressed) {
            const wxSize size = GetClientSize();
            const int w = std::max(1, m_border_width);
            const int inset = w / 2;
            const int width = std::max(0, size.x - w);
            const int height = std::max(0, size.y - w);
            dc.SetBrush(*wxTRANSPARENT_BRUSH);
            dc.SetPen(wxPen(m_border_color.IsOk() ? m_border_color : wxColour("#2E86C1"), w));
            dc.DrawRectangle(inset + 1, inset, width, height);
        }
    }

private:
    ClickFn m_on_click;
    wxColour m_normal_bg;
    wxColour m_border_color;
    int m_border_width{ 1 };
    wxBitmap m_bmp;
    bool m_hover{ false };
    bool m_pressed{ false };
};

class UpdateProgressTitleBar : public wxPanel
{
public:
    UpdateProgressTitleBar(wxWindow* parent, const wxString& title, bool show_min_btn = true)
        : wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL)
        , m_title(title)
        , m_show_min_btn(show_min_btn)
    {
        apply_theme(wxGetApp().dark_mode());

        auto* sizer = new wxBoxSizer(wxHORIZONTAL);
        m_title_text = new wxStaticText(this, wxID_ANY, m_title, wxDefaultPosition, wxDefaultSize, wxST_ELLIPSIZE_END);
        m_title_text->SetFont(Label::Body_13);
        sizer->Add(m_title_text, 1, wxALIGN_CENTER_VERTICAL | wxLEFT, FromDIP(12));

        if (m_show_min_btn) {
            m_min_btn = new TitleBarIconButton(this, m_bmp_min, [this]() {
                if (auto* dlg = wxDynamicCast(wxGetTopLevelParent(this), wxDialog))
                    dlg->Iconize(true);
            });
            sizer->Add(m_min_btn, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(6));
        }
        m_close_btn = new TitleBarIconButton(this, m_bmp_close, [this]() {
            if (auto* dlg = wxDynamicCast(wxGetTopLevelParent(this), wxDialog))
                dlg->Close();
        });
        sizer->Add(m_close_btn, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(6));

        SetSizer(sizer);
        Layout();

        Bind(wxEVT_LEFT_DOWN, &UpdateProgressTitleBar::on_left_down, this);
        Bind(wxEVT_LEFT_UP, &UpdateProgressTitleBar::on_left_up, this);
        Bind(wxEVT_MOTION, &UpdateProgressTitleBar::on_mouse_move, this);

        m_title_text->Bind(wxEVT_LEFT_DOWN, &UpdateProgressTitleBar::on_left_down, this);
        m_title_text->Bind(wxEVT_LEFT_UP, &UpdateProgressTitleBar::on_left_up, this);
        m_title_text->Bind(wxEVT_MOTION, &UpdateProgressTitleBar::on_mouse_move, this);
    }

    void apply_theme(bool is_dark)
    {
        const wxColour bg = is_dark ? wxColour("#4B4B4D") : wxColour("#FFFFFF");
        const wxColour fg = is_dark ? *wxWHITE : *wxBLACK;
        SetBackgroundColour(bg);
        if (m_title_text) {
            m_title_text->SetForegroundColour(fg);
            m_title_text->SetBackgroundColour(bg);
        }

        m_bmp_min = create_scaled_bitmap(is_dark ? "topbar_min" : "topbar_min_light", this, FromDIP(18));
        m_bmp_close = create_scaled_bitmap(is_dark ? "topbar_close" : "topbar_close_light", this, FromDIP(18));
        if (m_min_btn)
            m_min_btn->apply_theme(is_dark, m_bmp_min, bg);
        if (m_close_btn)
            m_close_btn->apply_theme(is_dark, m_bmp_close, bg);
        Refresh();
    }

    void set_title(const wxString& title)
    {
        if (m_title == title)
            return;
        m_title = title;
        if (m_title_text)
            m_title_text->SetLabel(m_title);
        Layout();
    }

private:
    void on_left_down(wxMouseEvent& event)
    {
        m_dragging = true;
        m_drag_start_pos = event.GetPosition();
        CaptureMouse();
        event.Skip();
    }

    void on_left_up(wxMouseEvent& event)
    {
        if (m_dragging) {
            ReleaseMouse();
            m_dragging = false;
        }
        event.Skip();
    }

    void on_mouse_move(wxMouseEvent& event)
    {
        if (!m_dragging) {
            event.Skip();
            return;
        }
        if (auto* dlg = wxDynamicCast(wxGetTopLevelParent(this), wxTopLevelWindow)) {
            wxPoint delta = event.GetPosition() - m_drag_start_pos;
            dlg->Move(dlg->GetPosition() + delta);
        }
        event.Skip();
    }

private:
    wxString m_title;
    wxStaticText* m_title_text{ nullptr };
    TitleBarIconButton* m_min_btn{ nullptr };
    TitleBarIconButton* m_close_btn{ nullptr };
    wxBitmap m_bmp_min;
    wxBitmap m_bmp_close;
    bool m_show_min_btn{ true };
    bool m_dragging{ false };
    wxPoint m_drag_start_pos;
};
}

AppUpdateProgressDialog::AppUpdateProgressDialog(wxWindow* parent)
    : DPIDialog(parent, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(400, 150), wxBORDER_NONE)
    , m_last_percent(0)
{
    auto* main_sizer = new wxBoxSizer(wxVERTICAL);

    m_title_bar = new UpdateProgressTitleBar(this, _L("Downloading Update"), true);
    m_title_bar->SetMinSize(wxSize(-1, FromDIP(40)));
    main_sizer->Add(m_title_bar, 0, wxEXPAND, 0);

    main_sizer->AddSpacer(FromDIP(18));

    auto* progress_panel = new RoundedPanel(this, wxID_ANY);
    m_progress_panel = progress_panel;
    progress_panel->SetMinSize(wxSize(FromDIP(360), FromDIP(90)));
    progress_panel->SetBorderWidth(0);
    progress_panel->SetTopLeftRadius(FromDIP(10));
    progress_panel->SetTopRightRadius(FromDIP(10));
    progress_panel->SetBottomLeftRadius(FromDIP(10));
    progress_panel->SetBottomRightRadius(FromDIP(10));

    auto* progress_sizer = new wxBoxSizer(wxVERTICAL);
    progress_sizer->AddSpacer(FromDIP(14));

    m_status_text = new wxStaticText(progress_panel, wxID_ANY, _L("Preparing to download..."), wxDefaultPosition, wxDefaultSize, wxST_ELLIPSIZE_END);
    m_status_text->SetFont(Label::Body_13);
    m_status_text->SetForegroundColour(*wxWHITE);
    progress_sizer->Add(m_status_text, 0, wxLEFT | wxRIGHT | wxEXPAND, FromDIP(18));
    progress_sizer->AddSpacer(FromDIP(12));

    m_progress = new ProgressBar(progress_panel, wxID_ANY, 100, wxDefaultPosition, wxSize(FromDIP(360), FromDIP(14)), false);
    m_progress->SetMinSize(wxSize(FromDIP(360), FromDIP(14)));
    progress_sizer->Add(m_progress, 0, wxLEFT | wxRIGHT | wxEXPAND, FromDIP(18));
    progress_sizer->AddSpacer(FromDIP(14));

    progress_panel->SetSizer(progress_sizer);
    progress_panel->Layout();

    main_sizer->Add(progress_panel, 0, wxLEFT | wxRIGHT | wxEXPAND, FromDIP(20));
    main_sizer->AddSpacer(FromDIP(18));

    auto* btn_sizer = new wxBoxSizer(wxHORIZONTAL);
    btn_sizer->AddStretchSpacer();
    m_cancel_btn = new Button(this, _L("Cancel"), "", 0, 0, wxID_CANCEL);
    m_cancel_btn->SetMinSize(wxSize(FromDIP(100), FromDIP(32)));
    m_cancel_btn->SetCornerRadius(FromDIP(16));
    btn_sizer->Add(m_cancel_btn, 0, wxBOTTOM, FromDIP(6));
    btn_sizer->AddStretchSpacer();
    main_sizer->Add(btn_sizer, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(20));
    main_sizer->AddSpacer(FromDIP(14));

    SetSizerAndFit(main_sizer);
    Layout();
    CenterOnScreen();
    apply_theme();

    Bind(wxEVT_ICONIZE, [this](wxIconizeEvent& e) {
        if (e.IsIconized()) {
            show_imgui_notification();
            Hide();
            e.Skip(false);
            return;
        }
        e.Skip();
    });

    Bind(wxEVT_BUTTON, [this](wxCommandEvent& e) {
        // Cancel download
        AppUpdater::getInstance().cancel_download();
        EndModal(wxID_CANCEL);
        e.Skip(false);
    }, wxID_CANCEL);

    // Handle close button
    Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent& e) {
        // Cancel download when close button is clicked
        AppUpdater::getInstance().cancel_download();
        EndModal(wxID_CANCEL);
        e.Skip(false);
    });
}

AppUpdateProgressDialog::~AppUpdateProgressDialog()
{
    close_imgui_notification();
}

void AppUpdateProgressDialog::apply_theme()
{
    const bool is_dark = wxGetApp().dark_mode();
    wxGetApp().UpdateDlgDarkUI(this);

    const wxColour bg = is_dark ? wxColour("#4B4B4D") : wxColour("#FFFFFF");
    const wxColour fg = is_dark ? *wxWHITE : *wxBLACK;
    const wxColour line = is_dark ? wxColour("#616165") : wxColour("#E8EAEE");
    const wxColour border = is_dark ? wxColour("#6E6E72") : wxColour("#DBDBDB");
    const wxColour progress_track = is_dark ? wxColour("#616165") : wxColour("#E8EAEE");
    const wxColour progress_fill = wxColour("#2E86C1");

    SetBackgroundColour(bg);

    if (auto* title_bar = dynamic_cast<UpdateProgressTitleBar*>(m_title_bar))
        title_bar->apply_theme(is_dark);

    if (auto* panel = dynamic_cast<RoundedPanel*>(m_progress_panel)) {
        panel->SetBackgroundColour(bg);
        panel->SetBorderColor(bg);
    } else if (m_progress_panel) {
        m_progress_panel->SetBackgroundColour(bg);
    }

    if (m_status_text) {
        m_status_text->SetFont(Label::Body_13);
        m_status_text->SetForegroundColour(fg);
        m_status_text->SetBackgroundColour(bg);
    }

    if (m_progress) {
        m_progress->SetBackgroundColour(bg);
        m_progress->SetProgressForedColour(progress_track);
        m_progress->SetProgressBackgroundColour(progress_fill);
    }

    if (m_cancel_btn) {
        StateColor btn_border(
            std::pair<wxColour, int>(border, StateColor::Disabled),
            std::pair<wxColour, int>(progress_fill, StateColor::Pressed),
            std::pair<wxColour, int>(progress_fill, StateColor::Hovered),
            std::pair<wxColour, int>(border, StateColor::Normal)
        );
        StateColor btn_bg(
            std::pair<wxColour, int>(bg, StateColor::Normal),
            std::pair<wxColour, int>(bg, StateColor::Hovered),
            std::pair<wxColour, int>(bg, StateColor::Pressed),
            std::pair<wxColour, int>(bg, StateColor::Disabled)
        );
        const wxColour btn_text_base = wxColour("#000000");
        StateColor btn_text(
            std::pair<wxColour, int>(btn_text_base, StateColor::Disabled),
            std::pair<wxColour, int>(btn_text_base, StateColor::Pressed),
            std::pair<wxColour, int>(btn_text_base, StateColor::Hovered),
            std::pair<wxColour, int>(btn_text_base, StateColor::Normal)
        );

        m_cancel_btn->SetBackgroundColour(bg);
        m_cancel_btn->SetBackgroundColor(btn_bg);
        m_cancel_btn->SetBorderColor(btn_border);
        m_cancel_btn->SetTextColor(btn_text);
    }

    Layout();
    Refresh();
}

void AppUpdateProgressDialog::update_progress(int percent, const wxString& msg)
{
    m_last_percent = percent;
    m_last_msg = msg;
    if (m_progress) m_progress->SetValue(percent);
    if (m_status_text) {
        const wxString preparing = normalize_progress_msg(_L("Preparing to download..."));
        const wxString normalized = msg.IsEmpty() ? wxString() : normalize_progress_msg(msg);
        const bool is_preparing = !msg.IsEmpty() && normalized == preparing;

        if (is_preparing && percent <= 0) {
            m_status_text->SetLabel(_L("Preparing to download..."));
        } else {
            wxString base = msg.IsEmpty() ? wxString(_L("Downloading")) : normalized;
            if (percent >= 0)
                m_status_text->SetLabel(wxString::Format(_L("%s (%d%% Completed)"), base, percent));
            else
                m_status_text->SetLabel(base);
        }
    }

    if (IsShown() && !IsIconized()) {
        close_imgui_notification();
        return;
    }

    NotificationManager* nm = wxGetApp().notification_manager();
    if (nm) {
        float p = 0.f;
        if (percent <= 0) p = 0.f;
        else if (percent >= 100) p = 1.f;
        else p = static_cast<float>(percent) / 100.f;
        nm->set_download_URL_progress(imgui_notification_id(), p);
    }
}

bool AppUpdateProgressDialog::Show(bool show)
{
    if (show) {
        close_imgui_notification();
        apply_theme();
        // Ensure dialog is centered on screen when showing
        CentreOnParent();
        if (wxGetApp().plater() && wxGetApp().plater()->get_current_canvas3D())
            wxGetApp().plater()->get_current_canvas3D()->schedule_extra_frame(0);
    }
    return DPIDialog::Show(show);
}

void AppUpdateProgressDialog::close_imgui_notification()
{
    NotificationManager* nm = wxGetApp().notification_manager();
    if (nm)
        nm->close_download_URL_notification(imgui_notification_id());
}

void AppUpdateProgressDialog::show_imgui_notification()
{
    NotificationManager* nm = wxGetApp().notification_manager();
    if (!nm)
        return;

    nm->push_download_URL_progress_notification(imgui_notification_id(), _u8L("Downloading Update"),
        [this](DownloaderUserAction action, int) {
            if (action == DownloaderUserAction::DownloadUserCanceled) {
                wxCommandEvent evt(wxEVT_BUTTON, wxID_CANCEL);
                wxPostEvent(this, evt);
                return true;
            }
            if (action == DownloaderUserAction::DownloadUserOpenedFolder) {
                this->restore_from_imgui_notification();
                return true;
            }
            return true;
        });

    update_progress(m_last_percent, m_last_msg);

    if (wxGetApp().plater() && wxGetApp().plater()->get_current_canvas3D())
        wxGetApp().plater()->get_current_canvas3D()->schedule_extra_frame(0);
}

void AppUpdateProgressDialog::restore_from_imgui_notification()
{
    close_imgui_notification();
    Iconize(false);
    DPIDialog::Show(true);
    Raise();
}

void AppUpdateProgressDialog::on_dpi_changed(const wxRect& suggested_rect)
{
    Layout();
    SetSize(suggested_rect.GetSize());
    Refresh();
}

size_t AppUpdateProgressDialog::imgui_notification_id()
{
    return 1000000000u;
}

AppUpdateFinishDialog::AppUpdateFinishDialog(wxWindow* parent)
    : DPIDialog(parent, wxID_ANY, wxEmptyString, wxDefaultPosition, wxSize(520, 220), wxBORDER_NONE)
{
    auto* main_sizer = new wxBoxSizer(wxVERTICAL);

    m_title_bar = new UpdateProgressTitleBar(this, _L("Update Ready"), false);
    m_title_bar->SetMinSize(wxSize(-1, FromDIP(40)));
    main_sizer->Add(m_title_bar, 0, wxEXPAND, 0);

    main_sizer->AddSpacer(FromDIP(42));

    m_text = new wxStaticText(this, wxID_ANY, _L("Update downloaded successfully. Install now?"), wxDefaultPosition, wxDefaultSize, wxALIGN_CENTER_HORIZONTAL);
    m_text->SetFont(Label::Body_13);
    main_sizer->Add(m_text, 0, wxLEFT | wxRIGHT | wxEXPAND, FromDIP(30));

    main_sizer->AddSpacer(FromDIP(42));

    auto* btn_sizer = new wxBoxSizer(wxHORIZONTAL);
    btn_sizer->AddStretchSpacer();
    m_btn_later = new Button(this, _L("Install Later"), "", 0, 0, wxID_CANCEL);
    m_btn_later->SetMinSize(wxSize(FromDIP(104), FromDIP(32)));
    m_btn_later->SetCornerRadius(FromDIP(4));
    btn_sizer->Add(m_btn_later, 0, wxRIGHT, FromDIP(20));

    m_btn_now = new Button(this, _L("Install Now"), "", 0, 0, wxID_YES);
    m_btn_now->SetMinSize(wxSize(FromDIP(104), FromDIP(32)));
    m_btn_now->SetCornerRadius(FromDIP(4));
    btn_sizer->Add(m_btn_now, 0);
    btn_sizer->AddStretchSpacer();

    main_sizer->Add(btn_sizer, 0, wxEXPAND | wxLEFT | wxRIGHT, FromDIP(30));
    main_sizer->AddSpacer(FromDIP(18));

    SetSizerAndFit(main_sizer);
    Layout();
    CenterOnScreen();

    Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent& e) {
        EndModal(wxID_CANCEL);
        e.Skip(false);
    });

    Bind(wxEVT_BUTTON, [this](wxCommandEvent& e) {
        EndModal(wxID_YES);
        e.Skip(false);
    }, wxID_YES);

    Bind(wxEVT_BUTTON, [this](wxCommandEvent& e) {
        EndModal(wxID_CANCEL);
        e.Skip(false);
    }, wxID_CANCEL);

    apply_theme();
}

void AppUpdateFinishDialog::on_dpi_changed(const wxRect& suggested_rect)
{
    Layout();
    SetSize(suggested_rect.GetSize());
    apply_theme();
    Refresh();
}

bool AppUpdateFinishDialog::Show(bool show)
{
    if (show) {
        // Ensure dialog is centered on parent when showing
        CentreOnParent();
    }
    return DPIDialog::Show(show);
}

void AppUpdateFinishDialog::apply_theme()
{
    const bool is_dark = wxGetApp().dark_mode();
    wxGetApp().UpdateDlgDarkUI(this);

    const wxColour content_bg = is_dark ? wxColour("#4B4B4D") : wxColour("#FFFFFF");
    const wxColour fg = is_dark ? *wxWHITE : *wxBLACK;
    const wxColour line = is_dark ? wxColour("#616165") : wxColour("#E8EAEE");
    const wxColour accent = is_dark ? wxColour("#3498DB") : wxColour("#2E86C1");
    const wxColour now_hover = is_dark ? wxColour("#19D567") : wxColour("#19D567");
    const wxColour now_pressed = is_dark ? wxColour("#12AA50") : wxColour("#12AA50");
    const wxColour cancel_border = is_dark ? wxColour("#6E6E72") : wxColour("#DBDBDB");
    const wxColour cancel_hover_bg = is_dark ? wxColour("#535355") : wxColour("#F3F4F6");
    const wxColour cancel_pressed_bg = is_dark ? wxColour("#434345") : wxColour("#E8EAEE");

    SetBackgroundColour(content_bg);

    if (auto* title_bar = dynamic_cast<UpdateProgressTitleBar*>(m_title_bar))
        title_bar->apply_theme(is_dark);

    if (m_text) {
        m_text->SetFont(Label::Body_13);
        m_text->SetForegroundColour(fg);
        m_text->SetBackgroundColour(content_bg);
    }

    if (m_btn_later) {
        m_btn_later->SetBackgroundColour(content_bg);
        m_btn_later->SetBorderWidth(FromDIP(1));
        m_btn_later->SetBackgroundColor(StateColor(
            std::pair<wxColour, int>(content_bg, StateColor::Disabled),
            std::pair<wxColour, int>(cancel_pressed_bg, StateColor::Pressed),
            std::pair<wxColour, int>(cancel_hover_bg, StateColor::Hovered),
            std::pair<wxColour, int>(content_bg, StateColor::Normal)));
        m_btn_later->SetBorderColor(StateColor(
            std::pair<wxColour, int>(cancel_border, StateColor::Disabled),
            std::pair<wxColour, int>(accent, StateColor::Hovered),
            std::pair<wxColour, int>(accent, StateColor::Pressed),
            std::pair<wxColour, int>(cancel_border, StateColor::Normal)));
        m_btn_later->SetTextColor(StateColor(
            std::pair<wxColour, int>(wxColour("#000000"), StateColor::Disabled),
            std::pair<wxColour, int>(wxColour("#000000"), StateColor::Pressed),
            std::pair<wxColour, int>(wxColour("#000000"), StateColor::Hovered),
            std::pair<wxColour, int>(wxColour("#000000"), StateColor::Normal)));
    }

    if (m_btn_now) {
        m_btn_now->SetBackgroundColour(content_bg);
        m_btn_now->SetBorderWidth(0);
        m_btn_now->SetBackgroundColor(StateColor(
            std::pair<wxColour, int>(accent, StateColor::Disabled),
            std::pair<wxColour, int>(now_pressed, StateColor::Pressed),
            std::pair<wxColour, int>(now_hover, StateColor::Hovered),
            std::pair<wxColour, int>(accent, StateColor::Normal)));
        m_btn_now->SetBorderColor(StateColor(
            std::pair<wxColour, int>(accent, StateColor::Disabled),
            std::pair<wxColour, int>(now_hover, StateColor::Hovered),
            std::pair<wxColour, int>(now_pressed, StateColor::Pressed),
            std::pair<wxColour, int>(accent, StateColor::Normal)));
        m_btn_now->SetTextColor(StateColor(
            std::pair<wxColour, int>(wxColour("#000000"), StateColor::Disabled),
            std::pair<wxColour, int>(wxColour("#000000"), StateColor::Pressed),
            std::pair<wxColour, int>(wxColour("#000000"), StateColor::Hovered),
            std::pair<wxColour, int>(wxColour("#000000"), StateColor::Normal)));
    }

    Layout();
    Refresh();
}

AppUpdateErrorDialog::AppUpdateErrorDialog(wxWindow* parent)
    : DPIDialog(parent, wxID_ANY, _L("Update Failed"), wxDefaultPosition, wxSize(400, 150), wxCAPTION | wxCLOSE_BOX)
{
    SetBackgroundColour(*wxWHITE);
    wxBoxSizer* main_sizer = new wxBoxSizer(wxVERTICAL);
    
    m_text = new wxStaticText(this, wxID_ANY, _L("An error occurred during download."));
    main_sizer->Add(m_text, 1, wxALL | wxALIGN_CENTER | wxEXPAND, 20);
    
    wxBoxSizer* btn_sizer = new wxBoxSizer(wxHORIZONTAL);
    
    wxButton* btn_retry = new wxButton(this, wxID_RETRY, _L("Retry"));
    wxButton* btn_close = new wxButton(this, wxID_CANCEL, _L("Close"));
    
    btn_sizer->Add(btn_retry, 0, wxALL, 10);
    btn_sizer->Add(btn_close, 0, wxALL, 10);
    
    main_sizer->Add(btn_sizer, 0, wxALIGN_CENTER);
    
    SetSizer(main_sizer);
    Layout();
    CenterOnScreen();

    wxGetApp().UpdateDlgDarkUI(this);
}

void AppUpdateErrorDialog::SetErrorMsg(const wxString& msg)
{
    if (m_text) m_text->SetLabel(msg);
    Layout();
}

void AppUpdateErrorDialog::on_dpi_changed(const wxRect& suggested_rect)
{
    Layout();
    SetSize(suggested_rect.GetSize());
    Refresh();
}

}
}
