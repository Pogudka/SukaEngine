#pragma once

#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <cctype>

namespace suka {

inline bool fileExists(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    return file.good();
}

inline std::string readFile(const std::string& path) {
    std::ifstream file(path);
    if (!file.good()) return "";
    std::stringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

inline bool jsonGetString(const std::string& json, const std::string& key, std::string& out) {
    const std::string needle = "\"" + key + "\"";
    size_t pos = json.find(needle);
    if (pos == std::string::npos) return false;

    pos = json.find(':', pos + needle.size());
    if (pos == std::string::npos) return false;

    size_t q1 = json.find('"', pos + 1);
    if (q1 == std::string::npos) return false;

    size_t q2 = q1 + 1;
    while (q2 < json.size() && json[q2] != '"') {
        if (json[q2] == '\\') q2++;
        q2++;
    }
    if (q2 >= json.size()) return false;

    out = json.substr(q1 + 1, q2 - q1 - 1);
    return true;
}

inline bool jsonGetNumber(const std::string& json, const std::string& key, float& out) {
    const std::string needle = "\"" + key + "\"";
    size_t pos = json.find(needle);
    if (pos == std::string::npos) return false;

    size_t p = json.find(':', pos + needle.size());
    if (p == std::string::npos) return false;
    p++;

    while (p < json.size() && std::isspace((unsigned char)json[p])) p++;

    size_t start = p;
    while (p < json.size() &&
           (std::isdigit((unsigned char)json[p]) ||
            json[p] == '-' || json[p] == '+' || json[p] == '.')) {
        p++;
    }

    if (p == start) return false;

    out = std::stof(json.substr(start, p - start));
    return true;
}

inline bool jsonFindArray(const std::string& json, const std::string& key, size_t& begin, size_t& end) {
    const std::string needle = "\"" + key + "\"";
    size_t pos = json.find(needle);
    if (pos == std::string::npos) return false;

    size_t b = json.find('[', pos);
    if (b == std::string::npos) return false;

    int depth = 0;
    for (size_t i = b; i < json.size(); ++i) {
        if (json[i] == '[') depth++;
        else if (json[i] == ']') {
            depth--;
            if (depth == 0) {
                begin = b;
                end = i;
                return true;
            }
        }
    }
    return false;
}

inline std::vector<std::string> jsonSplitObjects(const std::string& arr) {
    std::vector<std::string> objects;
    int depth = 0;
    size_t start = std::string::npos;

    for (size_t i = 0; i < arr.size(); ++i) {
        char c = arr[i];
        if (c == '{') {
            if (depth == 0) start = i;
            depth++;
        } else if (c == '}') {
            depth--;
            if (depth == 0 && start != std::string::npos) {
                objects.push_back(arr.substr(start, i - start + 1));
                start = std::string::npos;
            }
        }
    }
    return objects;
}

} // namespace suka