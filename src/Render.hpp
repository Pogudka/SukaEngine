#pragma once

#include <string>
#include <sstream>
#include <map>
#include <iostream>
#include <cmath>

#include "Scene.hpp"

namespace suka {

inline std::string g_projectRoot;

inline std::string resolveAssetPath(const std::string& p) {
    if (p.empty() || p[0] == '/' || g_projectRoot.empty()) return p;
    return g_projectRoot + "/" + p;
}

struct DrawCmd {
    enum class Kind { Bg, Rect, Text, Texture, Shape, Button } kind = Kind::Rect;
    Rect rect;
    std::string text;
    std::string texture;
    std::string shape;
    float fontSize = 0;
    unsigned int color = 0xFFFFFFFF;
    float angle = 0.0f;
};

inline std::string substituteVars(const std::string& text, const std::map<std::string, double>& vars) {
    std::string out;
    size_t i = 0;
    while (i < text.size()) {
        if (text[i] == '{') {
            size_t j = text.find('}', i);
            if (j != std::string::npos) {
                std::string key = text.substr(i + 1, j - i - 1);
                auto it = vars.find(key);
                if (it != vars.end()) out += std::to_string((long long)it->second);
                else out += "0";
                i = j + 1;
                continue;
            }
        }
        out += text[i++];
    }
    return out;
}

class IRenderBackend {
public:
    virtual ~IRenderBackend() = default;
    virtual void begin() = 0;
    virtual void draw(const DrawCmd& cmd) = 0;
    virtual void end() = 0;
};

class LogRenderBackend : public IRenderBackend {
public:
    void begin() override { index_ = 0; }
    void draw(const DrawCmd& cmd) override {
        std::cout << "  draw[" << index_++ << "] ";
        if (cmd.kind == DrawCmd::Kind::Bg) std::cout << "bg=" << colorToHex(cmd.color);
        else if (cmd.kind == DrawCmd::Kind::Rect) std::cout << "rect=(" << cmd.rect.x << "," << cmd.rect.y << "," << cmd.rect.w << "," << cmd.rect.h << ") color=" << colorToHex(cmd.color) << " ang=" << cmd.angle;
        else if (cmd.kind == DrawCmd::Kind::Text) std::cout << "text='" << cmd.text << "' at=(" << cmd.rect.x << "," << cmd.rect.y << ") font=" << (int)cmd.fontSize << " color=" << colorToHex(cmd.color) << " ang=" << cmd.angle;
        else if (cmd.kind == DrawCmd::Kind::Texture) std::cout << "texture='" << cmd.texture << "' at=(" << cmd.rect.x << "," << cmd.rect.y << "," << cmd.rect.w << "," << cmd.rect.h << ") ang=" << cmd.angle;
        else if (cmd.kind == DrawCmd::Kind::Shape) std::cout << "shape=" << cmd.shape << " at=(" << cmd.rect.x << "," << cmd.rect.y << "," << cmd.rect.w << "," << cmd.rect.h << ") color=" << colorToHex(cmd.color) << " ang=" << cmd.angle;
        else std::cout << "button='" << cmd.text << "' at=(" << cmd.rect.x << "," << cmd.rect.y << "," << cmd.rect.w << "," << cmd.rect.h << ") color=" << colorToHex(cmd.color) << " ang=" << cmd.angle << " tex=" << cmd.texture;
        std::cout << "\n";
    }
    void end() override {}
private:
    int index_ = 0;
};

class StringRenderBackend : public IRenderBackend {
public:
    void begin() override { ss_.str(""); ss_.clear(); }
    void draw(const DrawCmd& cmd) override {
        if (cmd.kind == DrawCmd::Kind::Bg) {
            ss_ << "DRAW bg|" << colorToHex(cmd.color) << "\n";
        } else if (cmd.kind == DrawCmd::Kind::Text) {
            ss_ << "DRAW text|" << cmd.text << "|" << (int)cmd.rect.x << "|"
                << (int)cmd.rect.y << "|" << (int)cmd.fontSize << "|"
                << colorToHex(cmd.color) << "|" << cmd.angle << "\n";
        } else if (cmd.kind == DrawC
