#pragma once

#include <string>
#include <vector>
#include <fstream>
#include <cctype>

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
    std::string orientation = "portrait";
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
        if (slash != std::string::npos) {
            info.rootPath = projectJsonPath.substr(0, slash);
        }

        info.loaded = true;
        return true;
    }
};

struct ProjectEntry {
    std::string dir;
    std::string name;
};

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
                    ProjectEntry entry;
                    entry.dir = n;
                    entry.name = info.name;
                    out.push_back(entry);
                }
            }
        }
        closedir(d);
        return out;
    }
};

// Создаёт проекты и скрипты прямо из редактора / хаба
class ProjectCreator {
public:
    static bool createProject(const std::string& dir, const std::string& displayName) {
        std::string base = PROJECT_ROOT + "/projects/" + dir;
        makeDirs(base + "/scenes");
        makeDirs(base + "/assets");
        makeDirs(base + "/scripts");

        writeFile(base + "/project.json",
            "{\n  \"name\": \"" + displayName + "\",\n  \"engine\": \"SukaEngine\",\n"
            "  \"package\": \"com.sukaengine." + dir + "\",\n  \"version\": \"0.1.0\",\n"
            "  \"main_scene\": \"scenes/menu.json\",\n  \"orientation\": \"portrait\",\n"
            "  \"default_font\": \"assets/fonts/Ubuntu-Regular.ttf\"\n}\n");

        writeFile(base + "/scenes/menu.json",
            "{\n  \"name\": \"Menu\",\n  \"nodes\": [\n"
            "    { \"type\": \"Label\", \"name\": \"Title\", \"text\": \"" + displayName + "\", \"x\": 200, \"y\": 300, \"font_size\": 48 }\n"
            "  ],\n  \"ui\": [\n"
            "    { \"id\": \"start\", \"x\": 240, \"y\": 700, \"w\": 240, \"h\": 120, \"text\": \"START\", \"action\": \"restart_scene:scenes/main.json\" }\n"
            "  ]\n}\n");

        writeFile(base + "/scenes/main.json",
            "{\n  \"name\": \"Main\",\n  \"nodes\": [\n"
            "    { \"type\": \"Player\", \"name\": \"Player\", \"x\": 200, \"y\": 400, \"speed\": 260 }\n"
            "  ],\n  \"ui\": [\n"
            "    { \"id\": \"pause\", \"x\": 20, \"y\": 20, \"w\": 100, \"h\": 60, \"text\": \"II\", \"action\": \"change_scene:scenes/pause.json\" }\n"
            "  ]\n}\n");

        writeFile(base + "/scenes/pause.json",
            "{\n  \"name\": \"Pause\",\n  \"nodes\": [\n"
            "    { \"type\": \"Label\", \"name\": \"PauseTitle\", \"text\": \"PAUSE\", \"x\": 300, \"y\": 500, \"font_size\": 40 }\n"
            "  ],\n  \"ui\": [\n"
            "    { \"id\": \"resume\", \"x\": 240, \"y\": 700, \"w\": 240, \"h\": 100, \"text\": \"RESUME\", \"action\": \"change_scene:scenes/main.json\" },\n"
            "    { \"id\": \"menu\", \"x\": 240, \"y\": 840, \"w\": 240, \"h\": 100, \"text\": \"MENU\", \"action\": \"change_scene:scenes/menu.json\" }\n"
            "  ]\n}\n");

        writeFile(base + "/scripts.json", "{\n  \"scripts\": []\n}\n");

        writeFile(base + "/scripts/main.lua",
            "-- " + displayName + ": main script\n"
            "function on_start()\n  print(\"hello from " + dir + "\")\nend\n\n"
            "function on_update(dt)\nend\n");

        std::cout << "[ProjectCreator] created project: " << dir
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