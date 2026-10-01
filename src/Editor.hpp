#pragma once

#include <string>
#include <vector>
#include <iostream>
#include <fstream>
#include <functional>

#include "Scene.hpp"

namespace suka {

class SceneWriter {
public:
    static bool write(Scene& scene, const std::string& path) {
        std::ofstream f(path);
        if (!f.good()) return false;

        f << "{\n";
        f << "  \"name\": \"" << scene.name << "\",\n";
        f << "  \"gravity\": " << scene.gravity << ",\n";          // FIX: раньше терялась при SAVE
        if (scene.bgSet()) f << "  \"bg\": \"" << scene.bg << "\",\n";   // BG-FIX: сохраняем фон
        f << "  \"nodes\": [\n";

        std::vector<std::string> parts;
        if (scene.root) {
            for (const auto& ch : scene.root->getChildren()) {
                parts.push_back(nodeJson(*ch, 4));
            }
        }
        for (size_t i = 0; i < parts.size(); ++i) {
            f << parts[i] << (i + 1 < parts.size() ? ",\n" : "\n");
        }

        f << "  ],\n";
        f << "  \"ui\": [\n";

        std::vector<std::string> uiparts;
        for (const auto& b : scene.ui) {
            std::string s = "    { ";
            s += "\"id\": \"" + b.touch.id + "\", ";
            s += "\"x\": " + std::to_string((int)b.touch.rect.x) + ", ";
            s += "\"y\": " + std::to_string((int)b.touch.rect.y) + ", ";
            s += "\"w\": " + std::to_string((int)b.touch.rect.w) + ", ";
            s += "\"h\": " + std::to_string((int)b.touch.rect.h) + ", ";
            s += "\"text\": \"" + b.text + "\", ";
            s += "\"action\": \"" + b.action + "\", ";
            s += "\"color\": \"" + colorToHex(b.color) + "\" }";
            uiparts.push_back(s);
        }
        for (size_t i = 0; i < uiparts.size(); ++i) {
            f << uiparts[i] << (i + 1 < uiparts.size() ? ",\n" : "\n");
        }

        f << "  ]\n";
        f << "}\n";
        return true;
    }

private:
    static std::string nodeJson(Node& n, int indent) {
        std::string pad(indent, ' ');
        Node2D* n2 = dynamic_cast<Node2D*>(&n);

        std::string s = pad + "{ ";
        s += std::string("\"type\": \"") + n.typeName() + "\", ";
        s += "\"name\": \"" + n.name + "\"";

        if (n2) {
            s += ", \"x\": " + std::to_string((int)n2->position.x);
            s += ", \"y\": " + std::to_string((int)n2->position.y);
            s += ", \"rotation\": " + std::to_string(n2->rotation * 57.2957795f);   // ROT/SCL-FIX
            s += ", \"scale_x\": " + std::to_string(n2->scale.x);
            s += ", \"scale_y\": " + std::to_string(n2->scale.y);

            if (n2->hasAppearance()) {
                s += ", \"shape\": \"" + n2->shape + "\"";
                s += ", \"color\": \"" + colorToHex(n2->color) + "\"";
                if (!n2->texture.empty()) s += ", \"texture\": \"" + n2->texture + "\"";
                s += ", \"w\": " + std::to_string((int)n2->w);
                s += ", \"h\": " + std::to_string((int)n2->h);
            }
        }

        std::string t = n.typeName();
        if (t == "Label") {
            Label* l = static_cast<Label*>(n2);
            s += ", \"text\": \"" + l->text + "\"";
            s += ", \"font_size\": " + std::to_string(l->fontSize);
        }
        else if (t == "Sprite2D") {
            Sprite2D* sp = static_cast<Sprite2D*>(n2);
            s += ", \"texture\": \"" + sp->texturePath + "\"";
            s += ", \"w\": " + std::to_string((int)sp->size.x);
            s += ", \"h\": " + std::to_string((int)sp->size.y);
        }
        else if (t == "Player") {
            Player* p = static_cast<Player*>(n2);
            s += ", \"speed\": " + std::to_string((int)p->speed);
            s += ", \"mode\": \"" + p->mode + "\"";
        }
        else if (t == "Enemy") {
            s += ", \"speed\": " + std::to_string((int)static_cast<Enemy*>(n2)->speed);
        }
        else if (t == "Coin") {
            s += ", \"radius\": " + std::to_string((int)static_cast<Coin*>(n2)->radius);
        }
        else if (t == "Camera2D") {
            s += ", \"follow\": \"" + static_cast<Camera2D*>(n2)->followName + "\"";
        }

        s += " }";
        return s;
    }
};

class Editor {
public:
    void attach(Scene* scene) {
        scene_ = scene;
        selected_ = nullptr;
        std::cout << "[Editor] attached to scene: " << sceneName() << "\n";
    }

    Scene* scene() const { return scene_; }
    std::string sceneName() const { return scene_ ? scene_->name : std::string("-"); }
    Node* selected() const { return selected_; }

    void select(const std::string& name) {
        if (!scene_ || !scene_->root) return;
        selected_ = scene_->root->findNode(name);
        std::cout << "[Editor] select: " << name
                  << (selected_ ? "" : "  (NOT FOUND)") << "\n";
    }

    void moveSelected(float dx, float dy) {
        Node2D* n2 = selected_ ? dynamic_cast<Node2D*>(selected_) : nullptr;
        if (!n2) {
            std::cout << "[Editor] move failed: nothing selected\n";
            return;
        }
        n2->position.x += dx;
        n2->position.y += dy;
        std::cout << "[Editor] move '" << n2->name << "': +" << dx << ", +" << dy << "\n";
    }

    void setTextSelected(const std::string& text) {
        if (!selected_) return;
        if (std::string(selected_->typeName()) == "Label") {
            static_cast<Label*>(selected_)->text = text;
            std::cout << "[Editor] set text: '" << text << "'\n";
        }
    }

    Node2D* addNode(const std::string& type, const std::string& name, float x, float y) {
        if (!scene_ || !scene_->root) return nullptr;

        std::unique_ptr<Node2D> n;
        if (type == "Label") n = std::make_unique<Label>();
        else if (type == "Sprite2D") n = std::make_unique<Sprite2D>();
        else if (type == "Player") n = std::make_unique<Player>();
        else if (type == "Enemy") n = std::make_unique<Enemy>();
        else if (type == "Coin") n = std::make_unique<Coin>();
        else if (type == "Solid2D") n = std::make_unique<Solid2D>();
        else if (type == "Camera2D") n = std::make_unique<Camera2D>();
        else n = std::make_unique<Node2D>();

        n->name = name;
        n->position = Vec2{x, y};

        Node2D* raw = n.get();
        scene_->root->addChild(std::move(n));

        std::cout << "[Editor] add node: " << type << " '" << name
                  << "' at (" << x << ", " << y << ")\n";
        return raw;
    }

    void deleteNode(const std::string& name) {
        if (!scene_ || !scene_->root) return;
        Node* n = scene_->root->findNode(name);
        if (!n || n == scene_->root.get()) {
            std::cout << "[Editor] delete failed: " << name << "\n";
            return;
        }
        if (selected_ == n) selected_ = nullptr;
        n->dead = true;
        scene_->root->prune();
        std::cout << "[Editor] delete node: " << name << "\n";
    }

    // ===== внешний вид (v0.15.0) =====

    Node2D* find2d(const std::string& name) {
        if (!scene_ || !scene_->root) return nullptr;
        return dynamic_cast<Node2D*>(scene_->root->findNode(name));
    }

    void setShape(const std::string& name, const std::string& shape) {
        Node2D* n = find2d(name);
        if (!n) { std::cout << "[Editor] set_shape failed: " << name << "\n"; return; }
        n->shape = shape;
        std::cout << "[Editor] set_shape '" << name << "' -> " << shape << "\n";
    }

    void setColor(const std::string& name, const std::string& color) {
        Node2D* n = find2d(name);
        if (!n) { std::cout << "[Editor] set_color failed: " << name << "\n"; return; }
        n->color = parseColor(color);
        std::cout << "[Editor] set_color '" << name << "' -> " << color << "\n";
    }

    void setTexture(const std::string& name, const std::string& path) {
        Node2D* n = find2d(name);
        if (!n) { std::cout << "[Editor] set_texture failed: " << name << "\n"; return; }
        n->texture = path;
        std::cout << "[Editor] set_texture '" << name << "' -> " << path << "\n";
    }

    // ---------- строки для панелей ----------

    std::vector<std::string> hierarchyLines() const {
        std::vector<std::string> out;
        if (!scene_ || !scene_->root) return out;
        std::function<void(Node&, int)> walk = [&](Node& n, int depth) {
            std::string s(depth * 2, ' ');
            s += "- " + std::string(n.typeName()) + " '" + n.name + "'";
            if (&n == selected_) s += " [*]";
            out.push_back(s);
            for (const auto& ch : n.getChildren()) walk(*ch, depth + 1);
        };
        walk(*scene_->root, 0);
        return out;
    }

    std::vector<std::string> inspectorLines() const {
        std::vector<std::string> out;
        if (!selected_) {
            out.push_back("(nothing selected)");
            return out;
        }
        Node2D* n2 = dynamic_cast<Node2D*>(selected_);
        out.push_back("name: " + selected_->name);
        out.push_back("type: " + std::string(selected_->typeName()));
        if (n2) {
            out.push_back("pos: (" + std::to_string((int)n2->position.x) + ", " +
                          std::to_string((int)n2->position.y) + ")");
            if (n2->hasAppearance()) {
                out.push_back("shape: " + n2->shape);
                out.push_back("color: " + colorToHex(n2->color));
                if (!n2->texture.empty()) out.push_back("texture: " + n2->texture);
                out.push_back("size: (" + std::to_string((int)n2->w) + ", " +
                              std::to_string((int)n2->h) + ")");
            }
        }
        std::string t = selected_->typeName();
        if (t == "Label") {
            Label* l = static_cast<Label*>(n2);
            out.push_back("text: " + l->text);
            out.push_back("fontSize: " + std::to_string(l->fontSize));
        }
        else if (t == "Sprite2D") {
            Sprite2D* s = static_cast<Sprite2D*>(n2);
            out.push_back("texture: " + s->texturePath);
            out.push_back("size: (" + std::to_string((int)s->size.x) + ", " +
                          std::to_string((int)s->size.y) + ")");
        }
        else if (t == "Player") out.push_back("speed: " + std::to_string((int)static_cast<Player*>(n2)->speed));
        else if (t == "Enemy") out.push_back("speed: " + std::to_string((int)static_cast<Enemy*>(n2)->speed));
        else if (t == "Coin") out.push_back("radius: " + std::to_string((int)static_cast<Coin*>(n2)->radius));
        else if (t == "Camera2D") out.push_back("follow: " + static_cast<Camera2D*>(n2)->followName);
        return out;
    }

    std::vector<std::string> sceneViewLines() const {
        std::vector<std::string> out;
        if (!scene_ || !scene_->root) return out;

        const int W = 35, H = 18;
        std::vector<std::string> grid(H, std::string(W, '.'));

        auto put = [&](float x, float y, char c) {
            int cx = (int)(x * W / 720.0f);
            int cy = (int)(y * H / 1280.0f);
            if (cx >= 0 && cx < W && cy >= 0 && cy < H) grid[cy][cx] = c;
        };

        std::function<void(Node&)> walk = [&](Node& n) {
            Node2D* n2 = dynamic_cast<Node2D*>(&n);
            if (n2) {
                char c = markerFor(n2);
                if (n2 == selected_) c = '*';
                put(n2->position.x, n2->position.y, c);
            }
            for (const auto& ch : n.getChildren()) walk(*ch);
        };
        walk(*scene_->root);

        for (const auto& b : scene_->ui) {
            put(b.touch.rect.x + b.touch.rect.w / 2,
                b.touch.rect.y + b.touch.rect.h / 2, 'b');
        }

        for (const auto& row : grid) out.push_back(row);
        out.push_back("P pl E en C co L lb S sp K cam");
        out.push_back("o circle d diamond t triangle");
        out.push_back("b UI  * selected");
        return out;
    }

    void printHierarchy() const {
        std::cout << "Hierarchy:\n";
        for (const auto& l : hierarchyLines()) std::cout << l << "\n";
    }
    void printInspector() const {
        std::cout << "Inspector:\n";
        for (const auto& l : inspectorLines()) std::cout << l << "\n";
    }
    void printSceneView() const {
        std::cout << "Scene view:\n";
        for (const auto& l : sceneViewLines()) std::cout << l << "\n";
    }

    bool save(const std::string& path) {
        if (!scene_) return false;
        bool ok = SceneWriter::write(*scene_, path);
        std::cout << (ok ? "[Editor] saved: " : "[Editor] save failed: ") << path << "\n";
        return ok;
    }

private:
    static char markerFor(Node2D* n) {
        if (n->shape == "circle") return 'o';
        if (n->shape == "diamond") return 'd';
        if (n->shape == "triangle") return 't';
        std::string t = n->typeName();
        if (t == "Player") return 'P';
        if (t == "Enemy") return 'E';
        if (t == "Coin") return 'C';
        if (t == "Label") return 'L';
        if (t == "Sprite2D") return 'S';
        if (t == "Camera2D") return 'K';
        if (t == "Solid2D") return 'N';
        return 'N';
    }

    Scene* scene_ = nullptr;
    Node* selected_ = nullptr;
};

} // namespace suka
