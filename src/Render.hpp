#pragma once

#include <string>
#include <sstream>
#include <map>
#include <iostream>

#include "Scene.hpp"

namespace suka {

struct DrawCmd {
    enum class Kind { Bg, Rect, Text, Texture, Shape, Button } kind = Kind::Rect;
    Rect rect;
    std::string text;
    std::string texture;
    std::string shape;
    float fontSize = 0;
    unsigned int color = 0xFFFFFFFF;
    float angle = 0.0f;                 // ROT-FIX: градусы, 0 = без поворота
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
        else std::cout << "button='" << cmd.text << "' at=(" << cmd.rect.x << "," << cmd.rect.y << "," << cmd.rect.w << "," << cmd.rect.h << ") color=" << colorToHex(cmd.color);
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
                << colorToHex(cmd.color) << "|" << cmd.angle << "\n";      // +angle
        } else if (cmd.kind == DrawCmd::Kind::Rect) {
            ss_ << "DRAW rect|" << (int)cmd.rect.x << "|" << (int)cmd.rect.y << "|"
                << (int)cmd.rect.w << "|" << (int)cmd.rect.h << "|"
                << colorToHex(cmd.color) << "|" << cmd.angle << "\n";      // +angle
        } else if (cmd.kind == DrawCmd::Kind::Texture) {
            ss_ << "DRAW tex|" << cmd.texture << "|" << (int)cmd.rect.x << "|"
                << (int)cmd.rect.y << "|" << (int)cmd.rect.w << "|"
                << (int)cmd.rect.h << "|" << cmd.angle << "\n";            // +angle
        } else if (cmd.kind == DrawCmd::Kind::Shape) {
            ss_ << "DRAW shape|" << cmd.shape << "|" << (int)cmd.rect.x << "|"
                << (int)cmd.rect.y << "|" << (int)cmd.rect.w << "|"
                << (int)cmd.rect.h << "|" << colorToHex(cmd.color) << "|" << cmd.angle << "\n";  // +angle
        } else {
            ss_ << "DRAW button|" << cmd.text << "|" << (int)cmd.rect.x << "|"
                << (int)cmd.rect.y << "|" << (int)cmd.rect.w << "|"
                << (int)cmd.rect.h << "|" << colorToHex(cmd.color) << "\n";
        }
    }
    void end() override {}
    std::string str() const { return ss_.str(); }
private:
    std::ostringstream ss_;
};

class Renderer {
public:
    explicit Renderer(IRenderBackend& backend) : backend_(backend) {}

    void render(Scene& scene, Context* ctx = nullptr) {
        ctx_ = ctx;
        backend_.begin();
        DrawCmd bg;
        bg.kind = DrawCmd::Kind::Bg;
        bg.color = scene.bgSet() ? parseColor(scene.bg) : currentTheme().bg;
        backend_.draw(bg);
        if (scene.root) collectNode(*scene.root);
        for (auto& b : scene.ui) {
            DrawCmd cmd;
            cmd.kind = DrawCmd::Kind::Button;
            cmd.text = b.text;
            cmd.rect = b.touch.rect;
            cmd.color = b.color;
            backend_.draw(cmd);
        }
        backend_.end();
    }

private:
    static float deg(float rad) { return rad * 57.2957795f; }

    void collectNode(Node& node) {
        Node2D* node2d = dynamic_cast<Node2D*>(&node);
        if (!node2d) {
            for (const auto& child : node.getChildren()) collectNode(*child);
            return;
        }
        const std::string type = node2d->typeName();
        const float ang = deg(node2d->rotation);                 // ROT-FIX
        const float sx = node2d->scale.x, sy = node2d->scale.y;  // SCL-FIX

        if (type == "Label") {
            Label& label = static_cast<Label&>(*node2d);
            DrawCmd cmd;
            cmd.kind = DrawCmd::Kind::Text;
            cmd.text = ctx_ ? substituteVars(label.text, ctx_->vars) : label.text;
            cmd.rect = Rect{node2d->position.x, node2d->position.y, 0, 0};
            cmd.fontSize = label.fontSize;
            cmd.color = label.color;
            cmd.angle = ang;
            backend_.draw(cmd);
        }
        else if (type == "Sprite2D") {
            Sprite2D& sprite = static_cast<Sprite2D&>(*node2d);
            DrawCmd cmd;
            cmd.kind = DrawCmd::Kind::Texture;
            cmd.texture = sprite.texturePath;
            cmd.rect = Rect{node2d->position.x, node2d->position.y,
                            sprite.size.x * sx, sprite.size.y * sy};   // SCL-FIX
            cmd.angle = ang;
            backend_.draw(cmd);
        }
        else if (type == "Camera2D") {
            // камеру не рисуем
        }
        else if (node2d->hasAppearance()) {
            float w = node2d->w * sx, h = node2d->h * sy;             // SCL-FIX
            DrawCmd cmd;
            if (!node2d->texture.empty()) {
                cmd.kind = DrawCmd::Kind::Texture;
                cmd.texture = node2d->texture;
                cmd.rect = Rect{node2d->position.x - w / 2, node2d->position.y - h / 2, w, h};
            } else {
                cmd.kind = DrawCmd::Kind::Shape;
                cmd.shape = node2d->shape;
                cmd.rect = Rect{node2d->position.x - w / 2, node2d->position.y - h / 2, w, h};
                cmd.color = node2d->color;
            }
            cmd.angle = ang;
            backend_.draw(cmd);
        }

        for (const auto& child : node.getChildren()) collectNode(*child);
    }

    IRenderBackend& backend_;
    Context* ctx_ = nullptr;
};

} // namespace suka
