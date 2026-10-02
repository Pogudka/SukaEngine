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
    std::string text, texture, shape;
    float fontSize = 0;
    unsigned int color = 0xFFFFFFFF;
    float angle = 0.0f;
    float alpha = 1.0f;     // ALPHA-FIX
};

inline std::string substituteVars(const std::string& text, const std::map<std::string, double>& vars) {
    std::string out; size_t i = 0;
    while (i < text.size()) {
        if (text[i] == '{') {
            size_t j = text.find('}', i);
            if (j != std::string::npos) {
                std::string key = text.substr(i + 1, j - i - 1);
                auto it = vars.find(key);
                if (it != vars.end()) out += std::to_string((long long)it->second); else out += "0";
                i = j + 1; continue;
            }
        }
        out += text[i++];
    }
    return out;
}

class IRenderBackend {
public:
    virtual ~IRenderBackend() = default;
    virtual void begin() = 0; virtual void draw(const DrawCmd& cmd) = 0; virtual void end() = 0;
};

class LogRenderBackend : public IRenderBackend {
public:
    void begin() override { index_ = 0; }
    void draw(const DrawCmd& cmd) override {
        std::cout << "  draw[" << index_++ << "] ";
        if (cmd.kind == DrawCmd::Kind::Bg) std::cout << "bg=" << colorToHex(cmd.color);
        else std::cout << "alpha=" << cmd.alpha;
        std::cout << "\n";
    }
    void end() override {}
private: int index_ = 0;
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
                << colorToHex(cmd.color) << "|" << cmd.angle << "|" << cmd.alpha << "\n";
        } else if (cmd.kind == DrawCmd::Kind::Rect) {
            ss_ << "DRAW rect|" << (int)cmd.rect.x << "|" << (int)cmd.rect.y << "|"
                << (int)cmd.rect.w << "|" << (int)cmd.rect.h << "|"
                << colorToHex(cmd.color) << "|" << cmd.angle << "|" << cmd.alpha << "\n";
        } else if (cmd.kind == DrawCmd::Kind::Texture) {
            ss_ << "DRAW tex|" << cmd.texture << "|" << (int)cmd.rect.x << "|"
                << (int)cmd.rect.y << "|" << (int)cmd.rect.w << "|"
                << (int)cmd.rect.h << "|" << cmd.angle << "|" << cmd.alpha << "\n";
        } else if (cmd.kind == DrawCmd::Kind::Shape) {
            ss_ << "DRAW shape|" << cmd.shape << "|" << (int)cmd.rect.x << "|"
                << (int)cmd.rect.y << "|" << (int)cmd.rect.w << "|"
                << (int)cmd.rect.h << "|" << colorToHex(cmd.color) << "|" << cmd.angle << "|" << cmd.alpha << "\n";
        } else {
            ss_ << "DRAW button|" << cmd.text << "|" << (int)cmd.rect.x << "|"
                << (int)cmd.rect.y << "|" << (int)cmd.rect.w << "|"
                << (int)cmd.rect.h << "|" << colorToHex(cmd.color) << "|"
                << cmd.angle << "|" << cmd.texture << "|" << cmd.alpha << "\n";
        }
    }
    void end() override {}
    std::string str() const { return ss_.str(); }
private: std::ostringstream ss_;
};

struct WorldXf {
    float x = 0, y = 0, rot = 0, sx = 1, sy = 1;
    WorldXf child(const Vec2& p, float r, float csx, float csy) const {
        float cr = std::cos(rot), sr = std::sin(rot);
        WorldXf w;
        w.x = x + (p.x * sx) * cr - (p.y * sy) * sr;
        w.y = y + (p.x * sx) * sr + (p.y * sy) * cr;
        w.rot = rot + r;
        w.sx = sx * csx; w.sy = sy * csy;
        return w;
    }
};

class Renderer {
public:
    explicit Renderer(IRenderBackend& backend) : backend_(backend) {}

    void render(Scene& scene, Context* ctx = nullptr) {
        ctx_ = ctx;
        backend_.begin();
        DrawCmd bg; bg.kind = DrawCmd::Kind::Bg;
        bg.color = scene.bgSet() ? parseColor(scene.bg) : currentTheme().bg;
        backend_.draw(bg);

        camActive_ = false; camX_ = 0; camY_ = 0; camZoom_ = 1;
        if (scene.root) {
            Node* cn = scene.root->findByType("Camera2D");
            if (cn) { Camera2D* cam = static_cast<Camera2D*>(cn); camActive_ = true; camX_ = cam->position.x; camY_ = cam->position.y; camZoom_ = cam->zoom > 0.01f ? cam->zoom : 1.0f; }
        }

        if (scene.root) { WorldXf identity; collectNode(*scene.root, identity); }

        for (auto& b : scene.ui) {
            DrawCmd cmd; cmd.kind = DrawCmd::Kind::Button;
            cmd.text = b.text; cmd.rect = b.touch.rect; cmd.color = b.color;
            cmd.angle = b.angle; cmd.texture = resolveAssetPath(b.texture);
            cmd.alpha = b.alpha;     // ALPHA-FIX
            backend_.draw(cmd);
        }
        backend_.end();
    }

private:
    static float deg(float rad) { return rad * 57.2957795f; }
    float toScreenX(float wx) const { return camActive_ ? (wx - camX_) * camZoom_ + 640.0f : wx; }
    float toScreenY(float wy) const { return camActive_ ? (wy - camY_) * camZoom_ + 360.0f : wy; }
    float zsf() const { return camActive_ ? camZoom_ : 1.0f; }

    void collectNode(Node& node, const WorldXf& parent) {
        Node2D* node2d = dynamic_cast<Node2D*>(&node);
        if (!node2d) { for (const auto& child : node.getChildren()) collectNode(*child, parent); return; }
        const std::string type = node2d->typeName();

        WorldXf w = parent.child(node2d->position, node2d->rotation, node2d->scale.x, node2d->scale.y);
        float wx = w.x, wy = w.y, wang = deg(w.rot);
        float wsx = w.sx, wsy = w.sy;

        float sx = toScreenX(wx), sy = toScreenY(wy), zf = zsf();
        float alp = node2d->alpha;     // ALPHA-FIX

        if (type == "Label") {
            Label& label = static_cast<Label&>(*node2d);
            DrawCmd cmd; cmd.kind = DrawCmd::Kind::Text;
            cmd.text = ctx_ ? substituteVars(label.text, ctx_->vars) : label.text;
            cmd.rect = Rect{sx, sy, 0, 0};
            cmd.fontSize = label.fontSize * ((wsx + wsy) * 0.5f) * zf;
            cmd.color = label.color; cmd.angle = wang; cmd.alpha = alp;
            backend_.draw(cmd);
        }
        else if (type == "Sprite2D") {
            Sprite2D& sprite = static_cast<Sprite2D&>(*node2d);
            DrawCmd cmd; cmd.kind = DrawCmd::Kind::Texture;
            cmd.texture = resolveAssetPath(sprite.texturePath);
            cmd.rect = Rect{sx - sprite.size.x*wsx*zf/2, sy - sprite.size.y*wsy*zf/2, sprite.size.x*wsx*zf, sprite.size.y*wsy*zf};
            cmd.angle = wang; cmd.alpha = alp;
            backend_.draw(cmd);
        }
        else if (type == "Camera2D") { /* skip */ }
        else if (type == "Light2D") {
            Light2D& li = static_cast<Light2D&>(*node2d);
            float r = li.radius * ((wsx + wsy) * 0.5f) * zf;
            DrawCmd cmd; cmd.kind = DrawCmd::Kind::Shape; cmd.shape = "glow";
            cmd.rect = Rect{sx - r, sy - r, r*2, r*2};
            cmd.color = li.color; cmd.angle = 0; cmd.alpha = alp;
            backend_.draw(cmd);
        }
        else if (node2d->hasAppearance()) {
            float ww = node2d->w * wsx * zf, hh = node2d->h * wsy * zf;
            DrawCmd cmd;
            if (!node2d->texture.empty()) {
                cmd.kind = DrawCmd::Kind::Texture; cmd.texture = resolveAssetPath(node2d->texture);
                cmd.rect = Rect{sx - ww/2, sy - hh/2, ww, hh};
            } else {
                cmd.kind = DrawCmd::Kind::Shape; cmd.shape = node2d->shape;
                cmd.rect = Rect{sx - ww/2, sy - hh/2, ww, hh}; cmd.color = node2d->color;
            }
            cmd.angle = wang; cmd.alpha = alp;
            backend_.draw(cmd);
        }
        for (const auto& child : node2d->getChildren()) collectNode(*child, w);
    }

    IRenderBackend& backend_; Context* ctx_ = nullptr;
    bool camActive_ = false; float camX_ = 0, camY_ = 0, camZoom_ = 1;
};

} // namespace suka
