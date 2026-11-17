#include "game_files.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <thread>

#include "log.h"
#include "paths.h"
#include "sha1.h"

namespace launcher {

namespace fs = std::filesystem;

namespace {

constexpr uint32_t kSector = 2048;
constexpr char kMagic[] = "MICROSOFT*XBOX*MEDIA";
// Where the game partition starts: an XISO (only the partition), then full images of the three disc formats.
constexpr uint64_t kPartitions[] = {0, 0x18300000, 0xFD90000, 0x2080000};
constexpr uint32_t kChunk = 4u << 20;

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string hex(const uint8_t* data, size_t size) {
    static constexpr char kHex[] = "0123456789abcdef";
    std::string out;
    for (size_t i = 0; i < size; ++i) out += kHex[data[i] >> 4], out += kHex[data[i] & 15];
    return out;
}

std::string megabytes(uint64_t bytes) {
    char text[32];
    std::snprintf(text, sizeof(text), "%.1f GB", static_cast<double>(bytes) / (1ull << 30));
    return bytes >= (1ull << 30) ? std::string(text) : std::to_string((bytes + (1 << 19)) >> 20) + " MB";
}

// A file of the source, readable by offset.
struct SourceFile {
    std::string path;
    uint64_t size = 0, offset = 0;  // offset: in the image; 0 for a folder's file
};

// An image or a folder, as a list of files and a way to read them.
class Source {
public:
    bool open(const fs::path& where, std::string& error) {
        std::error_code ec;
        if (fs::is_directory(where, ec)) {
            folder_ = where;
            for (auto it = fs::recursive_directory_iterator(where, ec); !ec && it != fs::recursive_directory_iterator();
                 it.increment(ec)) {
                if (!it->is_regular_file(ec)) continue;
                SourceFile f;
                f.path = fs::relative(it->path(), where, ec).generic_string();
                f.size = it->file_size(ec);
                files_.push_back(std::move(f));
            }
            return true;
        }
        image_ = DiscImage::open(where, error);
        if (!image_) return false;
        for (const auto& e : image_->files()) files_.push_back({e.path, e.size, e.offset});
        return true;
    }
    const std::vector<SourceFile>& files() const { return files_; }

    // Streams the file to `sink` in chunks; false on a read error or when `sink` says stop.
    bool read(const SourceFile& f, const std::function<bool(const uint8_t*, uint32_t)>& sink) {
        std::vector<uint8_t> buffer(static_cast<size_t>(std::min<uint64_t>(kChunk, std::max<uint64_t>(f.size, 1))));
        std::unique_ptr<platform::File> file;
        if (!image_) {
            std::error_code ec;
            file = platform::File::open(folder_ / fs::path(f.path), platform::File::Access::Read, false, ec);
            if (!file) return false;
        }
        for (uint64_t at = 0; at < f.size;) {
            const uint32_t n = static_cast<uint32_t>(std::min<uint64_t>(buffer.size(), f.size - at));
            if (image_) {
                if (!image_->read(f.offset + at, buffer.data(), n)) return false;
            } else {
                uint32_t done = 0;
                std::error_code ec;
                if (!file->read_at(at, buffer.data(), n, done, ec) || done != n) return false;
            }
            if (!sink(buffer.data(), n)) return false;
            at += n;
        }
        return true;
    }

    std::optional<std::string> sha1(const SourceFile& f, const std::function<bool(uint32_t)>& step) {
        platform::sha1::Context c;
        platform::sha1::init(c);
        const bool ok = read(f, [&](const uint8_t* data, uint32_t n) {
            platform::sha1::update(c, data, n);
            return step(n);
        });
        if (!ok) return std::nullopt;
        uint8_t digest[20];
        platform::sha1::final(c, digest);
        return hex(digest, 20);
    }

private:
    fs::path folder_;
    std::unique_ptr<DiscImage> image_;
    std::vector<SourceFile> files_;
};

}  // namespace

const GameFileTable::Edition* GameFileTable::by_xbe(const std::string& sha1) const {
    for (const auto& e : editions)
        if (e.xbe_sha1 == sha1) return &e;
    return nullptr;
}

const GameFileTable::Edition* GameFileTable::by_id(const std::string& id) const {
    for (const auto& e : editions)
        if (e.id == id) return &e;
    return nullptr;
}

std::optional<GameFileTable> parse_game_file_table(const std::string& text) {
    GameFileTable table;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        std::istringstream words(line);
        if (line.rfind("edition ", 0) == 0) {
            GameFileTable::Edition e;
            std::string word;
            words >> word >> e.id >> e.xbe_sha1;
            std::getline(words >> std::ws, e.name);
            if (e.id.empty() || e.xbe_sha1.size() != 40) return std::nullopt;
            table.editions.push_back(std::move(e));
            continue;
        }
        if (table.editions.empty()) return std::nullopt;
        GameFileTable::Row row;
        words >> row.sha1 >> row.size;
        std::getline(words >> std::ws, row.path);
        if (row.sha1.size() != 40 || row.path.empty()) return std::nullopt;
        table.editions.back().files.push_back(std::move(row));
    }
    if (table.editions.empty()) return std::nullopt;
    return table;
}

std::optional<GameFileTable> load_game_file_table() {
    fs::path path = platform::executable_dir() / "game_files.txt";
    if (const char* env = std::getenv("COD_BRO_GAME_FILE_TABLE"); env && *env) path = env;
    std::ifstream in(path, std::ios::binary);
    if (!in) return std::nullopt;
    return parse_game_file_table(std::string(std::istreambuf_iterator<char>(in), {}));
}

std::unique_ptr<DiscImage> DiscImage::open(const fs::path& path, std::string& error) {
    std::error_code ec;
    auto image = std::unique_ptr<DiscImage>(new DiscImage);
    image->file_ = platform::File::open(path, platform::File::Access::Read, false, ec);
    if (!image->file_) {
        error = "cannot open " + path.string() + ": " + ec.message();
        return nullptr;
    }
    image->size_ = image->file_->size().value_or(0);
    uint64_t base = UINT64_MAX;
    uint8_t volume[28];
    for (const uint64_t start : kPartitions) {
        if (!image->read(start + 32ull * kSector, volume, sizeof(volume))) continue;
        if (std::memcmp(volume, kMagic, 20) == 0) {
            base = start;
            break;
        }
    }
    if (base == UINT64_MAX) {
        error = "not an Xbox disc dump (no Xbox file system found)";
        return nullptr;
    }
    uint32_t root_sector, root_size;
    std::memcpy(&root_sector, volume + 20, 4);
    std::memcpy(&root_size, volume + 24, 4);

    // Directory tables are binary trees of entries (left, right in 4-byte units; start sector, size, attributes,
    // name). A table is read whole; entries never cross a sector (0xFF padding fills the rest).
    std::vector<std::tuple<uint32_t, uint32_t, std::string>> pending{{root_sector, root_size, ""}};
    size_t tables = 0;
    while (!pending.empty()) {
        auto [sector, size, prefix] = pending.back();
        pending.pop_back();
        if (!size) continue;
        if (++tables > 100000 || size > (64u << 20)) {
            error = "the dump's file system is damaged";
            return nullptr;
        }
        std::vector<uint8_t> table(size);
        if (!image->read(base + uint64_t{sector} * kSector, table.data(), size)) {
            error = "the dump is shorter than its file system says (incomplete copy?)";
            return nullptr;
        }
        std::vector<uint32_t> stack{0};
        std::vector<bool> seen(size / 4 + 1);
        while (!stack.empty()) {
            const uint32_t off = stack.back();
            stack.pop_back();
            if (off + 14 > size || seen[off / 4]) continue;
            seen[off / 4] = true;
            uint16_t left, right;
            uint32_t start, length;
            std::memcpy(&left, &table[off], 2);
            std::memcpy(&right, &table[off + 2], 2);
            std::memcpy(&start, &table[off + 4], 4);
            std::memcpy(&length, &table[off + 8], 4);
            const uint8_t attributes = table[off + 12], name_length = table[off + 13];
            if (left == 0xFFFF || off + 14 + name_length > size) continue;
            std::string name(reinterpret_cast<const char*>(&table[off + 14]), name_length);
            if (left) stack.push_back(uint32_t{left} * 4);
            if (right) stack.push_back(uint32_t{right} * 4);
            if (name.empty() || name == "." || name == ".." || name.find_first_of("/\\:") != std::string::npos) {
                error = "the dump's file system is damaged (bad name)";
                return nullptr;
            }
            if (attributes & 0x10) {
                pending.emplace_back(start, length, prefix + name + "/");
            } else {
                const uint64_t offset = base + uint64_t{start} * kSector;
                if (offset + length > image->size_) {
                    error = "the dump is shorter than its file system says (incomplete copy?)";
                    return nullptr;
                }
                image->files_.push_back({prefix + name, length, offset});
            }
        }
    }
    std::sort(image->files_.begin(), image->files_.end(),
              [](const Entry& a, const Entry& b) { return lower(a.path) < lower(b.path); });
    return image;
}

bool DiscImage::read(uint64_t offset, void* buffer, uint32_t size) {
    uint32_t done = 0;
    std::error_code ec;
    return file_->read_at(offset, buffer, size, done, ec) && done == size;
}

std::string GameFilesCheck::summary() const {
    if (!has_xbe) return "No default.xbe: not the game's files.";
    std::string text;
    if (edition) {
        text = edition->name + ": ";
        if (missing.empty() && damaged.empty())
            return text + "all " + std::to_string(files) + " files check out (" + megabytes(bytes) + ").";
    } else {
        text = "Not an edition this launcher knows (another region or language?), untested: ";
        if (missing.empty()) return text + "every file of the standard release is there.";
        return text + std::to_string(missing.size()) + (missing.size() == 1 ? " file" : " files") +
               " of the standard release are not on it (" +
               missing.front() + (missing.size() > 1 ? ", ..." : "") +
               "), as on discs in other languages. It can still be extracted and played.";
    }
    if (!missing.empty()) text += std::to_string(missing.size()) + " missing";
    if (!damaged.empty()) text += std::string(missing.empty() ? "" : ", ") + std::to_string(damaged.size()) + " damaged";
    text += " (" + (missing.empty() ? damaged.front() : missing.front()) +
            (missing.size() + damaged.size() > 1 ? ", ..." : "") + "). Make a new dump of the disc.";
    return text;
}

bool check_game_files(const fs::path& source_path, const GameFileTable& table, GameFilesCheck& result,
                      const std::function<void(const GameFilesProgress&)>& progress, const std::atomic<bool>& cancel,
                      std::string& error) {
    result = {};
    Source source;
    if (!source.open(source_path, error)) return false;
    std::map<std::string, const SourceFile*> by_path;
    for (const auto& f : source.files()) by_path.emplace(lower(f.path), &f);
    GameFilesProgress state;
    state.message = "Reading default.xbe...";
    progress(state);
    const auto xbe = by_path.find("default.xbe");
    if (xbe == by_path.end()) return true;
    result.has_xbe = true;
    const auto never = [&](uint32_t) { return !cancel; };
    const std::optional<std::string> xbe_sha1 = source.sha1(*xbe->second, never);
    if (!xbe_sha1) {
        error = cancel ? "cancelled" : "cannot read default.xbe";
        return false;
    }
    result.edition = table.by_xbe(*xbe_sha1);
    const GameFileTable::Edition* reference = result.edition ? result.edition : table.by_id("standard");
    if (!reference) reference = &table.editions.front();
    for (const auto& row : reference->files) {
        state.total += row.size;
        ++result.files;
    }
    result.bytes = state.total;
    // Unknown edition: only the tree (its files may differ). Known: sizes and checksums too.
    for (const auto& row : reference->files) {
        const auto it = by_path.find(lower(row.path));
        if (it == by_path.end()) {
            result.missing.push_back(row.path);
            state.done += row.size;
            continue;
        }
        if (!result.edition) continue;
        if (it->second->size != row.size) {
            result.damaged.push_back(row.path);
            state.done += row.size;
            continue;
        }
        state.message = "Checking " + row.path;
        uint64_t last = state.done;
        const auto sha1 = source.sha1(*it->second, [&](uint32_t n) {
            state.done += n;
            if (state.done - last >= (64u << 20)) {
                last = state.done;
                progress(state);
            }
            return !cancel;
        });
        if (cancel) {
            error = "cancelled";
            return false;
        }
        if (!sha1 || *sha1 != row.sha1) result.damaged.push_back(row.path);
    }
    state.done = state.total;
    state.message = result.summary();
    progress(state);
    LOG_INFO("launcher", "game files %s: %s", source_path.string().c_str(), result.summary().c_str());
    return true;
}

bool extract_disc_image(const fs::path& image_path, const GameFileTable::Edition* edition, const fs::path& target,
                        const std::function<void(const GameFilesProgress&)>& progress,
                        const std::atomic<bool>& cancel, std::string& error) {
    auto image = DiscImage::open(image_path, error);
    if (!image) return false;
    std::map<std::string, const GameFileTable::Row*> expected;
    if (edition)
        for (const auto& row : edition->files) expected.emplace(lower(row.path), &row);
    GameFilesProgress state;
    for (const auto& f : image->files()) state.total += f.size;
    std::error_code ec;
    fs::create_directories(target, ec);
    if (ec) {
        error = "cannot create " + target.string() + ": " + ec.message();
        return false;
    }
    const fs::space_info space = fs::space(target, ec);
    if (!ec && space.available < state.total + (64u << 20)) {
        error = "not enough free space in " + target.string() + ": " + megabytes(state.total) + " needed, " +
                megabytes(space.available) + " free";
        return false;
    }
    std::vector<uint8_t> buffer(kChunk);
    for (const auto& f : image->files()) {
        const fs::path out_path = target / fs::path(f.path);
        fs::create_directories(out_path.parent_path(), ec);
        const fs::path part = out_path.string() + ".part";
        std::ofstream out(part, std::ios::binary | std::ios::trunc);
        if (!out) {
            error = "cannot write " + part.string();
            return false;
        }
        state.message = "Extracting " + f.path;
        progress(state);
        platform::sha1::Context c;
        platform::sha1::init(c);
        for (uint64_t at = 0; at < f.size;) {
            if (cancel) {
                out.close();
                fs::remove(part, ec);
                error = "cancelled";
                return false;
            }
            const uint32_t n = static_cast<uint32_t>(std::min<uint64_t>(buffer.size(), f.size - at));
            if (!image->read(f.offset + at, buffer.data(), n)) {
                error = "cannot read " + f.path + " from the dump";
                return false;
            }
            platform::sha1::update(c, buffer.data(), n);
            out.write(reinterpret_cast<const char*>(buffer.data()), n);
            at += n;
            state.done += n;
            if ((at & ((64u << 20) - 1)) == 0) progress(state);
        }
        out.close();
        if (!out) {
            fs::remove(part, ec);
            error = "cannot write " + out_path.string() + " (disk full?)";
            return false;
        }
        uint8_t digest[20];
        platform::sha1::final(c, digest);
        if (auto it = expected.find(lower(f.path)); it != expected.end() && hex(digest, 20) != it->second->sha1) {
            fs::remove(part, ec);
            error = f.path + " in the dump is damaged; nothing after it was extracted. Make a new dump of the disc.";
            return false;
        }
        fs::rename(part, out_path, ec);
        if (ec) {
            error = "cannot replace " + out_path.string() + ": " + ec.message();
            return false;
        }
    }
    state.done = state.total;
    state.message = "The game files are extracted (" + std::to_string(image->files().size()) + " files).";
    progress(state);
    LOG_INFO("launcher", "extracted %s into %s (%zu files)", image_path.string().c_str(), target.string().c_str(),
             image->files().size());
    return true;
}

std::unique_ptr<GameFilesTask> GameFilesTask::check(fs::path source, std::shared_ptr<const GameFileTable> table) {
    auto task = std::unique_ptr<GameFilesTask>(new GameFilesTask);
    task->shared_->table = table;
    std::thread([shared = task->shared_, source = std::move(source)] {
        GameFilesCheck check;
        std::string error;
        const bool ok = check_game_files(
            source, *shared->table, check,
            [&](const GameFilesProgress& p) {
                std::lock_guard lock(shared->mutex);
                shared->result.progress = p;
            },
            shared->cancel, error);
        std::lock_guard lock(shared->mutex);
        shared->result.check = check;
        shared->result.error = error;
        shared->result.status = ok ? Status::Done : shared->cancel ? Status::Cancelled : Status::Failed;
    }).detach();
    return task;
}

std::unique_ptr<GameFilesTask> GameFilesTask::extract(fs::path image, std::shared_ptr<const GameFileTable> table,
                                                      std::string edition_id, fs::path target) {
    auto task = std::unique_ptr<GameFilesTask>(new GameFilesTask);
    task->shared_->table = table;
    std::thread([shared = task->shared_, image = std::move(image), edition_id = std::move(edition_id),
                 target = std::move(target)] {
        std::string error;
        const bool ok = extract_disc_image(
            image, shared->table->by_id(edition_id), target,
            [&](const GameFilesProgress& p) {
                std::lock_guard lock(shared->mutex);
                shared->result.progress = p;
            },
            shared->cancel, error);
        std::lock_guard lock(shared->mutex);
        shared->result.error = error;
        shared->result.status = ok ? Status::Done : shared->cancel ? Status::Cancelled : Status::Failed;
        if (!ok) LOG_WARN("launcher", "extracting %s: %s", image.string().c_str(), error.c_str());
    }).detach();
    return task;
}

GameFilesTask::~GameFilesTask() { cancel(); }

GameFilesTask::Result GameFilesTask::result() const {
    std::lock_guard lock(shared_->mutex);
    return shared_->result;
}

}  // namespace launcher
