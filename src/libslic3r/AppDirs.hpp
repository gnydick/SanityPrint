#ifndef slic3r_AppDirs_hpp_
#define slic3r_AppDirs_hpp_

// Folder choices that let several app copies, one per checkout, run side by side (#25).
// Header-only so its tests need no other library.

#include <filesystem>
#include <system_error>
#include <vector>

namespace Slic3r {

// The resources folder the app reads: `env_value` (the SLIC3R_RESOURCES_DIR variable) when it is
// set and not empty, else `beside_exe`. A checkout's run config sets the variable to that
// checkout's resources/, so the build needs no link or copy.
inline std::filesystem::path resources_dir_from(const char *env_value, const std::filesystem::path &beside_exe)
{
    if (env_value != nullptr && *env_value != '\0')
        return std::filesystem::u8path(env_value);
    return beside_exe;
}

// Starts `dest` as a copy of existing data: when `dest` is missing or empty, copies the first
// source that exists and is not empty into it, recursively. Sources are never changed.
// Returns the source that was copied, or an empty path when nothing was copied (dest already
// had data, or no source had any).
inline std::filesystem::path seed_folder_from_first(const std::filesystem::path &dest,
                                                    const std::vector<std::filesystem::path> &sources)
{
    namespace sfs = std::filesystem;
    std::error_code ec;
    if (sfs::exists(dest, ec) && !sfs::is_empty(dest, ec))
        return {};
    for (const sfs::path &src : sources) {
        if (!sfs::is_directory(src, ec) || sfs::is_empty(src, ec))
            continue;
        sfs::create_directories(dest);
        sfs::copy(src, dest, sfs::copy_options::recursive | sfs::copy_options::copy_symlinks);
        return src;
    }
    return {};
}

} // namespace Slic3r

#endif // slic3r_AppDirs_hpp_
