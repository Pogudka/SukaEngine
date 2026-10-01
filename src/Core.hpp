#pragma once

#include <string>
#include <vector>
#include <cstdlib>
#include <cstdio>
#include <iostream>

namespace suka {

constexpr const char* ENGINE_NAME = "SukaEngine";
constexpr const char* ENGINE_VERSION = "0.17.0";
constexpr const char* DEFAULT_FONT_NAME = "Ubuntu";

inline std::string& projectRootRef() {
    static std::string root = "/storage/emulated/0/Documents/SukaEngine";
    return root;
}

inline void setProjectRoot(const std::string& r) {
    projectRootRef() = r;
}

#define PROJECT_ROOT suka::projectRootRef()

struct Vec2 {
    float x = 0.0f;
    float y = 0.0f;
};

struct Rect {
    float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;

    bool contains(float px, float py) const {
        return px >= x && px <= x + w && py >= y && py <= y + h;
    }
};

inline unsigned int parseColor(const std::string& s, unsigned int def = 0xFFFFFFFF) {
    if (s.empty()) return def;
    if (s[0] == '#') {
        unsigned int v = (unsigned int)strtoul(s.substr(1).c_str(), nullptr, 16);
        if (s.size() == 7) return (v << 8) | 0xFF;
        return v;
    }
    int r = 255, g = 255, b = 255;
    sscanf(s.c_str(), "%d,%d,%d", &r, &g, &b);
    return ((unsigned int)r << 24) | ((unsigned int)g << 16) | ((unsigned int)b << 8) | 0xFF;
}

inline std::string colorToHex(unsigned int c) {
    char buf[10];
    snprintf(buf, sizeof(buf), "#%06X", (c >> 8) & 0xFFFFFF);
    return buf;
}

struct Theme {
    std::string name;
    unsigned int bg;
    unsigned int accent;
    unsigned int ink;
    unsigned int button;
};

inline std::vector<Theme>& themeList() {
    static std::vector<Theme> t = {
        { "Cream & Red",   parseColor("#FFF3E0"), parseColor("#D62828"), parseColor("#1A1A2E"), parseColor("#D62828") },
        { "Teal & Navy",   parseColor("#16213E"), parseColor("#4CC9F0"), parseColor("#EAF2FF"), parseColor("#2D4A7B") },
        { "Night & Paper", parseColor("#111111"), parseColor("#F4EDE4"), parseColor("#F4EDE4"), parseColor("#F4EDE4") }
    };
    return t;
}

inline int& themeIndexRef() {
    static int i = 0;
    return i;
}

inline Theme& currentTheme() {
    return themeList()[themeIndexRef()];
}

inline void cycleTheme() {
    themeIndexRef() = (themeIndexRef() + 1) % (int)themeList().size();
}

} // namespace suka
