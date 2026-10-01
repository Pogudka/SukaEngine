#pragma once

#include <string>
#include <vector>
#include <fstream>
#include <cctype>
#include <algorithm>

#include <dirent.h>
#include <sys/stat.h>

#include "Core.hpp"
#include "Json.hpp"

namespace suka {

struct ProjectInfo {
    std::string name = "Untitled Game";
    std::string engine = ENGINE_NAME;
    std::string packageName = "com.sukaengine.game";
    std::string version = "0.0.1";
    std::string mainScene = "scenes/main.json";
    std::string orientation = "landscape";
    std::string defaultFont = "assets/fonts/Ubuntu-Regular.ttf";
    std::string rootPath;
    bool loaded = false;
};

class ProjectLoader {
public:
    static bool load(const std::string& projectJsonPath, ProjectInfo& info) {
        if (!fileExists(projectJsonPath)) return false;
        std::string json = readFile(projectJsonPath);
        if (json.empty()) return false;
        jsonGetString(json, "name", info.name);
        jsonGetString(json, "engine", info.engine);
        jsonGetString(json, "package", info.packageName);
        jsonGetString(json, "version", info.version);
        jsonGetString(json, "main_scene", info.mainScene);
        jsonGetString(json, "orientation", info.orientation);
        jsonGetString(json, "default_font", info.defaultFont);
        size_t slash = projectJsonPath.find_last_of('/');
        if (slash != std::string::npos) info.rootPath = projectJsonPath.substr(0, slash);
        info.loaded = true;
        return true;
    }
};

struct ProjectEntry { std::string dir; std::string name; };

class ProjectList {
public:
    static std::vector<ProjectEntry> scan() {
        std::vector<ProjectEntry> out;
        std::string base = PROJECT_ROOT + "/projects";
        DIR* d = opendir(base.c_str());
        if (!d) return out;
        struct dirent* e;
        while ((e = readdir(d))) {
            std::string n = e->d_name;
            if (n == "." || n == "..") continue;
            std::string full = base + "/" + n;
            struct stat st;
            if (stat(full.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) {
                ProjectInfo info;
                if (ProjectLoader::load(full + "/project.json", info)) {
                    ProjectEntry entry; entry.dir = n; entry.name = info.name;
                    out.push_back(entry);
                }
            }
        }
        closedir(d);
        return out;
    }
};

// ---- файловый браузер (тот же POSIX-API, что ProjectList::scan) ----
struct FileEntry { std::string name; bool isDir = false; };

class FileBrowser {
public:
    static std::vector<FileEntry> list(const std::string& absPath) {
        std::vector<FileEntry> out;
        DIR* d = opendir(absPath.c_str());
        if (!d) return out;
        struct dirent* e;
        while ((e = readdir(d))) {
            std::string n = e->d_name;
            if (n == "." || n == "..") continue;
            std::string full = absPath + "/" + n;
            struct stat st;
            FileEntry fe; fe.name = n;
            fe.isDir = (stat(full.c_str(), &st) == 0 && S_ISDIR(st.st_mode));
            out.push_back(fe);
        }
        closedir(d);
        std::sort(out.begin(), out.end(), [](const FileEntry& a, const FileEntry& b) {
            if (a.isDir != b.isDir) return a.isDir > b.isDir;   // папки первыми
            return a.name < b.name;
        });
        return out;
    }
};

class ProjectCreator {
public:
    // ПУСТОЙ проект: голая сцена, без игрока/физики/кнопок/скриптов.
    // Человек сам наполняет через редактор (+ / TXT / SCR / drag).
    static bool createProject(const std::string& dir, const std::string& displayName) {
        std::string base = PROJECT_ROOT + "/projects/" + dir;
        makeDirs(base + "/scenes");
        makeDirs(base + "/assets");
        makeDirs(base + "/scripts");

        writeFile(base + "/project.json",
            "{\n  \"name\": \"" + displayName + "\",\n  \"engine\": \"SukaEngine\",\n"
            "  \"package\": \"com.sukaengine." + dir + "\",\n  \"version\": \"0.1.0\",\n"
            "  \"main_scene\": \"scenes/main.json\",\n  \"orientation\": \"landscape\",\n"
            "  \"default_font\": \"assets/fonts/Ubuntu-Regular.ttf\"\n}\n");

        writeFile(base + "/scenes/main.json",
            "{\n  \"name\": \"Main\",\n  \"gravity\": 0,\n  \"nodes\": [],\n  \"ui\": []\n}\n");

        writeFile(base + "/scripts.json", "{\n  \"scripts\": []\n}\n");

        std::cout << "[ProjectCreator] created EMPTY project: " << dir
                  << " (" << displayName << ")\n";
        return true;
    }

    static bool createScript(const std::string& projectRoot, const std::string& relPath, const std::string& nodeName) {
        std::string full = projectRoot + "/" + relPath;
        if (!writeFile(full,
            "-- script for node " + nodeName + "\n"
            "function on_start()\n  print(\"" + nodeName + " started\")\nend\n\n"
            "function on_update(dt)\n  -- your logic here\nend\n")) {
            return false;
        }
        std::string sj = projectRoot + "/scripts.json";
        std::string json = fileExists(sj) ? readFile(sj) : std::string("");
        std::string entry = "    { \"node\": \"" + nodeName + "\", \"path\": \"" + relPath + "\" }";
        if (json.empty()) {
            json = "{\n  \"scripts\": [\n" + entry + "\n  ]\n}\n";
        } else {
            size_t br = json.rfind(']');
            size_t lb = json.rfind('[', br);
            if (br == std::string::npos || lb == std::string::npos) return false;
            bool emptyArr = true;
            for (size_t i = lb + 1; i < br; ++i) {
                if (!std::isspace((unsigned char)json[i])) { emptyArr = false; break; }
            }
            std::string ins = emptyArr ? ("\n" + entry + "\n") : (",\n" + entry);
            json.insert(br, ins);
        }
        writeFile(sj, json);
        std::cout << "[ProjectCreator] created script: " << relPath
                  << " for node " << nodeName << "\n";
        return true;
    }

private:
    static void makeDirs(const std::string& path) {
        std::string cur;
        for (size_t i = 0; i < path.size(); ++i) {
            cur += path[i];
            if (path[i] == '/' || i + 1 == path.size()) {
                if (!cur.empty() && cur != "/") mkdir(cur.c_str(), 0777);
            }
        }
    }
    static bool writeFile(const std::string& path, const std::string& content) {
        std::ofstream f(path);
        if (!f.good()) return false;
        f << content;
        return true;
    }
};

} // namespace suka
