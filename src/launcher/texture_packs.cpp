#include "texture_packs.h"

#include <chrono>
#include <cstdlib>
#include <fstream>
#include <map>
#include <thread>
#include <tuple>

#include "https.h"
#include "log.h"
#include "sha256.h"
#include "update_check.h"
#include "updater.h"

namespace launcher {

namespace fs = std::filesystem;

namespace {

constexpr size_t kChunk = 1 << 20;
constexpr int kAttempts = 4;  // per run of files read from the web

std::optional<std::string> read_file(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return std::nullopt;
    return std::string(std::istreambuf_iterator<char>(in), {});
}

std::string megabytes(uint64_t bytes) { return std::to_string((bytes + (1 << 19)) >> 20) + " MB"; }

// `length` bytes of release file `name` from `offset` into `sink`.
bool fetch(const PackSource& source, const std::string& name, uint64_t offset, uint64_t length,
           const std::function<bool(const char*, size_t)>& sink, std::string& error) {
    if (source.web()) return platform::https_download(source.base_url + name, source.user_agent, offset, length, sink, error);
    std::ifstream in(source.folder / name, std::ios::binary);
    if (!in || !in.seekg(static_cast<std::streamoff>(offset))) {
        error = "cannot read " + (source.folder / name).string();
        return false;
    }
    std::vector<char> chunk(kChunk);
    while (length) {
        const size_t n = static_cast<size_t>(std::min<uint64_t>(length, chunk.size()));
        if (!in.read(chunk.data(), static_cast<std::streamsize>(n))) {
            error = (source.folder / name).string() + " is shorter than the index says";
            return false;
        }
        if (!sink(chunk.data(), n)) {
            error = "cancelled";
            return false;
        }
        length -= n;
    }
    return true;
}

// The pack's index section as text (what a resumed download must match).
std::string pack_signature(const platform::TextureIndex& index, const platform::TextureIndex::Pack& pack) {
    platform::TextureIndex one;
    one.release = index.release;
    one.packs.push_back(pack);
    return platform::format_texture_index(one);
}

using FileKey = std::tuple<uint64_t, char, std::string>;  // id, kind, stored SHA-256

}  // namespace

std::optional<PackSource> newest_pack_release(const std::string& repository, const std::string& user_agent,
                                              std::string& error) {
    PackSource source;
    source.user_agent = user_agent;
    if (const char* base = std::getenv("COD_BRO_TEXTURE_SOURCE"); base && *base) {
        source.base_url = base;
        if (source.base_url.back() != '/') source.base_url += '/';
        return source;
    }
    const std::optional<std::string> body =
        platform::https_get("https://api.github.com/repos/" + repository + "/releases?per_page=1", user_agent);
    const std::optional<std::string> tag = body ? newest_tag(*body) : std::nullopt;
    if (!tag) {
        error = body ? "the pack repository has no release yet" : "no answer from GitHub (offline?)";
        return std::nullopt;
    }
    source.base_url = "https://github.com/" + repository + "/releases/download/" + *tag + "/";
    return source;
}

std::optional<platform::TextureIndex> read_pack_index(const PackSource& source, std::string& error) {
    std::optional<std::string> text, sums;
    if (source.web()) {
        text = platform::https_get(source.base_url + kIndexName, source.user_agent, 60);
        sums = platform::https_get(source.base_url + "SHA256SUMS.txt", source.user_agent, 30);
        if (!text || !sums) {
            error = "cannot download the pack list (" + source.base_url + ")";
            return std::nullopt;
        }
    } else {
        text = read_file(source.folder / kIndexName);
        sums = read_file(source.folder / "SHA256SUMS.txt");
        if (!text) {
            error = "no " + std::string(kIndexName) + " in " + source.folder.string();
            return std::nullopt;
        }
    }
    if (sums) {
        const std::optional<std::string> expected = checksum_for(*sums, kIndexName);
        if (!expected || platform::sha256_hex(*text) != *expected) {
            error = "the pack list does not match its SHA256SUMS.txt (damaged download?)";
            return std::nullopt;
        }
    }
    std::string why;
    auto index = platform::parse_texture_index(*text, &why);
    if (!index) error = "the pack list is unreadable: " + why;
    return index;
}

std::vector<InstalledPack> installed_packs(const fs::path& mods_dir) {
    std::vector<InstalledPack> out;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(mods_dir, ec)) {
        if (!entry.is_directory(ec)) continue;
        const std::optional<std::string> text = read_file(entry.path() / "pack.idx");
        if (!text) continue;
        auto index = platform::parse_texture_index(*text);
        if (!index || index->packs.size() != 1) continue;
        InstalledPack pack;
        pack.id = index->packs[0].id;
        pack.release = index->release;
        pack.name = index->packs[0].ini_value("name");
        pack.files = index->packs[0].files.size();
        pack.bytes = index->packs[0].stored_bytes();
        pack.index = std::move(*index);
        if (entry.path().filename() == pack.id) out.push_back(std::move(pack));
    }
    return out;
}

bool install_pack(const PackSource& source, const platform::TextureIndex& index, const std::string& pack_id,
                  const fs::path& mods_dir, const std::function<void(const PackProgress&)>& progress,
                  const std::atomic<bool>& cancel, std::string& error) {
    const platform::TextureIndex::Pack* pack = index.find(pack_id);
    if (!pack) {
        error = "the release has no pack '" + pack_id + "'";
        return false;
    }
    const fs::path target = mods_dir / pack_id, work = mods_dir / (pack_id + ".download");
    const fs::path dat = work / "pack.dat";
    std::error_code ec;
    fs::create_directories(work, ec);
    if (ec) {
        error = "cannot write to " + mods_dir.string() + ": " + ec.message();
        return false;
    }
    PackProgress state;
    state.total = pack->stored_bytes();
    auto say = [&](std::string message) {
        state.message = std::move(message);
        progress(state);
    };

    // Resume only a download of the same pack version; anything else starts over.
    const std::string signature = pack_signature(index, *pack);
    if (read_file(work / "download.idx") != signature) {
        fs::remove(dat, ec);
        std::ofstream(work / "download.idx", std::ios::binary) << signature;
    }

    // Files the installed version holds: copied, not downloaded.
    std::map<FileKey, std::pair<uint64_t, uint64_t>> old_files;  // -> offset, stored in the old pack.dat
    if (const auto text = read_file(target / "pack.idx")) {
        const auto old = platform::parse_texture_index(*text);
        if (old && old->packs.size() == 1)
            for (const auto& f : old->packs[0].files) old_files[{f.id, f.kind, f.sha256}] = {f.offset, f.stored};
    }

    // What the download folder already has: kept up to the first file that does not check out.
    const uint64_t have = fs::exists(dat, ec) ? fs::file_size(dat, ec) : 0;
    platform::Sha256 whole;  // pack.dat's checksum, built in order
    uint64_t written = 0;
    size_t next = 0;
    if (have) {
        say("Checking the earlier download...");
        std::ifstream in(dat, std::ios::binary);
        std::vector<char> buffer;
        for (; next < pack->files.size(); ++next) {
            const auto& f = pack->files[next];
            if (written + f.stored > have || cancel) break;
            buffer.resize(f.stored);
            if (!in.read(buffer.data(), static_cast<std::streamsize>(f.stored))) break;
            if (platform::sha256_hex({buffer.data(), buffer.size()}) != f.sha256) break;
            whole.update(buffer.data(), buffer.size());
            written += f.stored;
            state.done = written;
            progress(state);
        }
        if (cancel) {
            error = "cancelled";
            return false;
        }
        fs::resize_file(dat, written, ec);
    }

    const uint64_t needed = state.total - written;
    const fs::space_info space = fs::space(mods_dir, ec);
    if (!ec && space.available < needed + (64u << 20)) {
        error = "not enough free space: " + megabytes(needed) + " needed, " + megabytes(space.available) + " free";
        return false;
    }

    std::ofstream out(dat, std::ios::binary | std::ios::app);
    std::ifstream old_dat(target / "pack.dat", std::ios::binary);
    if (!out) {
        error = "cannot write " + dat.string();
        return false;
    }
    const std::string verb = old_files.empty() ? "Installing " : "Updating ";
    const std::string name = pack->ini_value("name").empty() ? pack_id : pack->ini_value("name");
    while (next < pack->files.size()) {
        const auto& f = pack->files[next];
        // Copy from the installed version.
        if (auto it = old_files.find({f.id, f.kind, f.sha256}); it != old_files.end() && old_dat) {
            std::string buffer(f.stored, '\0');
            old_dat.clear();
            if (old_dat.seekg(static_cast<std::streamoff>(it->second.first)) &&
                old_dat.read(buffer.data(), static_cast<std::streamsize>(buffer.size())) &&
                platform::sha256_hex(buffer) == f.sha256) {
                out.write(buffer.data(), static_cast<std::streamsize>(buffer.size()));
                whole.update(buffer);
                written += f.stored;
                state.done = written;
                ++next;
                say(verb + name + ": " + megabytes(state.done) + " of " + megabytes(state.total));
                continue;
            }
        }
        // A run of files lying back to back in one part, none of them in the installed version: one request.
        size_t end = next + 1;
        while (end < pack->files.size()) {
            const auto& prev = pack->files[end - 1];
            const auto& g = pack->files[end];
            if (g.part != f.part || g.offset != prev.offset + prev.stored || old_files.count({g.id, g.kind, g.sha256}))
                break;
            ++end;
        }
        for (int attempt = 0; next < end;) {
            // From the first file not yet written to the run's end; each finished file is checked, then written.
            const auto& last = pack->files[end - 1];
            const uint64_t from = pack->files[next].offset, length = last.offset + last.stored - from;
            std::string current;
            size_t at = next;
            bool damaged = false;
            const auto sink = [&](const char* data, size_t size) {
                while (size) {
                    if (cancel) return false;
                    const auto& file = pack->files[at];
                    const size_t take = static_cast<size_t>(std::min<uint64_t>(size, file.stored - current.size()));
                    current.append(data, take);
                    data += take, size -= take;
                    if (current.size() == file.stored) {
                        if (platform::sha256_hex(current) != file.sha256) {
                            damaged = true;
                            return false;
                        }
                        out.write(current.data(), static_cast<std::streamsize>(current.size()));
                        if (!out) return false;
                        whole.update(current);
                        written += file.stored;
                        current.clear();
                        ++at;
                        state.done = written;
                        say(verb + name + ": " + megabytes(state.done) + " of " + megabytes(state.total));
                    }
                }
                return true;
            };
            std::string why;
            const bool ok = fetch(source, index.parts[f.part - 1].name, from, length, sink, why);
            if (at > next) attempt = 0;  // it got further: a fresh set of attempts
            next = at;
            if (ok || next >= end) break;
            if (cancel) {
                error = "cancelled";
                return false;
            }
            if (!out) {
                error = "cannot write " + dat.string() + " (disk full?)";
                return false;
            }
            if (damaged) why = "a file arrived damaged";
            if (!source.web()) {  // a folder reads the same bytes again: no retries
                error = damaged ? index.parts[f.part - 1].name + " is damaged (download it again)" : why;
                return false;
            }
            LOG_WARN("launcher", "texture pack %s: %s (attempt %d of %d)", pack_id.c_str(), why.c_str(), attempt + 1,
                     kAttempts);
            if (++attempt == kAttempts) {
                error = why;
                return false;
            }
            say(verb + name + ": retrying (" + why + ")");
            std::this_thread::sleep_for(std::chrono::seconds(2 * attempt));
        }
    }
    out.close();
    if (!out) {
        error = "cannot write " + dat.string() + " (disk full?)";
        return false;
    }
    old_dat.close();

    // pack.idx (offsets in pack.dat), mod.ini, then the finished folder takes the old one's place.
    platform::TextureIndex installed;
    installed.release = index.release;
    installed.parts.push_back({"pack.dat", written, whole.finish_hex()});
    installed.packs.push_back(*pack);
    uint64_t offset = 0;
    for (auto& f : installed.packs[0].files) {
        f.part = 1;
        f.offset = offset;
        offset += f.stored;
    }
    {
        std::ofstream idx(work / "pack.idx", std::ios::binary);
        idx << platform::format_texture_index(installed);
        std::ofstream ini(work / "mod.ini", std::ios::binary);
        for (const auto& line : pack->ini) ini << line << "\n";
        if (!idx || !ini) {
            error = "cannot write to " + work.string();
            return false;
        }
    }
    fs::remove(work / "download.idx", ec);
    const fs::path old = mods_dir / (pack_id + ".old");
    fs::remove_all(old, ec);
    if (fs::exists(target, ec)) {
        fs::rename(target, old, ec);
        if (ec) {
            error = "cannot replace " + target.string() + " (is the game running?): " + ec.message();
            return false;
        }
    }
    fs::rename(work, target, ec);
    if (ec) {
        error = "cannot move the pack into place: " + ec.message();
        std::error_code back;
        if (fs::exists(old, back)) fs::rename(old, target, back);
        return false;
    }
    fs::remove_all(old, ec);
    say(name + " " + index.release + " is installed.");
    LOG_INFO("launcher", "texture pack %s %s installed (%zu files, %s)", pack_id.c_str(), index.release.c_str(),
             pack->files.size(), megabytes(written).c_str());
    return true;
}

bool remove_pack(const fs::path& mods_dir, const std::string& pack_id, std::string& error) {
    std::error_code ec;
    fs::remove_all(mods_dir / (pack_id + ".download"), ec);
    fs::remove_all(mods_dir / pack_id, ec);
    if (ec) {
        error = "cannot delete " + (mods_dir / pack_id).string() + ": " + ec.message();
        return false;
    }
    LOG_INFO("launcher", "texture pack %s removed", pack_id.c_str());
    return true;
}

std::unique_ptr<PackTask> PackTask::lookup(std::string repository, fs::path folder, std::string user_agent) {
    auto task = std::unique_ptr<PackTask>(new PackTask);
    std::thread([shared = task->shared_, repository = std::move(repository), folder = std::move(folder),
                 user_agent = std::move(user_agent)] {
        Result result;
        std::string error;
        std::optional<PackSource> source;
        if (folder.empty()) {
            source = newest_pack_release(repository, user_agent, error);
        } else {
            source = PackSource{folder, {}, user_agent};
        }
        if (source) {
            result.index = read_pack_index(*source, error);
            if (result.index) result.source = std::move(source);
        }
        result.status = result.index ? Status::Done : Status::Failed;
        result.error = error;
        std::lock_guard lock(shared->mutex);
        shared->result = std::move(result);
    }).detach();
    return task;
}

std::unique_ptr<PackTask> PackTask::install(PackSource source, platform::TextureIndex index, std::string pack_id,
                                            fs::path mods_dir) {
    auto task = std::unique_ptr<PackTask>(new PackTask);
    std::thread([shared = task->shared_, source = std::move(source), index = std::move(index),
                 pack_id = std::move(pack_id), mods_dir = std::move(mods_dir)] {
        std::string error;
        const bool ok = install_pack(
            source, index, pack_id, mods_dir,
            [&](const PackProgress& p) {
                std::lock_guard lock(shared->mutex);
                shared->result.progress = p;
            },
            shared->cancel, error);
        std::lock_guard lock(shared->mutex);
        shared->result.status = ok ? Status::Done : shared->cancel ? Status::Cancelled : Status::Failed;
        shared->result.error = error;
        if (!ok) LOG_WARN("launcher", "texture pack %s: %s", pack_id.c_str(), error.c_str());
    }).detach();
    return task;
}

PackTask::~PackTask() { cancel(); }

PackTask::Result PackTask::result() const {
    std::lock_guard lock(shared_->mutex);
    return shared_->result;
}

}  // namespace launcher
