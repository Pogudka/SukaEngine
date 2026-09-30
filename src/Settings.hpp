#pragma once

#include <fstream>

#include "Json.hpp"

namespace suka {

inline void loadSettings() {
    std::string p = PROJECT_ROOT + "/settings.json";
    if (!fileExists(p)) return;

    std::string json = readFile(p);
    float t = 0;
    if (jsonGetNumber(json, "theme", t)) {
        int i = (int)t;
        if (i >= 0 && i < (int)themeList().size()) {
            themeIndexRef() = i;
        }
    }
}

inline void saveSettings() {
    std::ofstream f(PROJECT_ROOT + "/settings.json");
    if (f.good()) {
        f << "{\n  \"theme\": " << themeIndexRef() << "\n}\n";
    }
}

} // namespace suka