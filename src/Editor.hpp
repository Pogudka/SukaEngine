#pragma once

#include <string>
#include <vector>
#include <iostream>
#include <fstream>
#include <functional>

#include "Scene.hpp"

namespace suka {

// ---------------------------------------------------------------------------
// Emitter presets ("виды частиц"). Each preset fully describes a Particle2D:
// glyph + color + motion. The editor UI lists names and applies by name.
// ---------------------------------------------------------------------------
struct EmitterPreset {
    const char* name;
    const char* glyph;
    unsigned color;
    float rate, burst, vx, vy, spread, gravity, life, lifeSpread, size, sizeEnd, drag;
};

inline const std::vector<EmitterPreset>& emitterPresets() {
    static const std::vector<EmitterPreset> v = {
        // name      glyph  color        rate burst  vx   vy    spread grav  life  lspread size sizeEnd drag
        { "dot",     "\xe2\x80\xa2", 0xFFFFFFFF,  0,  24,   0, -120,  120,  300, 0.9f, 0.3f,  22,   0,  0.0f },
        { "star",    "*",            0xFFFFD700,  0,  30,   0, -160,  140,  320, 1.0f, 0.4f,  26,   0,  0.0f },
        { "spark",   "\xe2\x9c\xa6", 0xFFFF8000, 40,   0,   0, -200,   60,  500, 0.5f, 0.2f,  14,   0,  1.2f },
        { "ember",   ".",            0xFFFF4040, 30,   0,   0,  -40,   80,  900, 1.4f, 0.5f,  12,   0,  0.3f },
        { "smoke",   "\xe2\x97\x8b", 0xFFB0B0B0, 12,   0,   0,  -30,   40,  -20, 2.2f, 0.6f,  30,  60,  0.6f },
        { "snow",    "\xe2\x9d\x84", 0xFFBFEFFF,  8,   0,  20,   40,   60,  120, 3.0f, 0.8f,  18,   0,  0.2f },
        { "rain",    "|",            0xFF60A0FF, 60,   0,   0,  320,   20,    0, 0.6f, 0.1f,  10,   0,  0.0f },
        { "magic",   "\xe2\x9c\xa8", 0xFFC080FF, 20,   0,   0,  -90,  100, -120, 1.2f, 0.4f,  20,   0,  0.4f },
    };
    return v;
}

inline void applyEmitterPreset(Particle2D* pe, const EmitterPreset& p) {
    if (!pe) return;
    pe->glyph      = p.glyph;
    pe->color      = p.color;
    pe->rate       = p.rate;
    pe->burst      = (int)p.burst;
    pe->vx         = p.vx;
    pe->vy         = p.vy;
    pe->spread     = p.spread;
    pe->gravity    = p.gravity;
    pe->life       = p.life;
    pe->lifeSpread = p.lifeSpread;
    pe->size       = p.size;
    pe->sizeEnd    = p.sizeEnd;
    pe->drag       = p.drag;
}

class SceneWriter {
public:
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
            s += ", \"alpha\": " + std::to_string(b.alpha);   // BTN-ALPHA
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
        std::unique_ptr<Node> cp = selected_->cloneNode(); cp->name = newName;
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
        else n = std::make_unique<Node2D>();
        n->name = name; n->position = Vec2{x, y};
        Node2D* raw = n.get();
        scene_->root->addChild(std::move(n));
        return raw;
    }

    // ---- particle helpers for the [+] menu and the inspector ----

    // Create a Particle2D pre-loaded with a named view. Returns the node or null.
    Node2D* addParticleNode(const std::string& name, float x, float y, const std::string& presetName) {
        Node2D* raw = addNode("Particle2D", name, x, y);
        Particle2D* pe = raw ? dynamic_cast<Particle2D*>(raw) : nullptr;
        if (!pe) return raw;
        for (const auto& p : emitterPresets()) {
            if (presetName == p.name) { applyEmitterPreset(pe, p); break; }
        }
        pe->emitting = true;       // pour immediately after creation
        pe->burstPending = true;   // plus one opening volley
        return pe;
    }

    // Apply a named view to an existing node by name. True if node is an emitter.
    bool setEmitterPreset(const std::string& nodeName, const std::string& presetName) {
        Node2D* n = find2d(nodeName);
        Particle2D* pe = n ? dynamic_cast<Particle2D*>(n) : nullptr;
        if (!pe) return false;
        for (const auto& p : emitterPresets()) {
            if (presetName == p.name) { applyEmitterPreset(pe, p); return true; }
        }
        return false;
    }

    // Names of all views, for drawing the inspector row.
    std::vector<std::string> emitterPresetNames() const {
        std::vector<std::string> out;
        for (const auto& p : emitterPresets()) out.emplace_back(p.name);
        return out;
    }

    // Which view the node currently matches (by glyph), "" if custom/none.
    std::string currentEmitterPreset(const std::string& nodeName) {
        Node2D* n = find2d(nodeName);
        Particle2D* pe = n ? dynamic_cast<Particle2D*>(n) : nullptr;
        if (!pe) return "";
        for (const auto& p : emitterPresets()) {
            if (pe->glyph == p.glyph) return p.name;
        }
        return "";
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
    Scene* scene_ = nullptr; Node* selected_ = nullptr; std::string selectedUi_;
};

} // namespace suka
