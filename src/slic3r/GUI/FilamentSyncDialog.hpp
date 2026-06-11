#ifndef slic3r_FilamentSyncDialog_hpp_
#define slic3r_FilamentSyncDialog_hpp_

#include <string>
#include <vector>

#include <wx/dialog.h>

class CheckBox;
class Button;
class wxBoxSizer;

namespace Slic3r { namespace GUI {

// One printer row in the sync dialog, sourced from the Device screen's data.
struct SyncDevice
{
    std::string name;
    std::string address;
    bool        online = false;
};

// Modal picker: choose which printers receive the user's custom filaments.
// On Sync, every user-created filament preset is pushed to each selected
// printer through its local material catalog REST API
// (POST http://<ip>:7125/server/material — non-destructive upsert by name).
class FilamentSyncDialog : public wxDialog
{
public:
    explicit FilamentSyncDialog(wxWindow *parent);

    // Devices currently known to the Device screen.
    static std::vector<SyncDevice> collect_devices();

private:
    void start_sync(const std::vector<SyncDevice> &targets);
    std::vector<SyncDevice> selected_targets(bool require_any);

    std::vector<std::pair<CheckBox *, SyncDevice>> m_device_rows;
    CheckBox *m_select_all  = nullptr;
    Button   *m_sync_button = nullptr;
    size_t    m_filament_count = 0;
};

}} // namespace Slic3r::GUI

#endif // slic3r_FilamentSyncDialog_hpp_
