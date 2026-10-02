#pragma once

#include <memory>
#include <string>
#include <vector>
#include <map>
#include <iostream>

#include "Nodes.hpp"
#include "Json.hpp"
#include "Input.hpp"

namespace suka {

class Scene {
public:
    std::string name;
    std::unique_ptr<Node> root;
    std::vector<UiButton> ui;
    float gravity = 0.0f;
    std::string bg;
    float camX = 0.0f, camY = 0.0f;
    bool bgSet() const { return !bg.empty(); }

    void update(Context& ctx, double dt) {
        if (root) { root->update(ctx, dt); if (gravity > 0.0f) physicsStep(ctx, dt); root->prune(); }
    }
    void resolveLinks() {
        if (!root) return;
        Node* camNode = root->findByType("Camera2D");
        if (!camNode) return;
        Camera2D* cam = static_cast<Camera2D*>(camNode);
        if (cam->followName.empty()) return;
        Node* t = root->findNode(cam->followName);
        if (t && std::string(t->typeName()) != "Node") cam->target = static_cast<Node2D*>(t);
    }

private:
    static void collectByType(Node& n, const char* t, std::vector<Node2D*>& out) {
        Node2D* n2 = dynamic_cast<Node2D*>(&n);
        if (n2 && std::string(n2->typeName()) == t) out.push_back(n2);
        for (auto& c : n.getChildren()) collectByType(*c, t, out);
    }
    static bool aabb(Player* p, Solid2D* s) {
        return std::abs(p->position.x - s->position.x) < 16.0f + s->w/2.0f && std::abs(p->position.y - s->position.y) < 16.0f + s->h/2.0f;
    }
    void physicsStep(Context& ctx, double dt) {
        if (!root) return;
        std::vector<Node2D*> players, solids;
        collectByType(*root, "Player", players); collectByType(*root, "Solid2D", solids);
        for (auto* pn : players) {
            Player* pl = static_cast<Player*>(pn);
            if (pl->mode != "platformer") continue;
            pl->position.x += ctx.input.joystickX * pl->speed * float(dt);
            for (auto* sn : solids) { Solid2D* s = static_cast<Solid2D*>(sn);
                if (aabb(pl, s)) { if (pl->position.x < s->position.x) pl->position.x = s->position.x - s->w/2 - 16; else pl->position.x = s->position.x + s->w/2 + 16; } }
            pl->vy += gravity * float(dt);
            if (ctx.input.jumpPressed && pl->grounded) { pl->vy = -pl->jumpSpeed; pl->grounded = false; }
            pl->position.y += pl->vy * float(dt); pl->grounded = false;
            for (auto* sn : solids) { Solid2D* s = static_cast<Solid2D*>(sn);
                if (aabb(pl, s)) { if (pl->vy > 0) { pl->position.y = s->position.y - s->h/2 - 16; pl->vy = 0; pl->grounded = true; }
                    else if (pl->vy < 0) { pl->position.y = s->position.y + s->h/2 + 16; pl->vy = 0; } } }
            ctx.playerPos = pl->position; ctx.hasPlayer = true;
        }
    }
};

class SceneLoader {
public:
    static bool load(const std::string& path, Scene& scene, const std::string& fontPath, const std::string& projectRoot) {
        if (!fileExists(path)) return false;
        std::string json = readFile(path);
        if (json.empty()) return false;

        scene.name = "Scene";
        jsonGetString(json, "name", scene.name);
        jsonGetNumber(json, "gravity", scene.gravity);
        jsonGetString(json, "bg", scene.bg);

        scene.root = std::make_unique<Node>(); scene.root->name = scene.name;

        size_t begin = 0, end = 0;
        if (jsonFindArray(json, "nodes", begin, end)) {
            std::string arr = json.substr(begin, end - begin + 1);
            for (const auto& obj : jsonSplitObjects(arr)) {
                std::string type; jsonGetString(obj, "type", type);
                std::unique_ptr<Node2D> node2d;
                if (type == "Label") {
                    auto label = std::make_unique<Label>();
                    jsonGetString(obj, "text", label->text);
                    float fontSize = 0; if (jsonGetNumber(obj, "font_size", fontSize)) label->fontSize = (int)fontSize;
                    label->fontName = DEFAULT_FONT_NAME; label->fontPath = fontPath; label->color = currentTheme().ink;
                    node2d = std::move(label);
                }
                else if (type == "Sprite2D") {
                    auto sprite = std::make_unique<Sprite2D>();
                    jsonGetString(obj, "texture", sprite->texturePath);
                    float w = 0, h = 0; if (jsonGetNumber(obj, "w", w)) sprite->size.x = w; if (jsonGetNumber(obj, "h", h)) sprite->size.y = h;
                    node2d = std::move(sprite);
                }
                else if (type == "Player") {
                    auto player = std::make_unique<Player>();
                    float speed = 0; if (jsonGetNumber(obj, "speed", speed)) player->speed = speed;
                    jsonGetString(obj, "mode", player->mode);
                    float js = 0; if (jsonGetNumber(obj, "jump_speed", js)) player->jumpSpeed = js;
                    node2d = std::move(player);
                }
                else if (type == "Enemy") { auto e = std::make_unique<Enemy>(); float s = 0; if (jsonGetNumber(obj, "speed", s)) e->speed = s; node2d = std::move(e); }
                else if (type == "Coin") { auto c = std::make_unique<Coin>(); float r = 0; if (jsonGetNumber(obj, "radius", r)) c->radius = r; node2d = std::move(c); }
                else if (type == "Solid2D") { auto s = std::make_unique<Solid2D>(); float w = 0, h = 0; if (jsonGetNumber(obj, "w", w)) s->w = w; if (jsonGetNumber(obj, "h", h)) s->h = h; node2d = std::move(s); }
                else if (type == "Camera2D") { auto c = std::make_unique<Camera2D>(); jsonGetString(obj, "follow", c->followName); float z = 0; if (jsonGetNumber(obj, "zoom", z)) c->zoom = z; node2d = std::move(c); }
                else if (type == "Light2D") { auto l = std::make_unique<Light2D>(); float r = 0; if (jsonGetNumber(obj, "radius", r)) l->radius = r; float i = 0; if (jsonGetNumber(obj, "intensity", i)) l->intensity = i; node2d = std::move(l); }
                else node2d = std::make_unique<Node2D>();

                jsonGetString(obj, "name", node2d->name);
                float x = 0, y = 0; if (jsonGetNumber(obj, "x", x)) node2d->position.x = x; if (jsonGetNumber(obj, "y", y)) node2d->position.y = y;

                std::string shapeStr, colorStr, texStr, actStr;
                jsonGetString(obj, "shape", shapeStr); if (!shapeStr.empty()) node2d->shape = shapeStr;
                jsonGetString(obj, "color", colorStr); if (!colorStr.empty()) node2d->color = parseColor(colorStr);
                if (type != "Sprite2D") { jsonGetString(obj, "texture", texStr); if (!texStr.empty()) node2d->texture = texStr; }
                jsonGetString(obj, "action", actStr); if (!actStr.empty()) node2d->action = actStr;

                float aw = 0, ah = 0; if (jsonGetNumber(obj, "w", aw)) node2d->w = aw; if (jsonGetNumber(obj, "h", ah)) node2d->h = ah;
                float rotDeg = 0, scx = 1, scy = 1;
                if (jsonGetNumber(obj, "rotation", rotDeg)) node2d->rotation = rotDeg * 3.14159265f / 180.0f;
                if (jsonGetNumber(obj, "scale_x", scx)) node2d->scale.x = scx;
                if (jsonGetNumber(obj, "scale_y", scy)) node2d->scale.y = scy;

                float lk = 0; if (jsonGetNumber(obj, "locked", lk)) node2d->locked = (lk != 0);
                float alp = 1.0f; if (jsonGetNumber(obj, "alpha", alp)) node2d->alpha = alp;

                scene.root->addChild(std::move(node2d));
            }
        }

        begin = 0; end = 0;
        if (jsonFindArray(json, "ui", begin, end)) {
            std::string arr = json.substr(begin, end - begin + 1);
            for (const auto& obj : jsonSplitObjects(arr)) {
                UiButton button; button.color = currentTheme().button;
                jsonGetString(obj, "id", button.touch.id);
                jsonGetString(obj, "text", button.text);
                jsonGetString(obj, "action", button.action);
                std::string btnColor; jsonGetString(obj, "color", btnColor); if (!btnColor.empty()) button.color = parseColor(btnColor);
                float btnAngle = 0; if (jsonGetNumber(obj, "angle", btnAngle)) button.angle = btnAngle;
                jsonGetString(obj, "texture", button.texture);
                jsonGetString(obj, "group", button.group);
                float x = 0, y = 0, w = 0, h = 0;
                jsonGetNumber(obj, "x", x); jsonGetNumber(obj, "y", y);
                jsonGetNumber(obj, "w", w); jsonGetNumber(obj, "h", h);
                button.touch.rect = Rect{x, y, w, h};
                scene.ui.push_back(button);
            }
        }
        return true;
    }
};

class SceneManager {
public:
    SceneManager(std::string projectRoot, std::string fontPath)
        : projectRoot_(std::move(projectRoot)), fontPath_(std::move(fontPath)) {}
    bool changeScene(const std::string& rel, ResourceManager& rm) { return switchTo(rel, rm, false); }
    bool restartScene(const std::string& rel, ResourceManager& rm) { return switchTo(rel, rm, true); }
    void requestChange(const std::string& rel, bool force) { pending_ = rel; pendingForce_ = force; hasPending_ = true; }
    Scene* current() { return scene_; }
    const std::string& currentPath() const { return currentPath_; }

    void update(Context& ctx, double dt, InputManager& input, ResourceManager& rm) {
        if (hasPending_) {
            hasPending_ = false;
            const std::string target = pending_; const bool force = pendingForce_;
            input.joystick.reset(); input.setUi(nullptr);
            if (switchTo(target, rm, force)) input.setUi(&scene_->ui);
            else std::cout << "[SceneManager] failed to load: " << target << "\n";
        }
        if (scene_) scene_->update(ctx, dt);
    }

private:
    bool switchTo(const std::string& rel, ResourceManager& rm, bool force) {
        if (!force) {
            auto it = cache_.find(rel);
            if (it != cache_.end() && it->second) { scene_ = it->second.get(); currentPath_ = rel; return true; }
        }
        Scene next;
        const std::string full = projectRoot_ + "/" + rel;
        if (!SceneLoader::load(full, next, fontPath_, projectRoot_)) return false;
        auto up = std::make_unique<Scene>(std::move(next));
        Scene* ptr = up.get();
        cache_[rel] = std::move(up);
        ptr->resolveLinks();
        ptr->root->loadResources(rm, projectRoot_);
        scene_ = ptr; currentPath_ = rel;
        return true;
    }
    std::string projectRoot_; std::string fontPath_; std::string currentPath_;
    std::string pending_; bool pendingForce_ = false; bool hasPending_ = false;
    Scene* scene_ = nullptr;
    std::map<std::string, std::unique_ptr<Scene>> cache_;
};

} // namespace suka
