// Tests src/libslic3r/AppDirs.hpp. seed_folder_from_first starts a
// checkout's --datadir folder as a copy of existing user data (#25).
#include <catch_main.hpp>

#include <libslic3r/AppDirs.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

namespace sfs = std::filesystem;

namespace {

// A fresh, empty scratch folder per test case, removed at the end.
struct Scratch
{
    sfs::path root;
    explicit Scratch(const std::string &name)
    {
        root = sfs::temp_directory_path() /
               ("app_dirs_tests_" + name + "_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        sfs::remove_all(root);
        sfs::create_directories(root);
    }
    ~Scratch() { std::error_code ec; sfs::remove_all(root, ec); }
};

void write_file(const sfs::path &p, const std::string &text)
{
    sfs::create_directories(p.parent_path());
    std::ofstream(p) << text;
}

std::string read_file(const sfs::path &p)
{
    std::ifstream f(p);
    return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

} // namespace

TEST_CASE("missing dest is filled from the first non-empty source", "[seed_folder]")
{
    Scratch s("first_source");
    const sfs::path missing = s.root / "missing";
    const sfs::path empty   = s.root / "empty";
    const sfs::path user    = s.root / "user";
    const sfs::path other   = s.root / "other";
    sfs::create_directories(empty);
    write_file(user / "AppConfig.ini", "user settings");
    write_file(user / "user" / "default" / "filament" / "PLA.json", "{\"pla\":1}");
    write_file(other / "other.txt", "other");
    const sfs::path dest = s.root / "checkout" / ".appdata" / "Sanity Print" / "7.0";

    const sfs::path used = Slic3r::seed_folder_from_first(dest, {missing, empty, user, other});

    CHECK(used == user);
    CHECK(read_file(dest / "AppConfig.ini") == "user settings");
    CHECK(read_file(dest / "user" / "default" / "filament" / "PLA.json") == "{\"pla\":1}");
    CHECK_FALSE(sfs::exists(dest / "other.txt"));
    // The source is never changed.
    CHECK(read_file(user / "AppConfig.ini") == "user settings");
    CHECK(sfs::exists(user / "user" / "default" / "filament" / "PLA.json"));
}

TEST_CASE("empty dest folder is filled", "[seed_folder]")
{
    Scratch s("empty_dest");
    const sfs::path user = s.root / "user";
    write_file(user / "AppConfig.ini", "user settings");
    const sfs::path dest = s.root / "dest";
    sfs::create_directories(dest);

    CHECK(Slic3r::seed_folder_from_first(dest, {user}) == user);
    CHECK(read_file(dest / "AppConfig.ini") == "user settings");
}

TEST_CASE("dest that already has data is left alone", "[seed_folder]")
{
    Scratch s("dest_has_data");
    const sfs::path user = s.root / "user";
    write_file(user / "AppConfig.ini", "user settings");
    const sfs::path dest = s.root / "dest";
    write_file(dest / "AppConfig.ini", "checkout settings");

    CHECK(Slic3r::seed_folder_from_first(dest, {user}).empty());
    CHECK(read_file(dest / "AppConfig.ini") == "checkout settings");
}

TEST_CASE("no usable source copies nothing", "[seed_folder]")
{
    Scratch s("no_source");
    const sfs::path empty = s.root / "empty";
    sfs::create_directories(empty);
    const sfs::path dest = s.root / "dest";

    CHECK(Slic3r::seed_folder_from_first(dest, {s.root / "missing", empty}).empty());
    CHECK((!sfs::exists(dest) || sfs::is_empty(dest)));
}

// resources_dir_from: the app reads its resources/ from SLIC3R_RESOURCES_DIR when that is set,
// so each checkout's run config points its app at the checkout's own resources/ (#25).
TEST_CASE("resources folder: unset variable keeps the folder beside the exe", "[resources_dir]")
{
    const sfs::path beside_exe = sfs::path("D:/build/src/Release") / "resources";
    CHECK(Slic3r::resources_dir_from(nullptr, beside_exe) == beside_exe);
    CHECK(Slic3r::resources_dir_from("", beside_exe) == beside_exe);
}

TEST_CASE("resources folder: set variable wins", "[resources_dir]")
{
    const sfs::path beside_exe = sfs::path("D:/build/src/Release") / "resources";
    const char *checkout_resources = "G:/CLionProjects/SanityPrint/.claude/worktrees/feature/resources";
    CHECK(Slic3r::resources_dir_from(checkout_resources, beside_exe) == sfs::path(checkout_resources));
}
