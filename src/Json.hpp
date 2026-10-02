#pragma once

#include <string>
#include <fstream>
#include <sstream>
#include <vector>
#include <sys/stat.h>

namespace suka {

inline bool fileExists(const std::string& path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0;
}

inline std::string readFile(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) return "";
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

inline bool writeFile(const std::string& path, const std::string& content) {
    std::ofstream f(path);
    if (!f.is_open()) return false;
    f << content;
    return true;
}

inline std::string trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

inline bool jsonFindArray(const std::string& json, const std::string& key, size_t& begin, size_t& end) {
    std::string search = "\"" + key + "\"";
    size_t pos = json.find(search);
    if (pos == std::string::npos) return false;
    pos = json.find('[', pos);
    if (pos == std::string::npos) return false;
    begin = pos;
    int depth = 1;
    pos++;
    while (pos < json.size() && depth > 0) {
        if (json[pos] == '[') depth++;
        else if (json[pos] == ']') depth--;
        pos++;
    }
    end = pos - 1;
    return depth == 0;
}

inline std::vector<std::string> jsonSplitObjects(const std::string& arr) {
    std::vector<std::string> objects;
    int depth = 0;
    size_t start = 0;
    bool inString = false;
    
    for (size_t i = 0; i < arr.size(); ++i) {
        char c = arr[i];
        if (c == '"' && (i == 0 || arr[i-1] != '\\')) inString = !inString;
        if (inString) continue;
        
        if (c == '{') {
            if (depth == 0) start = i;
            depth++;
        } else if (c == '}') {
            depth--;
            if (depth == 0) {
                objects.push_back(arr.substr(start, i - start + 1));
            }
        }
    }
    return objects;
}

inline bool jsonGetString(const std::string& obj, const std::string& key, std::string& value) {
    std::string search = "\"" + key + "\"";
    size_t pos = obj.find(search);
    if (pos == std::string::npos) return false;
    pos = obj.find(':', pos);
    if (pos == std::string::npos) return false;
    pos = obj.find('"', pos);
    if (pos == std::string::npos) return false;
    size_t start = pos + 1;
    pos = obj.find('"', start);
    if (pos == std::string::npos) return false;
    value = obj.substr(start, pos - start);
    return true;
}

inline bool jsonGetNumber(const std::string& obj, const std::string& key, float& value) {
    std::string search = "\"" + key + "\"";
    size_t pos = obj.find(search);
    if (pos == std::string::npos) return false;
    pos = obj.find(':', pos);
    if (pos == std::string::npos) return false;
    pos++;
    while (pos < obj.size() && (obj[pos] == ' ' || obj[pos] == '\t')) pos++;
    size_t start = pos;
    while (pos < obj.size() && (isdigit(obj[pos]) || obj[pos] == '.' || obj[pos] == '-' || obj[pos] == '+')) pos++;
    if (pos == start) return false;
    std::string numStr = obj.substr(start, pos - start);
    value = std::stof(numStr);
    return true;
}

// JSON-FIX: добавлена функция для чтения boolean
inline bool jsonGetBool(const std::string& obj, const std::string& key, bool& value) {
    std::string search = "\"" + key + "\"";
    size_t pos = obj.find(search);
    if (pos == std::string::npos) return false;
    pos = obj.find(':', pos);
    if (pos == std::string::npos) return false;
    pos++;
    while (pos < obj.size() && (obj[pos] == ' ' || obj[pos] == '\t')) pos++;
    
    if (obj.substr(pos, 4) == "true") {
        value = true;
        return true;
    } else if (obj.substr(pos, 5) == "false") {
        value = false;
        return true;
    }
    return false;
}

} // namespace suka
