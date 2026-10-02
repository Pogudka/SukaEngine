#pragma once

#include <string>
#include <vector>
#include <fstream>
#include <cstdio>
#include <cctype>
#include <cstdlib>

#include <dirent.h>
#include <sys/stat.h>

#include "Core.hpp"
#include "Project.hpp"
#include "UiUtils.hpp"

namespace suka {

inline std::string projectsDir() {
    return std::string(PROJECT_ROOT) + "/projects/";
}

inline bool removePathRecursive(const std::string& path) {
    struct stat st;

    if (stat(path.c_str(), &st) != 0) return false;

    if (!S_ISDIR(st.st_mode)) {
        return std::remove(path.c_str()) == 0;
    }

    DIR* dir = opendir(path.c_str());
    if (!dir) return false;

    bool ok = true;
    struct dirent* ent;

    while ((ent = readdir(dir)) != nullptr) {
        std::string name = ent->d_name;

        if (name == "." || name == "..") continue;

        std::string child = path + "/" + name;

        if (!removePathRecursive(child)) ok = false;
    }

    closedir(dir);

    if (rmdir(path.c_str()) != 0) ok = false;

    return ok;
}

inline bool setProjectDisplayName(const std::string& root, const std::string& newName) {
    std::string path = root + "/project.json";
    std::string s = readFile(path);

    if (s.empty()) return false;

    std::string clean = safeProjectDisplayName(newName);
    std::string esc = escapeJsonString(clean);

    size_t key = s.find("\"name\"");

    if (key != std::string::npos) {
        size_t colon = s.find(':', key + 6);
        if (colon == std::string::npos) return false;

        size_t q1 = s.find('"', colon + 1);
        if (q1 == std::string::npos) return false;

        size_t q2 = s.find('"', q1 + 1);
        if (q2 == std::string::npos) return false;

        s.replace(q1 + 1, q2 - q1 - 1, esc);
    } else {
        size_t last = s.rfind('}');
        if (last == std::string::npos) return false;

        size_t ins = last;

        while (ins > 0 && std::isspace((unsigned char)s[ins - 1])) --ins;

        if (ins > 0 && s[ins - 1] != '{') s.insert(ins, ",\n  ");
        else s.insert(ins, "\n  ");

        s.insert(ins, "\"name\": \"" + esc + "\"");
    }

    std::ofstream f(path);

    if (!f.good()) return false;

    f << s;
    f.close();

    return true;
}

inline bool projectWantsSave(const std::string& root) {
    std::string s = readFile(root + "/project.json");

    size_t p = s.find("\"save_vars\"");
    if (p == std::string::npos) return false;

    p = s.find(':', p + 11);
    if (p == std::string::npos) return false;

    ++p;

    while (p < s.size() && std::isspace((unsigned char)s[p])) ++p;
    if (p >= s.size()) return false;

    char c = s[p];

    if (c == 't' || c == 'T' || c == '1') return true;
    if (c == 'f' || c == 'F' || c == '0') return false;

    if (c == '"') {
        size_t e = s.find('"', p + 1);
        if (e == std::string::npos) return false;

        std::string v = s.substr(p + 1, e - p - 1);

        return v == "1" || v == "true" || v == "on" || v == "yes";
    }

    return false;
}

inline bool setProjectSaveFlag(const std::string& root, bool on) {
    std::string path = root + "/project.json";
    std::string s = readFile(path);

    if (s.empty()) return false;

    std::string val = on ? "true" : "false";
    size_t key = s.find("\"save_vars\"");

    if (key != std::string::npos) {
        size_t colon = s.find(':', key + 11);
        if (colon == std::string::npos) return false;

        size_t st = colon + 1;

        while (st < s.size() && std::isspace((unsigned char)s[st])) ++st;

        size_t en = st;

        if (en < s.size() && s[en] == '"') {
            en = s.find('"', en + 1);
            if (en == std::string::npos) return false;

            ++en;
        } else {
            while (en < s.size() &&
                   (std::isalnum((unsigned char)s[en]) ||
                    s[en] == '_' ||
                    s[en] == '.' ||
                    s[en] == '+' ||
                    s[en] == '-')) {
                ++en;
            }
        }

        if (st == en) return false;

        s.replace(st, en - st, val);
    } else {
        size_t last = s.rfind('}');
        if (last == std::string::npos) return false;

        size_t ins = last;

        while (ins > 0 && std::isspace((unsigned char)s[ins - 1])) --ins;

        if (ins > 0 && s[ins - 1] != '{') s.insert(ins, ",\n  ");
        else s.insert(ins, "\n  ");

        s.insert(ins, "\"save_vars\": " + val);
    }

    std::ofstream f(path);

    if (!f.good()) return false;

    f << s;
    f.close();

    return true;
}

inline void saveVarsToFile(const std::string& root, const Context& ctx) {
    if (root.empty()) return;

    std::ofstream f(root + "/save.vars");

    if (!f.good()) return;

    f << "__score__=" << ctx.score << "\n";

    for (const auto& kv : ctx.vars) {
        f << kv.first << "=" << kv.second << "\n";
    }

    f.close();
}

inline void loadVarsFromFile(const std::string& root, Context& ctx) {
    if (root.empty()) return;

    std::string path = root + "/save.vars";

    if (!fileExists(path)) return;

    std::string s = readFile(path);
    size_t pos = 0;

    while (pos <= s.size()) {
        size_t nl = s.find('\n', pos);
        std::string line;

        if (nl == std::string::npos) {
            line = s.substr(pos);
            pos = s.size() + 1;
        } else {
            line = s.substr(pos, nl - pos);
            pos = nl + 1;
        }

        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;

        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;

        std::string k = line.substr(0, eq);
        double v = atof(line.substr(eq + 1).c_str());

        if (k == "__score__") ctx.score = (int)v;
        else if (!k.empty()) ctx.vars[k] = v;
    }
}

inline std::string uniqueProjectDir(const std::string& base, const std::vector<ProjectInfo>& games) {
    std::string b = base.empty() ? std::string("Project") : base;
    std::string dir = b;
    int suffix = 2;

    while (suffix < 1000) {
        bool exists = false;

        for (const auto& g : games) {
            if (g.dir == dir) {
                exists = true;
                break;
            }
        }

        if (!exists && !fileExists(projectsDir() + dir)) break;

        dir = b + "_" + std::to_string(suffix++);
    }

    return dir;
}

inline std::string projectDisplayName(const std::string& dir) {
    std::string disp = dir;

    ProjectInfo info;

    if (ProjectLoader::load(projectsDir() + dir + "/project.json", info) && !info.name.empty()) {
        disp = info.name;
    }

    return disp;
}

} // namespace suka
