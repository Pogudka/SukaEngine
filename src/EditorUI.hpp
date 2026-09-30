#pragma once

#include <string>
#include <vector>
#include <iostream>

#include <dirent.h>
#include <sys/stat.h>

#include "Editor.hpp"

namespace suka {

inline void listTree(const std::string& path, const std::string& prefix, int depth, std::vector<std::string>& out) {
    if (depth > 2) return;
    DIR* d = opendir(path.c_str());
    if (!d) {
        out.push_back(prefix + "(empty)");
        return;
    }
    struct dirent* e;
    while ((e = readdir(d))) {
        std::string n = e->d_name;
        if (n == "." || n == "..") continue;
        std::string full = path + "/" + n;
        struct stat st;
        if (stat(full.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) {
            out.push_back(prefix + n + "/");
            listTree(full, prefix + "  ", depth + 1, out);
        } else {
            out.push_back(prefix + n);
        }
    }
    closedir(d);
}

inline std::string padCut(const std::string& s, int w) {
    if ((int)s.size() >= w) return s.substr(0, w);
    return s + std::string(w - (int)s.size(), ' ');
}

inline std::string lineAt(const std::vector<std::string>& v, int i) {
    if (i < 0 || i >= (int)v.size()) return "";
    return v[i];
}

class EditorUI {
public:
    void render(Editor& editor, const std::string& projectRoot) {
        const int LW = 24, MW = 36, RW = 26;
        const int FULL = LW + MW + RW + 2;
        const int H = 20;

        auto left = editor.hierarchyLines();
        auto mid = editor.sceneViewLines();
        auto right = editor.inspectorLines();

        std::vector<std::string> files;
        listTree(projectRoot, "", 0, files);

        auto sep3 = [&]() {
            std::cout << "+" << std::string(LW, '-') << "+"
                      << std::string(MW, '-') << "+"
                      << std::string(RW, '-') << "+\n";
        };
        auto sepFull = [&]() {
            std::cout << "+" << std::string(FULL, '-') << "+\n";
        };
        auto row3 = [&](const std::string& a, const std::string& b, const std::string& c) {
            std::cout << "|" << padCut(a, LW) << "|"
                      << padCut(b, MW) << "|"
                      << padCut(c, RW) << "|\n";
        };
        auto rowFull = [&](const std::string& a) {
            std::cout << "|" << padCut(a, FULL) << "|\n";
        };

        sep3();
        rowFull(" CREATE: Shape Sprite Label Player Enemy Coin Solid Camera");
        rowFull(" SHAPES: square circle diamond triangle | COLOR: #RRGGBB");
        sep3();
        row3(" HIERARCHY", " SCENE", " INSPECTOR");
        sep3();
        for (int i = 0; i < H; ++i) {
            row3(" " + lineAt(left, i), " " + lineAt(mid, i), " " + lineAt(right, i));
        }
        sep3();
        rowFull(" PROJECT");
        sepFull();
        int show = (int)files.size();
        if (show > 8) show = 8;
        for (int i = 0; i < show; ++i) {
            rowFull(" " + files[i]);
        }
        if ((int)files.size() > 8) rowFull(" ...");
        sepFull();
    }
};

} // namespace suka