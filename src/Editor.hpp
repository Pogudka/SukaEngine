#pragma once

#include <string>
#include <vector>
#include <iostream>
#include <fstream>
#include <functional>
#include <cstdio>

#include "Scene.hpp"

namespace suka {

struct EmitterPreset {
    std::string name;
    std::string glyph;
    float rate; int burst;
    float vx, vy, spread, gravity;
    float life, lifeSpread;
    float size, sizeEnd, drag;
    unsigned color;
};

inline const std::vector<EmitterPreset>& emitterPresets() {
    static std::vector<EmitterPreset> list = {
        {"fire",  "\xe2\x80\xa2", 40.0f, 40, 0.0f, -180.0f, 140.0f, 80.0f, 0.8f, 0.3f, 18.0f, 4.0f, 0.5f, 0xFFFF6020u},
        {"smoke", "\xe2\x97\x89", 20.0f, 20, 0.0f, -80.0f,  100.0f, 40.0f, 1.6f, 0.5f, 28.0f, 8.0f, 2.0f, 0x80808080u},
        {"rain",  "|",             60.0f, 80, 0.0f,  300.0f,  10.0f, 200.0f,0.5f, 0.1f, 4.0f,  1.0f, 0.2f, 0x8888FFFFu},
        {"snow",  "*",             25.0f, 30, 0.0f,   80.0f,  60.0f, 20.0f, 3.0f, 1.0f, 6.0f,  2.0f, 1.5f, 0xFFFFFFE0u},
        {"spark", "+",             12.0f, 50, 0.0f, -240.0f, 200.0f, 500.0f,0.4f, 0.1f, 3.0f,  0.5f, 0.0f, 0xFFFFE040u}
    };
    return list;
}

class SceneWriter {
public:
    static std::string writeNode(Node& n, int indent) { return nodeJson(n, indent); }

    static bool write(Scene& scene, const std::string& path) {
        std::ofstream f(path);
        if (!f.good()) return false;

        f << "{\n";
        f << "  \"name\": \"" << scene.name << "\",\n";
        f << "  \"gravity\": " << scene.gravity << ",\n";
        if (scene.bgSet()) f << "  \"bg\": \"" << scene.bg << "\",\n";
        f << "  \"nodes\": [\n";

        std::vector<std::string> parts;
        if (scene.root) for (const auto& ch : scene.root->getChildren()) parts.push_back(nodeJson(*ch, 4));
        for (size_t i = 0; i < parts.size(); ++i) f << parts[i] << (i + 1 < parts.size() ? ",\n" : "\n");

        f << "  ],\n  \"ui\": [\n";

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
            s += "\"color\": \"" + colorToHex(b.color) + "\", ";
            s += "\"angle\": " + std::to_string(b.angle);
            if (!b.texture.empty()) s += ", \"texture\": \"" + b.texture + "\"";
            if (!b.group.empty()) s += ", \"group\": \"" + b.group + "\"";
            s += ", \"alpha\": " + std::to_string(b.alpha);
            s += " }";
            uiparts.push_back(s);
        }
        for (size_t i = 0; i < uiparts.size(); ++i) f << uiparts[i] << (i + 1 < uiparts.size() ? ",\n" : "\n");

        f << "  ]\n}\n";
        return true;
    }

private:
    static std::string nodeJson(Node& n, int indent) {
        std::string pad(indent, ' ');

        Prefab2D* pf = dynamic_cast<Prefab2D*>(&n);
        if (pf) {
            std::string s = pad + "{ ";
            s += "\"type\": \"Prefab2D\", ";
            s += "\"name\": \"" + n.name + "\", ";
            s += "\"source\": \"" + pf->sourcePath + "\", ";
            s += "\"x\": " + std::to_string((int)pf->position.x);
            s += ", \"y\": " + std::to_string((int)pf->position.y);
            s += ", \"rotation\": " + std::to_string(pf->rotation * 57.2957795f);
            s += ", \"scale_x\": " + std::to_string(pf->scale.x);
            s += ", \"scale_y\": " + std::to_string(pf->scale.y);
            s += ", \"color\": \"" + colorToHex(pf->color) + "\"";
            s += ", \"w\": " + std::to_string((int)pf->w);
            s += ", \"h\": " + std::to_string((int)pf->h);
            s += ", \"locked\": " + std::string(pf->locked ? "1" : "0");
            s += ", \"alpha\": " + std::to_string(pf->alpha);
            s += " }";
            return s;
        }

        Node2D* n2 = dynamic_cast<Node2D*>(&n);
        std::string s = pad + "{ ";
        s += std::string("\"type\": \"") + n.typeName() + "\", ";
        s += "\"name\": \"" + n.name + "\"";
        if (n2) {
            s += ", \"x\": " + std::to_string((int)n2->position.x);
            s += ", \"y\": " + std::to_string((int)n2->position.y);
            s += ", \"rotation\": " + std::to_string(n2->rotation * 57.2957795f);
            s += ", \"scale_x\": " + std::to_string(n2->scale.x);
            s += ", \"scale_y\": " + std::to_string(n2->scale.y);
            if (!n2->action.empty()) s += ", \"action\": \"" + n2->action + "\"";
            s += ", \"shape\": \"" + n2->shape + "\"";
            s += ", \"color\": \"" + colorToHex(n2->color) + "\"";
            if (!n2->texture.empty()) s += ", \"texture\": \"" + n2->texture + "\"";
            s += ", \"w\": " + std::to_string((int)n2->w);
            s += ", \"h\": " + std::to_string((int)n2->h);
        }
        std::string t = n.typeName();
        if (t == "Label") { Label* l = static_cast<Label*>(n2); s += ", \"text\": \"" + l->text + "\""; s += ", \"font_size\": " + std::to_string(l->fontSize); }
        else if (t == "Sprite2D") { Sprite2D* sp = static_cast<Sprite2D*>(n2); s += ", \"texture\": \"" + sp->texturePath + "\""; s += ", \"w\": " + std::to_string((int)sp->size.x); s += ", \"h\": " + std::to_string((int)sp->size.y); }
        else if (t == "Player") { Player* p = static_cast<Player*>(n2); s += ", \"speed\": " + std::to_string((int)p->speed); s += ", \"mode\": \"" + p->mode + "\""; }
        else if (t == "Enemy") s += ", \"speed\": " + std::to_string((int)static_cast<Enemy*>(n2)->speed);
        else if (t == "Coin") s += ", \"radius\": " + std::to_string((int)static_cast<Coin*>(n2)->radius);
        else if (t == "Camera2D") s += ", \"follow\": \"" + static_cast<Camera2D*>(n2)->followName + "\"";
        else if (t == "Light2D") { Light2D* li = static_cast<Light2D*>(n2); s += ", \"radius\": " + std::to_string((int)li->radius); s += ", \"intensity\": " + std::to_string(li->intensity); }
        else if (t == "Particle2D") {
            Particle2D* pe = static_cast<Particle2D*>(n2);
            s += ", \"rate\": " + std::to_string(pe->rate);
            s += ", \"burst\": " + std::to_string(pe->burst);
            s += ", \"vx\": " + std::to_string(pe->vx);
            s += ", \"vy\": " + std::to_string(pe->vy);
            s += ", \"spread\": " + std::to_string(pe->spread);
            s += ", \"gravity\": " + std::to_string(pe->gravity);
            s += ", \"life\": " + std::to_string(pe->life);
            s += ", \"life_spread\": " + std::to_string(pe->lifeSpread);
            s += ", \"size\": " + std::to_string(pe->size);
            s += ", \"size_end\": " + std::to_string(pe->sizeEnd);
            s += ", \"drag\": " + std::to_string(pe->drag);
            s += ", \"glyph\": \"" + pe->glyph + "\"";
            s += ", \"emitting\": " + std::string(pe->emitting ? "1" : "0");
        }
        if (n2) {
            s += ", \"locked\": " + std::string(n2->locked ? "1" : "0");
            s += ", \"alpha\": " + std::to_string(n2->alpha);
        }

        const auto& kids = n.getChildren();
        if (!kids.empty()) {
            s += ", \"children\": [\n";
            for (size_t i = 0; i < kids.size(); ++i) {
                s += nodeJson(*kids[i], indent + 2);
                s += (i + 1 < kids.size() ? ",\n" : "\n");
            }
            s += pad + "]";
        }

        s += " }";
        return s;
    }
};

class Editor {
public:
    void attach(Scene* scene) { scene_ = scene; selected_ = nullptr; selectedUi_.clear(); }
    Scene* scene() const { return scene_; }
    std::string sceneName() const { return scene_ ? scene_->name : std::string("-"); }
    Node* selected() const { return selected_; }

    void select(const std::string& name) {
        if (!scene_ || !scene_->root) return;
        selectedUi_.clear(); selected_ = scene_->root->findNode(name);
    }
    std::string selectedUi() const { return selectedUi_; }
    void selectUi(const std::string& id) { selected_ = nullptr; selectedUi_ = id; }

    Node* findNode(const std::string& name) {
        if (!scene_ || !scene_->root) return nullptr;
        return scene_->root->findNode(name);
    }

    UiButton* findUi(const std::string& id) {
        if (!scene_) return nullptr;
        for (auto& b : scene_->ui) if (b.touch.id == id) return &b;
        return nullptr;
    }
    void addUi(const std::string& id, const std::string& text, float x, float y, float w, float h, const std::string& action, unsigned color) {
        if (!scene_) return;
        UiButton b; b.touch.id = id; b.text = text; b.action = action; b.color = color;
        b.touch.rect = Rect{x, y, w, h};
        scene_->ui.push_back(b);
    }
    void deleteUi(const std::string& id) {
        if (!scene_) return;
        for (auto it = scene_->ui.begin(); it != scene_->ui.end(); ++it)
            if (it->touch.id == id) { if (selectedUi_ == id) selectedUi_.clear(); scene_->ui.erase(it); return; }
    }

    Node2D* cloneSelected(const std::string& newName) {
        if (!scene_ || !scene_->root || !selected_) return nullptr;
        std::unique_ptr<Node> cp = selected_->cloneNode();
        cp->name = newName;
        Node* raw = cp.get();
        scene_->root->addChild(std::move(cp));
        return dynamic_cast<Node2D*>(raw);
    }

    void moveSelected(float dx, float dy) {
        Node2D* n2 = selected_ ? dynamic_cast<Node2D*>(selected_) : nullptr;
        if (!n2) return;
        n2->position.x += dx; n2->position.y += dy;
    }
    void setTextSelected(const std::string& text) {
        if (!selected_) return;
        if (std::string(selected_->typeName()) == "Label") static_cast<Label*>(selected_)->text = text;
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
        else if (type == "Light2D") n = std::make_unique<Light2D>();
        else if (type == "Particle2D") n = std::make_unique<Particle2D>();
        else if (type == "Prefab2D") n = std::make_unique<Prefab2D>();
        else n = std::make_unique<Node2D>();
        n->name = name; n->position = Vec2{x, y};
        Node2D* raw = n.get();
        scene_->root->addChild(std::move(n));
        return raw;
    }

    void addParticleNode(const std::string& name, float x, float y, const std::string& glyph) {
        Node2D* n2 = addNode("Particle2D", name, x, y);
        if (!n2) return;
        Particle2D* pe = static_cast<Particle2D*>(n2);
        pe->glyph = glyph;
        pe->emitting = true;
        pe->burstPending = true;
    }

    bool setEmitterPreset(const std::string& name, const std::string& presetName) {
        Node* n = findNode(name);
        Particle2D* pe = n ? dynamic_cast<Particle2D*>(n) : nullptr;
        if (!pe) return false;
        for (const auto& p : emitterPresets()) {
            if (p.name == presetName) {
                pe->glyph = p.glyph; pe->rate = p.rate; pe->burst = p.burst;
                pe->vx = p.vx; pe->vy = p.vy; pe->spread = p.spread; pe->gravity = p.gravity;
                pe->life = p.life; pe->lifeSpread = p.lifeSpread;
                pe->size = p.size; pe->sizeEnd = p.sizeEnd; pe->drag = p.drag;
                pe->color = p.color;
                return true;
            }
        }
        return false;
    }

    std::string currentEmitterPreset(const std::string& name) const {
        Node* n = scene_ && scene_->root ? scene_->root->findNode(name) : nullptr;
        Particle2D* pe = n ? dynamic_cast<Particle2D*>(n) : nullptr;
        if (!pe) return "";
        for (const auto& p : emitterPresets()) {
            if (p.glyph == pe->glyph &&
                std::abs(p.rate - pe->rate) < 0.01f &&
                p.burst == pe->burst &&
                std::abs(p.vx - pe->vx) < 0.01f &&
                std::abs(p.vy - pe->vy) < 0.01f &&
                std::abs(p.spread - pe->spread) < 0.01f &&
                std::abs(p.gravity - pe->gravity) < 0.01f &&
                std::abs(p.life - pe->life) < 0.01f &&
                std::abs(p.lifeSpread - pe->lifeSpread) < 0.01f &&
                std::abs(p.size - pe->size) < 0.01f &&
                std::abs(p.sizeEnd - pe->sizeEnd) < 0.01f &&
                std::abs(p.drag - pe->drag) < 0.01f &&
                p.color == pe->color) {
                return p.name;
            }
        }
        return "";
    }

    void setProjectRoot(const std::string& root) { projectRoot_ = root; }

    // Makes a prefab out of the selected subtree.
    // Writes the subtree as a standalone scene-format JSON in prefabs/<name>.json,
    // then replaces the original node with a Prefab2D referencing it.
    bool saveAsPrefab(const std::string& prefabName) {
        if (!scene_ || !scene_->root || !selected_) return false;
        Node* sel = selected_;
        if (sel == scene_->root.get()) return false;
        Node* owner = scene_->root->findParentOf(sel->name);
        if (!owner) return false;

        std::string relPath = "prefabs/" + prefabName + ".json";
        std::string full = projectRoot_ + "/" + relPath;

        size_t slash = full.find_last_of('/');
        if (slash != std::string::npos) {
            std::string dir = full.substr(0, slash);
            std::string cmd = "mkdir -p \"" + dir + "\"";
            system(cmd.c_str());
        }

        std::string json = "{\n  \"name\": \"" + prefabName + "\",\n  \"nodes\": [\n"
                         + SceneWriter::writeNode(*sel, 4) + "\n  ]\n}\n";
        { std::ofstream f(full); if (!f.good()) return false; f << json; }

        auto up = owner->takeChild(sel->name);
        if (!up) return false;

        auto pf = std::make_unique<Prefab2D>();
        pf->name = up->name;
        Node2D* u2 = dynamic_cast<Node2D*>(up.get());
        if (u2) { pf->position = u2->position; pf->rotation = u2->rotation; pf->scale = u2->scale; pf->alpha = u2->alpha; }
        pf->sourcePath = relPath;

        auto kids = up->takeAllChildren();
        for (auto& k : kids) pf->addChild(std::move(k));

        Node* pfRaw = pf.get();
        owner->addChild(std::move(pf));
        selected_ = pfRaw;
        return true;
    }

    // Saves the current scene to scenes/<name>.json.
    bool saveScene(const std::string& sceneName) {
        if (!scene_) return false;
        if (projectRoot_.empty()) return false;
        std::string rel = "scenes/" + sceneName + ".json";
        std::string full = projectRoot_ + "/" + rel;
        size_t slash = full.find_last_of('/');
        if (slash != std::string::npos) {
            std::string dir = full.substr(0, slash);
            std::string cmd = "mkdir -p \"" + dir + "\"";
            system(cmd.c_str());
        }
        return SceneWriter::write(*scene_, full);
    }

    void deleteNode(const std::string& name) {
        if (!scene_ || !scene_->root) return;
        Node* n = scene_->root->findNode(name);
        if (!n || n == scene_->root.get()) return;
        if (selected_ == n) selected_ = nullptr;
        n->dead = true;
        scene_->root->prune();
    }
    Node2D* find2d(const std::string& name) {
        if (!scene_ || !scene_->root) return nullptr;
        return dynamic_cast<Node2D*>(scene_->root->findNode(name));
    }
    void setShape(const std::string& name, const std::string& shape) { Node2D* n = find2d(name); if (n) n->shape = shape; }
    void setColor(const std::string& name, const std::string& color) { Node2D* n = find2d(name); if (n) n->color = parseColor(color); }
    void setTexture(const std::string& name, const std::string& path) { Node2D* n = find2d(name); if (n) n->texture = path; }

    std::vector<std::string> hierarchyLines() const { return {}; }
    std::vector<std::string> inspectorLines() const { return {}; }
    std::vector<std::string> sceneViewLines() const { return {}; }
    bool save(const std::string& path) { if (!scene_) return false; return SceneWriter::write(*scene_, path); }

private:
    Scene* scene_ = nullptr;
    Node* selected_ = nullptr;
    std::string selectedUi_;
    std::string projectRoot_;
};

} // namespace suka
