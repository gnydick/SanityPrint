#include "common_header.h"

#include "../libslic3r/libslic3r_version.h"
namespace Slic3r { 
namespace CxBuildInfo {
const std::string getVersion() { return std::string(SANITYPRINT_VERSION); }
// Use __DATE__ and __TIME__ instead of cmake-generated timestamp to enable incremental builds
const std::string  getBuildTime() { return std::string(__DATE__ " " __TIME__); }
const std::string  getBuildId() { return std::string(SLIC3R_BUILD_ID); }
const std::string getBuildOs() { return std::string(BUILD_OS); }
const std::string getBuildType() { return std::string(PROJECT_VERSION_EXTRA); }

const std::string getProjectName() { return std::string(SLIC3R_PROCESS_NAME); }

 void setDarkMode(bool isDark) {
     if (g_is_dark != isDark) {

     }
     g_is_dark = isDark;
 }

const bool isCusotmized()
    {
#if defined(CUSTOMIZED)
    return true;
#else
    return false;
#endif
}

const bool isEnabledCxCloud()
{
#if defined(CUSTOMIZED) && !defined(CUSTOM_CXCLOUD_ENABLED)
    return false;
#else
    return true;
#endif
}
const std::string getIconName_noTheme() {
#if defined(CUSTOMIZED) && defined(CUSTOM_ICON_NAME)
    return std::string(CUSTOM_ICON_NAME);
#endif //
    return std::string(SLIC3R_APP_NAME);
}
const std::string getIconName()
{
#if defined(CUSTOMIZED) && defined(CUSTOM_ICON_NAME)
    if ( g_is_dark && std::string(CUSTOM_ICON_NAME) == "MorandiPrint")
    {
        return std::string(CUSTOM_ICON_NAME) + "_light";
    }
    return std::string(CUSTOM_ICON_NAME);
#endif //
    return std::string(SLIC3R_APP_NAME);
}

}

} // namespace Slic3r