#include "ColorSlider.hpp"

wxBEGIN_EVENT_TABLE(ColorSlider, wxControl)
    EVT_PAINT(ColorSlider::OnPaint)
    EVT_ERASE_BACKGROUND(ColorSlider::OnEraseBackground)
    EVT_SET_FOCUS(ColorSlider::OnFocusChange) 
    EVT_KILL_FOCUS(ColorSlider::OnFocusChange) 
    EVT_LEFT_DOWN(ColorSlider::OnLeftDown) 
    EVT_LEFT_UP(ColorSlider::OnLeftUp)
    EVT_MOTION(ColorSlider::OnMotion) 
    EVT_MOUSE_CAPTURE_LOST(ColorSlider::OnCaptureLost)
    EVT_SYS_COLOUR_CHANGED(ColorSlider::OnSysColourChanged)
                    wxEND_EVENT_TABLE()


    ColorSlider::ColorSlider(wxWindow* parent, wxWindowID id, int value, int minValue, int maxValue, const wxPoint& pos, const wxSize& size, long style)
    : wxControl(parent, id, pos, size, style | wxBORDER_NONE)
{
    SetBackgroundStyle(wxBG_STYLE_PAINT); 
    SetRange(minValue, maxValue);
    SetValue(value);
    CallAfter([this] {
        UpdatePaletteFromEnv();
        Refresh();
    });
}

void ColorSlider::OnEraseBackground(wxEraseEvent& event)
{
}

void ColorSlider::OnFocusChange(wxFocusEvent& event)
{
    Refresh(false);   
    //Update();
    event.Skip();
}

void ColorSlider::SetRange(int minVal, int maxVal)
{
    if (maxVal < minVal)
        std::swap(minVal, maxVal);
    m_min   = minVal;
    m_max   = maxVal;
    m_value = std::clamp(m_value, m_min, m_max);
    Refresh(false);
}

void ColorSlider::SetValue(int value)
{
    value = std::clamp(value, m_min, m_max);
    if (value == m_value)
        return;
    m_value = value;
    Refresh(false);

    wxCommandEvent ev(wxEVT_SLIDER, GetId());
    ev.SetEventObject(this);
    ev.SetInt(m_value);
    GetEventHandler()->ProcessEvent(ev);
}


void ColorSlider::OnPaint(wxPaintEvent&)
{
    wxAutoBufferedPaintDC dc(this);
    dc.SetBackground(wxBrush(GetParent() ? GetParent()->GetBackgroundColour() : GetBackgroundColour()));
    dc.Clear();

#if wxUSE_GRAPHICS_CONTEXT
    UpdatePaletteFromEnv();

    wxGCDC gdc(dc);
    Draw(gdc);
#endif
}

void ColorSlider::Draw(wxGCDC& gdc)
{
    auto* gc = gdc.GetGraphicsContext();
    if (!gc)
        return;

    const wxSize cs     = GetClientSize();
    const int    width  = cs.GetWidth();
    const int    height = cs.GetHeight();
    if (width <= 0 || height <= 0)
        return;

    const int trackH = TrackHeightDIP();
    const int trackR = trackH / 2;
    const int pad    = PaddingDIP();
    const int trackY = height / 2 - trackH / 2;

    const int minX = THUMB_RADIUS + pad;
    const int maxX = width - THUMB_RADIUS - pad;
    if (maxX <= minX)
        return;

    const int range = std::max(1, m_max - m_min);
    double    ratio = double(m_value - m_min) / double(range);
    ratio           = std::clamp(ratio, 0.0, 1.0);

    const double thumbX = minX + ratio * (maxX - minX);
    const double thumbY = height / 2.0;

    gc->SetPen(*wxTRANSPARENT_PEN);

    gc->SetBrush(wxBrush(mTrackBg));
    gc->DrawRoundedRectangle(THUMB_RADIUS, trackY, width - 2 * THUMB_RADIUS, trackH, trackR);

    gc->SetBrush(wxBrush(mThumbFill));
    gc->DrawRoundedRectangle(THUMB_RADIUS, trackY, thumbX - THUMB_RADIUS, trackH, trackR);

    gc->SetBrush(wxBrush(mThumbFill));
    gc->SetPen(wxPen(mThumbRing, FromDIP(2, this)));
    gc->DrawEllipse(thumbX - THUMB_RADIUS, thumbY - THUMB_RADIUS, THUMB_RADIUS * 2, THUMB_RADIUS * 2);
}

bool ColorSlider::IsDarkAppearance() const
{
    const wxWindow* ref = GetParent();
    const wxColour  bg  = ref ? ref->GetBackgroundColour() : wxSystemSettings::GetColour(wxSYS_COLOUR_WINDOW);
    const double    r = bg.Red() / 255.0, g = bg.Green() / 255.0, b = bg.Blue() / 255.0;
    const double    L = 0.2126 * r + 0.7152 * g + 0.0722 * b;
    return L < 0.5;
}

void ColorSlider::SetValueFromXCoord(int x, bool fireEvent)
{
    const int width = GetClientSize().GetWidth();
    const int minX  = THUMB_RADIUS + PaddingDIP();
    const int maxX  = width - THUMB_RADIUS - PaddingDIP();
    if (maxX <= minX)
        return;

    double ratio  = double(std::clamp(x, minX, maxX) - minX) / double(maxX - minX);
    int    newVal = m_min + int(std::lround(ratio * (m_max - m_min)));
    newVal        = std::clamp(newVal, m_min, m_max);

    if (fireEvent)
        SetValue(newVal);
    else {
        m_value = newVal;
        Refresh(false);
    }
}

void ColorSlider::OnLeftDown(wxMouseEvent& e)
{
    CaptureMouse();
    m_dragging = true;
    SetValueFromXCoord(e.GetX(), /*fireEvent=*/true);
}

void ColorSlider::OnLeftUp(wxMouseEvent&)
{
    if (HasCapture())
        ReleaseMouse();
    m_dragging = false;
}

void ColorSlider::OnMotion(wxMouseEvent& e)
{
    if (!m_dragging || !e.Dragging() || !e.LeftIsDown())
        return;
    SetValueFromXCoord(e.GetX(), /*fireEvent=*/true);
}

void ColorSlider::OnCaptureLost(wxMouseCaptureLostEvent&) { m_dragging = false; }

void ColorSlider::UpdatePaletteFromEnv()
{
    const bool dark = IsDarkAppearance();
    if (dark) {
        mTrackBg   = wxColour(43, 43, 43);
        mThumbFill = wxColour(65, 179, 224);
        mThumbRing = wxColour(219, 219, 219);
    } else {
        mTrackBg   = wxColour(225, 228, 233);
        mThumbFill = wxColour(52, 152, 219);
        mThumbRing = wxColour(255, 255, 255);
    }
}

void ColorSlider::OnSysColourChanged(wxSysColourChangedEvent& e)
{
    UpdatePaletteFromEnv();
    Refresh(false);
    e.Skip();
}