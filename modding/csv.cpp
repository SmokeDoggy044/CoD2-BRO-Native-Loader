// See csv.h.

#include "csv.h"

#include <cstdio>

namespace modding {

std::string csv_field(const std::string& text) {
    const bool quote = text.find_first_of(",\"\r\n") != std::string::npos;
    if (!quote) return text;
    std::string out = "\"";
    for (char c : text) {
        if (c == '"') out += "\"\"";
        else out += c;
    }
    out += '"';
    return out;
}

std::string csv_line(const std::vector<std::string>& fields) {
    std::string out;
    for (size_t i = 0; i < fields.size(); ++i) {
        if (i) out += ',';
        out += csv_field(fields[i]);
    }
    out += '\n';
    return out;
}

std::vector<std::string> csv_split(const std::string& line) {
    std::vector<std::string> fields;
    std::string field;
    bool quoted = false;
    for (size_t i = 0; i < line.size(); ++i) {
        const char c = line[i];
        if (quoted) {
            if (c == '"') {
                if (i + 1 < line.size() && line[i + 1] == '"') { field += '"'; ++i; }
                else quoted = false;
            } else {
                field += c;
            }
        } else if (c == '"') {
            quoted = true;
        } else if (c == ',') {
            fields.push_back(field);
            field.clear();
        } else if (c != '\r' && c != '\n') {
            field += c;
        }
    }
    fields.push_back(field);
    return fields;
}

std::string hex64(uint64_t value) {
    char buffer[17];
    std::snprintf(buffer, sizeof(buffer), "%016llx", static_cast<unsigned long long>(value));
    return std::string(buffer);
}

uint64_t parse_hex64(const std::string& text) {
    uint64_t value = 0;
    size_t digits = 0;
    for (char c : text) {
        int digit = -1;
        if (c >= '0' && c <= '9') digit = c - '0';
        else if (c >= 'a' && c <= 'f') digit = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') digit = c - 'A' + 10;
        else break;
        value = (value << 4) | static_cast<uint64_t>(digit);
        ++digits;
    }
    return digits ? value : 0;
}

}  // namespace modding
