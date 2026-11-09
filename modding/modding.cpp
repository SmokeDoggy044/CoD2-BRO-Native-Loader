// See modding.h.

#include "modding.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "imgui.h"

#include "animation.h"
#include "audio_bank.h"
#include "audio_codec.h"
#include "csv.h"
#include "image.h"
#include "log.h"
#include "pack.h"
#include "paths.h"
#include "texture_hash.h"

namespace fs = std::filesystem;

namespace modding {
namespace {

enum class Job { ExtractTextures, ReplaceTextures, ExtractAudio, ReplaceAudio,
                 ExtractAnimations, ReplaceAnimations, Count };

struct JobState {
    std::atomic<bool> running{false};
    std::atomic<uint32_t> done{0}, total{0};
    std::mutex mutex;
    std::string status = "Idle", current, message;
    bool ok = false, finished = false;
};

std::array<JobState, static_cast<size_t>(Job::Count)> g_jobs;
std::atomic<int> g_active{-1};
std::atomic<bool> g_stop{false};
std::thread g_worker;

bool g_open = false;
fs::path g_assets, g_mods_root;
std::mutex g_replacements_mutex;
std::unordered_map<uint64_t, std::shared_ptr<const render::TextureReplacement>> g_replacements;
std::atomic<uint32_t> g_version{1};

JobState& state(Job job) { return g_jobs[static_cast<size_t>(job)]; }

fs::path root() {
    return g_mods_root.empty() ? platform::user_data_dir() / "mods" : g_mods_root;
}

void progress(Job job, uint32_t done, uint32_t total, const std::string& status, const std::string& current) {
    JobState& s = state(job);
    s.done.store(done);
    s.total.store(total);
    std::lock_guard lock(s.mutex);
    s.status = status;
    s.current = current;
}

void finish(Job job, bool ok, const std::string& message) {
    JobState& s = state(job);
    {
        std::lock_guard lock(s.mutex);
        s.ok = ok;
        s.finished = true;
        s.message = message;
        s.status = ok ? "Done" : "Failed";
        s.current.clear();
    }
    s.running.store(false);
    g_active.store(-1);
}

bool stopped() { return g_stop.load(); }

void write_readme(const fs::path& path, const std::string& text) {
    std::error_code ec;
    if (fs::exists(path, ec)) return;
    fs::create_directories(path.parent_path(), ec);
    std::ofstream out(path);
    out << text;
}

std::string readme_textures() {
    return "Texture modding - CoD2: BRO native loader\n"
           "=========================================\n\n"
           "Press Extract in the Modding window (F4) to write every DDS picture from the\n"
           "game's .cod packs as PNG here. textures.csv maps each PNG to the game's\n"
           "texture content hash; textures_manifest.csv lists every occurrence.\n\n"
           "Edit any PNG (keep the __<hash> suffix in its name, or keep textures.csv\n"
           "unchanged) and press Replace. Replacements show in the running game: the\n"
           "renderer asks for the texture by content hash and receives the PNG instead.\n"
           "Any size works; mipmaps are generated from it. Alpha is supported.\n\n"
           "To go back to the original picture, delete the PNG (or move it elsewhere)\n"
           "and press Replace again.\n";
}

std::string readme_audio() {
    return "Audio modding - CoD2: BRO native loader\n"
           "=======================================\n\n"
           "Press Extract in the Modding window (F4) to decode the game's wave banks\n"
           "(.wbk, loose or inside .cod packs) and loose WAV files into ordinary\n"
           "16-bit PCM WAV files here. audio.csv records each sound's original format,\n"
           "byte offset and length.\n\n"
           "Edit a WAV (16-bit PCM is easiest; 8/24/32-bit and float also work), keep\n"
           "its name, and press Replace. The loader converts it back to the game's\n"
           "original codec (PCM 8/16 or Xbox ADPCM) at the original rate, channels and\n"
           "length, then writes a patched copy of the .wbk/.cod into\n"
           "mods/overrides/. The original game files are never modified. Restart the\n"
           "game (or reload the level) to hear the new sound. Delete files in\n"
           "mods/overrides/ to restore the originals.\n";
}

std::string readme_animations() { return animation_readme(); }

// --- Texture extraction ---------------------------------------------------------------

struct TextureOccurrence {
    fs::path source;
    std::string relative;
    std::string name, asset_path;
    uint64_t offset = 0;
    uint32_t size = 0;
};

std::vector<TextureOccurrence> texture_occurrences(const fs::path& assets) {
    std::vector<TextureOccurrence> out;
    for (const Pack& pack : find_packs(assets)) {
        std::error_code ec;
        const uint64_t pack_size = static_cast<uint64_t>(fs::file_size(pack.file, ec));
        if (ec) continue;
        std::ifstream in(pack.file, std::ios::binary);
        if (!in) continue;
        for (const PackEntry& entry : pack.entries) {
            if (entry.offset + 4 > pack_size) continue;
            char magic[4] = {};
            in.clear();
            in.seekg(static_cast<std::streamoff>(entry.offset), std::ios::beg);
            in.read(magic, 4);
            if (std::string(magic, 4) != "DDS ") continue;
            out.push_back({pack.file, pack.relative, entry.name, entry.asset_path, entry.offset, entry.size});
        }
    }
    for (const fs::path& file : find_files(assets, {".dds"})) {
        std::error_code ec;
        const auto size = fs::file_size(file, ec);
        if (ec || size > UINT32_MAX) continue;
        const std::string relative = file.lexically_relative(assets).generic_string();
        out.push_back({file, relative, file.filename().generic_string(), "loose/" + relative, 0,
                       static_cast<uint32_t>(size)});
    }
    return out;
}

void extract_textures_job() {
    const Job job = Job::ExtractTextures;
    const std::vector<TextureOccurrence> occurrences = texture_occurrences(g_assets);
    const uint32_t total = static_cast<uint32_t>(occurrences.size());
    progress(job, 0, total, "Extracting textures", "Reading the game's packs");
    if (!total) {
        finish(job, false, "No DDS textures found. Check that the game files are in the assets folder.");
        return;
    }
    const fs::path dir = textures_dir();
    std::error_code ec;
    fs::create_directories(dir, ec);
    std::ofstream manifest(dir / "textures_manifest.csv");
    std::ofstream index(dir / "textures.csv");
    manifest << csv_line({"source", "entry", "asset_path", "offset", "size", "hash", "output", "note"});
    index << csv_line({"hash", "file", "asset_path", "source", "offset", "size", "width", "height"});
    std::set<uint64_t> written;
    std::set<std::string> used_files;
    uint32_t done = 0, png_count = 0, failed = 0;
    for (const TextureOccurrence& occurrence : occurrences) {
        if (stopped()) break;
        progress(job, done, total, "Extracting textures", occurrence.asset_path);
        std::vector<uint8_t> bytes;
        std::string error;
        const bool ok = occurrence.offset ? read_bytes(occurrence.source, occurrence.offset, occurrence.size, bytes, error)
                                           : read_bytes(occurrence.source, bytes, error);
        if (!ok || bytes.size() < 132 || std::string(reinterpret_cast<const char*>(bytes.data()), 4) != "DDS ") {
            ++failed;
            ++done;
            continue;
        }
        const uint64_t hash = render::hash_bytes(bytes.data() + 128, bytes.size() - 128);
        const std::string short_hash = hex64(hash).substr(0, 16);
        const std::string stem = asset_stem(occurrence.asset_path, occurrence.name);
        std::string file = stem + "__" + short_hash + ".png";
        for (int n = 2; used_files.count(file); ++n)
            file = stem + "__" + short_hash + "_" + std::to_string(n) + ".png";
        used_files.insert(file);
        std::string note;
        Image image;
        if (!decode_dds(bytes.data(), bytes.size(), image, error)) {
            note = error;
            ++failed;
            // Keep the original DDS so nothing is lost when a format is unknown.
            const fs::path fallback = dir / (stem + "__" + short_hash + ".dds");
            fs::create_directories(fallback.parent_path(), ec);
            write_bytes(fallback, bytes, error);
        } else {
            if (write_png((dir / file).generic_string(), image, error)) {
                ++png_count;
                index << csv_line({hex64(hash), file, occurrence.asset_path, occurrence.relative,
                                   std::to_string(occurrence.offset), std::to_string(occurrence.size),
                                   std::to_string(image.width), std::to_string(image.height)});
            } else {
                note = error;
                ++failed;
            }
        }
        manifest << csv_line({occurrence.relative, occurrence.name, occurrence.asset_path,
                              std::to_string(occurrence.offset), std::to_string(occurrence.size), hex64(hash), file, note});
        written.insert(hash);
        ++done;
    }
    write_readme(dir / "README.txt", readme_textures());
    progress(job, done, total, "Extracting textures", "");
    finish(job, png_count > 0, "Wrote " + std::to_string(png_count) + " PNG textures (" +
                                   std::to_string(written.size()) + " distinct, " + std::to_string(failed) +
                                   " skipped) to " + dir.generic_string());
}

// --- Texture replacement --------------------------------------------------------------

struct TextureCandidate {
    uint64_t hash = 0;
    fs::path file;
};

std::vector<TextureCandidate> texture_candidates() {
    std::vector<TextureCandidate> out;
    std::set<uint64_t> seen;
    std::ifstream in(textures_dir() / "textures.csv");
    std::string line;
    bool header = true;
    while (std::getline(in, line)) {
        if (header) { header = false; continue; }
        const std::vector<std::string> fields = csv_split(line);
        if (fields.size() < 2) continue;
        const uint64_t hash = parse_hex64(fields[0]);
        if (!hash || seen.count(hash)) continue;
        fs::path file = textures_dir() / fields[1];
        std::error_code ec;
        if (!fs::is_regular_file(file, ec)) file = textures_dir() / (hex64(hash) + ".png");
        if (!fs::is_regular_file(file, ec)) continue;
        seen.insert(hash);
        out.push_back({hash, std::move(file)});
    }
    // Also accept PNGs named "<anything>__<16 hex digits>.png" without a manifest.
    std::error_code ec;
    for (fs::recursive_directory_iterator it(textures_dir(), fs::directory_options::skip_permission_denied, ec), end;
         it != end; it.increment(ec)) {
        if (ec) { ec.clear(); continue; }
        if (!it->is_regular_file(ec) || !ends_with_ci(it->path().extension().generic_string(), ".png")) continue;
        const std::string stem = it->path().stem().generic_string();
        const size_t split = stem.rfind("__");
        if (split == std::string::npos || stem.size() - split - 2 != 16) continue;
        const uint64_t hash = parse_hex64(stem.substr(split + 2));
        if (!hash || seen.count(hash)) continue;
        seen.insert(hash);
        out.push_back({hash, it->path()});
    }
    return out;
}

size_t load_texture_replacements(Job job) {
    const std::vector<TextureCandidate> candidates = texture_candidates();
    const uint32_t total = static_cast<uint32_t>(candidates.size());
    std::unordered_map<uint64_t, std::shared_ptr<const render::TextureReplacement>> loaded;
    uint32_t done = 0;
    for (const TextureCandidate& candidate : candidates) {
        if (stopped()) break;
        progress(job, done, total, "Replacing textures", candidate.file.filename().generic_string());
        Image image;
        std::string error;
        if (read_png(candidate.file.generic_string(), image, error)) {
            if (auto replacement = to_replacement(image)) loaded[candidate.hash] = std::move(replacement);
        } else {
            LOG_WARN("modding", "cannot load %s: %s", candidate.file.generic_string().c_str(), error.c_str());
        }
        ++done;
    }
    {
        std::lock_guard lock(g_replacements_mutex);
        g_replacements = std::move(loaded);
    }
    g_version.fetch_add(1);
    progress(job, done, total, "Replacing textures", "");
    return done;
}

void replace_textures_job() {
    const Job job = Job::ReplaceTextures;
    const size_t count = load_texture_replacements(job);
    finish(job, count > 0, std::to_string(count) + " PNG replacement(s) active. The renderer picks them up at once.");
}

// --- Audio jobs ----------------------------------------------------------------------

void extract_audio_job() {
    const Job job = Job::ExtractAudio;
    progress(job, 0, 100, "Scanning sound banks", g_assets.generic_string());
    const AudioScan scan = scan_audio(g_assets, [&](size_t done, size_t total) {
        progress(job, total ? static_cast<uint32_t>(done * 100 / total) : 0, 100, "Scanning sound banks",
                 std::to_string(done) + " / " + std::to_string(total) + " files");
    });
    if (scan.rows.empty()) {
        finish(job, false, "No sounds found. Check that the game files are in the assets folder.");
        return;
    }
    const fs::path dir = audio_dir();
    std::error_code ec;
    fs::create_directories(dir, ec);
    std::map<std::string, std::vector<const SoundRow*>> by_source;
    for (const SoundRow& row : scan.rows) by_source[row.source].push_back(&row);
    const uint32_t total = static_cast<uint32_t>(scan.rows.size());
    uint32_t done = 0, written = 0, failed = 0;
    for (const auto& [source, rows] : by_source) {
        if (stopped()) break;
        std::vector<uint8_t> bytes;
        std::string error;
        if (!read_bytes(g_assets / source, bytes, error)) {
            failed += static_cast<uint32_t>(rows.size());
            done += static_cast<uint32_t>(rows.size());
            continue;
        }
        for (const SoundRow* row : rows) {
            if (stopped()) break;
            ++done;
            progress(job, done, total, "Writing WAV files", row->file);
            if (row->data_offset + row->data_size > bytes.size()) { ++failed; continue; }
            const fs::path out_file = dir / row->file;
            fs::create_directories(out_file.parent_path(), ec);
            if (row->kind == 0) {
                const std::vector<uint8_t> wav(bytes.begin() + static_cast<ptrdiff_t>(row->data_offset),
                                               bytes.begin() + static_cast<ptrdiff_t>(row->data_offset + row->data_size));
                if (write_bytes(out_file, wav, error)) ++written;
                else ++failed;
                continue;
            }
            Pcm pcm;
            if (!decode_samples(bytes.data() + row->data_offset, row->data_size, row->kind, row->channels, row->rate,
                                row->frames, pcm, error) ||
                !write_wav(out_file.generic_string(), pcm, error)) {
                LOG_WARN("modding", "cannot extract %s: %s", row->name.c_str(), error.c_str());
                ++failed;
                continue;
            }
            ++written;
        }
    }
    std::string error;
    if (!write_audio_manifest(dir / "audio.csv", scan.rows, error)) LOG_WARN("modding", "audio.csv: %s", error.c_str());
    write_readme(dir / "README.txt", readme_audio());
    write_readme(overrides_dir() / "README.txt",
                 "Game-file overrides written by the Modding window's Audio -> Replace.\n"
                 "Each file mirrors the same path under assets/. Delete a file here (and\n"
                 "restart the game) to hear the original sound again.\n");
    finish(job, written > 0, "Wrote " + std::to_string(written) + " WAV files (" + std::to_string(failed) +
                                " skipped) to " + dir.generic_string());
}

fs::path replacement_wav(const SoundRow& row) {
    std::error_code ec;
    const fs::path first = audio_dir() / row.file;
    if (fs::is_regular_file(first, ec)) return first;
    const fs::path by_hash = audio_dir() / (row.hash + ".wav");
    if (fs::is_regular_file(by_hash, ec)) return by_hash;
    const fs::path by_short_hash = audio_dir() / (row.hash.substr(0, 16) + ".wav");
    if (fs::is_regular_file(by_short_hash, ec)) return by_short_hash;
    return {};
}

void replace_audio_job() {
    const Job job = Job::ReplaceAudio;
    std::vector<SoundRow> rows;
    std::string error;
    if (!read_audio_manifest(audio_dir() / "audio.csv", rows, error) || rows.empty()) {
        finish(job, false, error.empty() ? "No sounds in audio.csv. Press Extract first." : error);
        return;
    }
    std::map<std::string, std::vector<const SoundRow*>> by_source;
    for (const SoundRow& row : rows) by_source[row.source].push_back(&row);
    const uint32_t total = static_cast<uint32_t>(rows.size());
    uint32_t done = 0, replaced = 0, failed = 0;
    for (const auto& [source, group] : by_source) {
        if (stopped()) break;
        std::vector<uint8_t> patched;
        if (!read_bytes(g_assets / source, patched, error)) {
            failed += static_cast<uint32_t>(group.size());
            done += static_cast<uint32_t>(group.size());
            continue;
        }
        size_t changed = 0;
        for (const SoundRow* row : group) {
            ++done;
            progress(job, done, total, "Replacing sounds", row->name);
            const fs::path wav = replacement_wav(*row);
            if (wav.empty() || row->data_offset + row->data_size > patched.size()) continue;
            std::vector<uint8_t> bytes;
            if (row->kind == 0) {
                if (!read_bytes(wav, bytes, error)) { ++failed; continue; }
            } else {
                Pcm pcm;
                if (!read_wav(wav.generic_string(), pcm, error)) {
                    LOG_WARN("modding", "cannot read %s: %s", wav.generic_string().c_str(), error.c_str());
                    ++failed;
                    continue;
                }
                bytes = encode_samples(pcm, row->kind, row->channels, row->rate, row->data_size, error);
                if (bytes.size() != row->data_size) {
                    LOG_WARN("modding", "cannot encode %s: %s", wav.generic_string().c_str(), error.c_str());
                    ++failed;
                    continue;
                }
            }
            bytes.resize(row->data_size, 0);  // keep the bank's layout: pad/trim
            std::copy(bytes.begin(), bytes.end(), patched.begin() + static_cast<ptrdiff_t>(row->data_offset));
            ++changed;
            ++replaced;
        }
        if (changed) {
            const fs::path target = overrides_dir() / source;
            if (!write_bytes(target, patched, error)) {
                LOG_WARN("modding", "cannot write %s: %s", target.generic_string().c_str(), error.c_str());
                failed += static_cast<uint32_t>(changed);
                replaced -= static_cast<uint32_t>(changed);
            }
        }
    }
    if (!replaced) {
        finish(job, false, "No edited WAVs found. Press Extract, edit a WAV in mods/audio, then press Replace.");
        return;
    }
    finish(job, true, std::to_string(replaced) + " sound(s) replaced in mods/overrides. Restart the game (or reload "
                                                 "the level) to hear them.");
}

// --- Animations jobs -----------------------------------------------------------------

// All three tabs share one worker thread and one progress pair. The animations
// scan only reads pack headers, then extraction streams every asset payload.
void extract_animations_job() {
    const Job job = Job::ExtractAnimations;
    progress(job, 0, 0, "Scanning models and animations", g_assets.generic_string());
    const std::vector<AnimationAsset> rows = scan_animation_assets(g_assets, [&](size_t done, size_t total,
                                                                                 const std::string& current) {
        progress(job, static_cast<uint32_t>(done), static_cast<uint32_t>(total), "Scanning models and animations",
                 current);
    });
    if (rows.empty()) {
        finish(job, false, "No models or animations found. Check that the game files are in the assets folder.");
        return;
    }
    const fs::path dir = animations_dir();
    const AnimationJobResult result = export_animation_assets(
        g_assets, dir, rows, [&](size_t done, size_t total, const std::string& current) {
            progress(job, static_cast<uint32_t>(done), static_cast<uint32_t>(total), "Exporting hands, weapons and animations",
                     current);
        });
    write_readme(dir / "README.txt", readme_animations());
    write_readme(overrides_dir() / "README.txt",
                 "Game-file overrides written by the Modding window's Audio or Animations -> Replace.\n"
                 "Each file mirrors the same path under assets/. Delete a file here (and\n"
                 "restart the game) to hear/see the original asset again.\n");
    finish(job, result.ok, result.message);
}

void replace_animations_job() {
    const Job job = Job::ReplaceAnimations;
    progress(job, 0, 0, "Rebuilding game-file overrides", animations_dir().generic_string());
    const AnimationJobResult result = replace_animation_assets(
        g_assets, animations_dir(), overrides_dir(),
        [&](size_t done, size_t total, const std::string& current) {
            progress(job, static_cast<uint32_t>(done), static_cast<uint32_t>(total), "Replacing models and animations",
                     current);
        });
    finish(job, result.ok, result.message);
}

void run_job(Job job) {
    try {
        switch (job) {
            case Job::ExtractTextures: extract_textures_job(); break;
            case Job::ReplaceTextures: replace_textures_job(); break;
            case Job::ExtractAudio: extract_audio_job(); break;
            case Job::ReplaceAudio: replace_audio_job(); break;
            case Job::ExtractAnimations: extract_animations_job(); break;
            case Job::ReplaceAnimations: replace_animations_job(); break;
            default: break;
        }
    } catch (const std::exception& e) {
        LOG_ERROR("modding", "job failed: %s", e.what());
        finish(job, false, std::string("Job failed: ") + e.what());
    } catch (...) {
        finish(job, false, "Job failed with an unknown error.");
    }
}

void start_job(Job job) {
    int expected = -1;
    if (!g_active.compare_exchange_strong(expected, static_cast<int>(job))) return;
    g_stop.store(false);
    JobState& s = state(job);
    s.running.store(true);
    s.done.store(0);
    s.total.store(0);
    {
        std::lock_guard lock(s.mutex);
        s.finished = false;
        s.ok = false;
        s.status = "Starting";
        s.current.clear();
        s.message.clear();
    }
    if (g_worker.joinable()) g_worker.join();
    g_worker = std::thread(run_job, job);
}

void draw_progress(Job job, const char* heading) {
    JobState& s = state(job);
    const uint32_t done = s.done.load(), total = s.total.load();
    const float fraction = total ? std::min(1.0f, float(done) / float(total)) : 0.0f;
    std::string status, current, message;
    bool finished = false, ok = false;
    {
        std::lock_guard lock(s.mutex);
        status = s.status;
        current = s.current;
        message = s.message;
        finished = s.finished;
        ok = s.ok;
    }
    ImGui::SeparatorText(heading);
    const std::string label = status + "  " + std::to_string(done) + "/" + std::to_string(total);
    ImGui::ProgressBar(fraction, ImVec2(-1.0f, 0.0f), label.c_str());
    if (!current.empty()) ImGui::TextWrapped("%s", current.c_str());
    if (finished) {
        const ImVec4 colour = ok ? ImVec4(0.45f, 0.9f, 0.5f, 1.0f) : ImVec4(1.0f, 0.55f, 0.45f, 1.0f);
        ImGui::TextColored(colour, "%s", message.c_str());
    }
}

void draw_tab(const char* description, Job extract, Job replace, const fs::path& dir) {
    ImGui::TextWrapped("%s", description);
    ImGui::TextDisabled("Folder: %s", dir.generic_string().c_str());
    const bool busy = g_active.load() != -1;
    ImGui::BeginDisabled(busy);
    if (ImGui::Button("Extract", ImVec2(120, 0))) start_job(extract);
    ImGui::SameLine();
    if (ImGui::Button("Replace", ImVec2(120, 0))) start_job(replace);
    ImGui::EndDisabled();
    if (busy) {
        ImGui::SameLine();
        ImGui::TextDisabled("working...");
    }
    draw_progress(extract, "Extraction progress");
    draw_progress(replace, "Replacement progress");
}

}  // namespace

void configure(const fs::path& assets) {
    g_assets = assets;
    g_mods_root = platform::user_data_dir() / "mods";
    std::error_code ec;
    for (const fs::path& dir : {root(), textures_dir(), audio_dir(), animations_dir(), overrides_dir()})
        fs::create_directories(dir, ec);
    write_readme(root() / "README.txt",
                 "CoD2: BRO mods\n"
                 "==============\n\n"
                 "textures/    extracted PNG textures; press Replace in F4 -> Textures to use them\n"
                 "audio/       extracted WAV sounds; press Replace in F4 -> Audio to build overrides\n"
                 "animations/  all player view-hands, weapon models and animations; F4 -> Animations\n"
                 "overrides/   patched copies of game files (the originals in assets/ stay untouched)\n\n"
                 "See README.txt in each folder. Nothing here is required to play.\n");
    // Existing mods apply on the next start without pressing a button.
    load_texture_replacements(Job::ReplaceTextures);
}

fs::path mods_root() { return root(); }
fs::path textures_dir() { return root() / "textures"; }
fs::path audio_dir() { return root() / "audio"; }
fs::path animations_dir() { return root() / "animations"; }
fs::path overrides_dir() { return root() / "overrides"; }

bool menu() {
    if (!g_open) return false;
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(620, 460), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowBgAlpha(0.92f);
    bool open = true;
    if (!ImGui::Begin("Modding", &open, ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        g_open = false;
        return false;
    }
    ImGui::TextDisabled("F4 opens and closes this window. The game keeps running.");
    ImGui::TextWrapped("Extract the game's assets, edit them, then Replace. Textures are PNG and apply live; "
                       "sounds are WAV; models and animations stay in the game's original Xbox formats and "
                       "become game-file overrides.");
    if (ImGui::BeginTabBar("modding_tabs")) {
        if (ImGui::BeginTabItem("Textures")) {
            draw_tab("Extract every DDS texture from the .cod packs as PNG. Edit a PNG and press Replace to draw it "
                     "in place of the original texture.",
                     Job::ExtractTextures, Job::ReplaceTextures, textures_dir());
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Audio")) {
            draw_tab("Extract the game's wave-bank sounds as 16-bit PCM WAV. Edit a WAV and press Replace to convert it "
                     "back and write a patched game-file override.",
                     Job::ExtractAudio, Job::ReplaceAudio, audio_dir());
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Animations")) {
            draw_tab("Export all player view-hands, every weapon model and every animation (.xbmesh, .xmb, "
                     ".xbanim, .xbskel) into mods/animations/, grouped as viewhands/ and weapons/<weapon>/. "
                     "Replace patches edited same-length files into mods/overrides/.",
                     Job::ExtractAnimations, Job::ReplaceAnimations, animations_dir());
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::Separator();
    ImGui::TextDisabled("Game files: %s", g_assets.generic_string().c_str());
    ImGui::End();
    g_open = open;
    return open;
}

bool is_open() { return g_open; }

void shown(bool open) { g_open = open; }

std::shared_ptr<const render::TextureReplacement> texture_replacement(uint64_t content, float /*render_scale*/) {
    std::lock_guard lock(g_replacements_mutex);
    const auto it = g_replacements.find(content);
    return it == g_replacements.end() ? nullptr : it->second;
}

uint32_t texture_version() { return g_version.load(); }

void shutdown() {
    g_stop.store(true);
    if (g_worker.joinable()) g_worker.join();
}

}  // namespace modding
