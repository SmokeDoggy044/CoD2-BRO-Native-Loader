#include "update_check.h"

#include <cctype>
#include <thread>

#include "https.h"
#include "log.h"

namespace launcher {

std::optional<std::array<int, 3>> parse_version(const std::string& text) {
    std::array<int, 3> version{};
    size_t i = !text.empty() && (text[0] == 'v' || text[0] == 'V') ? 1 : 0;
    for (size_t part = 0; part < 3; ++part) {
        if (i >= text.size() || !std::isdigit(static_cast<unsigned char>(text[i]))) return {};
        int value = 0;
        while (i < text.size() && std::isdigit(static_cast<unsigned char>(text[i])) && value < 100000)
            value = value * 10 + (text[i++] - '0');
        version[part] = value;
        if (part < 2 && (i >= text.size() || text[i++] != '.')) return {};
    }
    if (i != text.size()) return {};
    return version;
}

std::optional<std::string> newest_tag(const std::string& releases_json) {
    const size_t key = releases_json.find("\"tag_name\"");
    if (key == std::string::npos) return {};
    size_t i = releases_json.find_first_not_of(" \t\r\n", key + 10);
    if (i == std::string::npos || releases_json[i] != ':') return {};
    i = releases_json.find_first_not_of(" \t\r\n", i + 1);
    if (i == std::string::npos || releases_json[i] != '"') return {};
    const size_t end = releases_json.find('"', i + 1);
    if (end == std::string::npos) return {};
    return releases_json.substr(i + 1, end - i - 1);
}

UpdateCheck::UpdateCheck(std::string repository, std::string current_version) {
    std::thread([shared = shared_, repository = std::move(repository), current = std::move(current_version)] {
        Result result;
        result.status = Status::Failed;
        const std::optional<std::string> body =
            platform::https_get("https://api.github.com/repos/" + repository + "/releases?per_page=1",
                                "cod_bro_launcher/" + current);
        const std::optional<std::string> tag = body ? newest_tag(*body) : std::nullopt;
        const auto newest = tag ? parse_version(*tag) : std::nullopt;
        const auto have = parse_version(current);
        if (newest && have) {
            result.status = *newest > *have ? Status::Available : Status::UpToDate;
            result.version = std::to_string((*newest)[0]) + "." + std::to_string((*newest)[1]) + "." +
                             std::to_string((*newest)[2]);
            result.url = "https://github.com/" + repository + "/releases/tag/" + *tag;
            LOG_INFO("launcher", "update check: newest release %s, this is %s", tag->c_str(), current.c_str());
        } else {
            LOG_INFO("launcher", "update check failed (%s)", !body ? "no answer" : "unexpected answer");
        }
        std::lock_guard lock(shared->mutex);
        shared->result = result;
    }).detach();
}

UpdateCheck::Result UpdateCheck::result() const {
    std::lock_guard lock(shared_->mutex);
    return shared_->result;
}

}  // namespace launcher
