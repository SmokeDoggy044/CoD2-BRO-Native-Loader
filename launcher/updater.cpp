#include "updater.h"

#include <fstream>
#include <sstream>
#include <thread>
#include <utility>
#include <vector>

#include "https.h"
#include "log.h"
#include "program.h"
#include "sha256.h"

namespace fs = std::filesystem;

namespace launcher {

std::optional<std::string> checksum_for(const std::string& sums, const std::string& file) {
    std::istringstream lines(sums);
    for (std::string line; std::getline(lines, line);) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        // "<64 hex digits>  <name>", or " *<name>" for binary mode
        if (line.size() < 66 || line[64] != ' ') continue;
        std::string name = line.substr(65);
        if (!name.empty() && (name[0] == ' ' || name[0] == '*')) name.erase(0, 1);
        if (name != file) continue;
        std::string hex = line.substr(0, 64);
        for (char& c : hex) {
            if (c >= 'A' && c <= 'F') c = static_cast<char>(c - 'A' + 'a');
            if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return {};
        }
        return hex;
    }
    return {};
}

bool is_release_install(const fs::path& dir) {
    std::error_code ec;
    return fs::is_regular_file(dir / "LICENSES" / "BUNDLED.txt", ec);
}

void remove_update_leftovers(const fs::path& dir) {
    std::error_code ec;
    if (!fs::exists(dir / kUpdateDir, ec)) return;
    fs::remove_all(dir / kUpdateDir, ec);
    if (ec) LOG_WARN("launcher", "cannot delete %s: %s", (dir / kUpdateDir).string().c_str(), ec.message().c_str());
}

bool install_files(const fs::path& from, const fs::path& to, const fs::path& backup, std::string& error) {
    std::vector<fs::path> files;  // relative to `from`
    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(from, ec); !ec && it != fs::recursive_directory_iterator();
         it.increment(ec)) {
        const fs::path relative = it->path().lexically_relative(from);
        if (*relative.begin() == "assets") {
            if (it->is_directory(ec)) it.disable_recursion_pending();
            continue;
        }
        if (!it->is_directory(ec) || it->is_symlink(ec)) files.push_back(relative);
    }
    if (ec) {
        error = "cannot read " + from.string() + ": " + ec.message();
        return false;
    }

    struct Moved {
        fs::path relative;
        bool replaced;  // an older file went to `backup`
    };
    std::vector<Moved> moved;
    for (const fs::path& relative : files) {
        const fs::path target = to / relative, old = backup / relative;
        const bool exists = fs::exists(fs::symlink_status(target, ec));
        bool step_ok = true;
        if (exists) {
            fs::create_directories(old.parent_path(), ec);
            if (!ec) fs::rename(target, old, ec);
            step_ok = !ec;
        }
        if (step_ok) {
            fs::create_directories(target.parent_path(), ec);
            if (!ec) fs::rename(from / relative, target, ec);
            if (ec && exists) {
                std::error_code back;
                fs::rename(old, target, back);
            }
            step_ok = !ec;
        }
        if (!step_ok) {
            error = "cannot replace " + target.string() + ": " + ec.message();
            for (auto it = moved.rbegin(); it != moved.rend(); ++it) {
                std::error_code back;
                fs::rename(to / it->relative, from / it->relative, back);
                if (it->replaced) fs::rename(backup / it->relative, to / it->relative, back);
            }
            return false;
        }
        moved.push_back({relative, exists});
    }
    return true;
}

Updater::Updater(std::string repository, std::string tag, std::string version, std::string package,
                 fs::path install_dir) {
    std::thread([shared = shared_, repository = std::move(repository), tag = std::move(tag),
                 version = std::move(version), package = std::move(package), install = std::move(install_dir)] {
        auto report = [&](Status status, std::string message) {
            if (status != Status::Working) LOG_INFO("launcher", "update to %s: %s", version.c_str(), message.c_str());
            std::lock_guard lock(shared->mutex);
            shared->result = {status, std::move(message)};
        };
        auto fail = [&](std::string message) { report(Status::Failed, std::move(message)); };

        const std::string archive = "cod_bro-" + version + "-" + package;
        const std::string base = "https://github.com/" + repository + "/releases/download/" + tag + "/";
        const std::string user_agent = "cod_bro_launcher/" COD_BRO_VERSION;
        report(Status::Working, "Downloading " + archive + "...");
        const std::optional<std::string> sums = platform::https_get(base + "SHA256SUMS.txt", user_agent);
        const std::optional<std::string> expected = sums ? checksum_for(*sums, archive) : std::nullopt;
        if (!expected) return fail("The release has no checksum for " + archive + ".");
        const std::optional<std::string> data = platform::https_get(base + archive, user_agent, 600);
        if (!data) return fail("The download failed.");
        report(Status::Working, "Checking the download...");
        if (sha256_hex(*data) != *expected) return fail("The download is damaged (wrong checksum).");

        report(Status::Working, "Unpacking...");
        const fs::path work = install / kUpdateDir;
        const fs::path unpacked = work / "new";
        std::error_code ec;
        fs::remove_all(work, ec);
        fs::create_directories(unpacked, ec);
        if (ec) return fail("Cannot write to " + install.string() + ": " + ec.message() + ".");
        const fs::path archive_path = work / archive;
        {
            std::ofstream out(archive_path, std::ios::binary);
            out.write(data->data(), static_cast<std::streamsize>(data->size()));
            if (!out) return fail("Cannot write " + archive_path.string() + ".");
        }
        const fs::path tar = platform::system_program("tar");
        if (const int code = platform::run_program(tar, {"-xf", archive_path, "-C", unpacked}); code != 0)
            return fail("Unpacking with " + tar.string() + " failed (exit code " + std::to_string(code) + ").");
        // One top folder, cod_bro-<version>-<platform>, with the launcher in it.
        const fs::path root = unpacked / ("cod_bro-" + version + "-" + package.substr(0, package.find('.')));
        const fs::path launcher_name = platform::sibling_executable("cod_bro_launcher").filename();
        if (!fs::is_regular_file(root / launcher_name, ec))
            return fail("The archive does not have the expected layout.");

        report(Status::Working, "Installing...");
        std::string error;
        if (!install_files(root, install, work / "old", error)) return fail(error + ". Nothing was changed.");
        fs::remove(archive_path, ec);
        fs::remove_all(unpacked, ec);
        report(Status::Installed, "Version " + version + " is installed.");
    }).detach();
}

Updater::Result Updater::result() const {
    std::lock_guard lock(shared_->mutex);
    return shared_->result;
}

}  // namespace launcher
