#pragma once

#include <iostream>
#include <memory>
#include <string>
#include <vector>
#include <cmath>
#include <algorithm>

#include "Core.hpp"
#include "Input.hpp"
#include "Resources.hpp"

namespace suka {

class Node {
public:
    std::string name;
    bool dead = false;

    virtual ~Node() = default;

    virtual const char* typeName() const { return "Node"; }
    virtual std::string extra() const { return ""; }

    virtual void update(Context& ctx, double dt) {
        for (auto& child : children) child->update(ctx, dt);
    }

    virtual void loadResources(ResourceManager& rm, const std::string& projectRoot) {
        for (auto& child : children) child->loadResources(rm, projectRoot);
    }

    virtual std::unique_ptr<Node> cloneNode() const {
        auto c = std::make_unique<Node>();
        copyBase(*c);
        return c;
    }

    Node* addChild(std::unique_ptr<Node> child) {
        Node* ptr = child.get();
        children.push_back(std::move(child));
        return ptr;
    }

    Node* findParentOf(const std::string& childName) {
        for (auto& child : children) {
            if (child->name == childName) return this;
            Node* p = child->findParentOf(childName);
            if (p) return p;
        }
        return nullptr;
    }

    bool containsName(const std::string& nodeName) const {
        if (name == nodeName) return true;
        for (const auto& child : children)
            if (child->containsName(nodeName)) return true;
        return false;
    }

    std::unique_ptr<Node> takeChild(const std::string& childName) {
        for (auto it = children.begin(); it != children.end(); ++it) {
            if ((*it)->name == childName) {
                std::unique_ptr<Node> taken = std::move(*it);
                children.erase(it);
                return taken;
            }
        }
        return nullptr;
    }

    size_t childCount() const { return children.size(); }

    Node* findNode(const std::string& nodeName) {
        if (name == nodeName) return this;
        for (auto& child : children) {
            Node* result = child->findNode(nodeName);
            if (result) return result;
        }
        return nullptr;
    }

    Node* findByType(const char* type) {
        if (std::string(typeName()) == type) return this;
        for (auto& child : children) {
            Node* result = child->findByType(type);
            if (result) return result;
        }
        return nullptr;
    }

    void prune() {
        children.erase(
            std::remove_if(children.begin(), children.end(),
                [](const std::unique_ptr<Node>& n) { return n->dead; }),
            children.end());
        for (auto& child : children) child->prune();
    }

    void printTree(int depth = 0) const {
        for (int i = 0; i < depth; ++i) std::cout << "  ";
        std::cout << "- " << typeName() << " '" << name << "'" << extra() << "\n";
        for (auto& child : children) child->printTree(depth + 1);
    }

    const std::vector<std::unique_ptr<Node>>& getChildren() const { return children; }

protected:
    void copyBase(Node& dst) const {
        dst.name = name;
        dst.dead = dead;
        for (const auto& ch : children) dst.addChild(ch->cloneNode());
    }

private:
    std::vector<std::unique_ptr<Node>> children;
};

class Node2D : public Node {
public:
    Vec2 position;
    Vec2 scale{1.0f, 1.0f};
    float rotation = 0.0f;

    std::string shape = "none";
    unsigned int color = 0xFFFFFFFF;
    std::string texture;
    float w = 32.0f, h = 32.0f;

    // TOUCH-FIX: объект-как-кнопка (действие по касанию в игре)
    std::string action;

    bool hasAppearance() const { return shape != "none" || !texture.empty(); }

    const char* typeName() const override { return "Node2D"; }

    std::string extra() const override {
        return " pos=(" + std::to_string((int)position.x) + "," +
               std::to_string((int)position.y) + ")";
    }

    std::unique_ptr<Node> cloneNode() const override {
        auto c = std::make_unique<Node2D>();
        copyBase(*c); copyNode2D(*c);
        return c;
    }

protected:
    void copyNode2D(Node2D& dst) const {
        dst.position = position; dst.scale = scale; dst.rotation = rotation;
        dst.shape = shape; dst.color = color; dst.texture = texture;
        dst.w = w; dst.h = h; dst.action = action;
    }
};

class Label : public Node2D {
public:
    std::string text;
    std::string fontName = DEFAULT_FONT_NAME;
    std::string fontPath;
    int fontSize = 24;

    const char* typeName() const override { return "Label"; }
    std::string extra() const override {
        return Node2D::extra() + " text='" + text + "' font=" + fontName + ":" + std::to_string(fontSize);
    }
    std::unique_ptr<Node> cloneNode() const override {
        auto c = std::make_unique<Label>();
        copyBase(*c); copyNode2D(*c);
        c->text = text; c->fontName = fontName; c->fontPath = fontPath; c->fontSize = fontSize;
        return c;
    }
};

class Sprite2D : public Node2D {
public:
    std::string texturePath;
    std::string resolvedPath;
    Vec2 size{64.0f, 64.0f};
    bool textureLoaded = false;

    const char* typeName() const override { return "Sprite2D"; }
    std::string extra() const override {
        return Node2D::extra() + " texture='" + texturePath + "' " + (textureLoaded ? "[loaded]" : "[missing]");
    }
    void loadResources(ResourceManager& rm, const std::string& projectRoot) override {
        if (!texturePath.empty() && texturePath[0] == '/') resolvedPath = texturePath;
        else resolvedPath = projectRoot + "/" + texturePath;
        textureLoaded = rm.loadTexture(resolvedPath);
        Node2D::loadResources(rm, projectRoot);
    }
    std::unique_ptr<Node> cloneNode() const override {
        auto c = std::make_unique<Sprite2D>();
        copyBase(*c); copyNode2D(*c);
        c->texturePath = texturePath; c->resolvedPath = resolvedPath;
        c->size = size; c->textureLoaded = textureLoaded;
        return c;
    }
};

class Solid2D : public Node2D {
public:
    Solid2D() { w = 64.0f; h = 64.0f; shape = "square"; color = 0x808080FF; }
    const char* typeName() const override { return "Solid2D"; }
    std::string extra() const override {
        return Node2D::extra() + " size=(" + std::to_string((int)w) + "," + std::to_string((int)h) + ")";
    }
    std::unique_ptr<Node> cloneNode() const override {
        auto c = std::make_unique<Solid2D>();
        copyBase(*c); copyNode2D(*c);
        return c;
    }
};

class Player : public Node2D {
public:
    float speed = 220.0f;
    float vy = 0.0f;
    bool jumpHeld = false;
    std::string mode = "topdown";
    float jumpSpeed = 300.0f;
    bool grounded = false;

    Player() { shape = "square"; color = 0x40C040FF; }
    const char* typeName() const override { return "Player"; }
    std::string extra() const override { return Node2D::extra() + " mode=" + mode; }
    void update(Context& ctx, double dt) override {
        if (mode == "platformer") { ctx.playerPos = position; ctx.hasPlayer = true; Node2D::update(ctx, dt); return; }
        position.x += ctx.input.joystickX * speed * float(dt);
        position.y += ctx.input.joystickY * speed * float(dt);
        if (ctx.input.jumpPressed && !jumpHeld) { vy = -300.0f; jumpHeld = true; ctx.jumpPressedThisFrame = true; }
        if (!ctx.input.jumpPressed) jumpHeld = false;
        position.y += vy * float(dt);
        vy *= 0.9f;
        ctx.playerPos = position; ctx.hasPlayer = true;
        Node2D::update(ctx, dt);
    }
    std::unique_ptr<Node> cloneNode() const override {
        auto c = std::make_unique<Player>();
        copyBase(*c); copyNode2D(*c);
        c->speed = speed; c->vy = vy; c->jumpHeld = jumpHeld;
        c->mode = mode; c->jumpSpeed = jumpSpeed; c->grounded = grounded;
        return c;
    }
};

class Enemy : public Node2D {
public:
    float speed = 80.0f;
    Enemy() { shape = "square"; color = 0xC04040FF; }
    const char* typeName() const override { return "Enemy"; }
    void update(Context& ctx, double dt) override {
        if (ctx.hasPlayer) {
            float dx = ctx.playerPos.x - position.x, dy = ctx.playerPos.y - position.y;
            float len = std::sqrt(dx*dx + dy*dy);
            if (len > 1.0f) { position.x += dx/len*speed*float(dt); position.y += dy/len*speed*float(dt); }
        }
        Node2D::update(ctx, dt);
    }
    std::unique_ptr<Node> cloneNode() const override {
        auto c = std::make_unique<Enemy>();
        copyBase(*c); copyNode2D(*c);
        c->speed = speed;
        return c;
    }
};

class Coin : public Node2D {
public:
    float radius = 24.0f;
    Coin() { shape = "circle"; color = 0xE0C040FF; w = 32.0f; h = 32.0f; }
    const char* typeName() const override { return "Coin"; }
    void update(Context& ctx, double dt) override {
        if (ctx.hasPlayer && !dead) {
            float dx = ctx.playerPos.x - position.x, dy = ctx.playerPos.y - position.y;
            if (std::sqrt(dx*dx + dy*dy) < radius) { dead = true; ctx.score += 1; ctx.coinCollectedThisFrame = true; }
        }
        Node2D::update(ctx, dt);
    }
    std::unique_ptr<Node> cloneNode() const override {
        auto c = std::make_unique<Coin>();
        copyBase(*c); copyNode2D(*c);
        c->radius = radius;
        return c;
    }
};

class Camera2D : public Node2D {
public:
    std::string followName;
    Node2D* target = nullptr;
    float zoom = 1.0f;
    float followSpeed = 5.0f;

    const char* typeName() const override { return "Camera2D"; }
    std::string extra() const override { return Node2D::extra() + " follow='" + followName + "'"; }
    void update(Context& ctx, double dt) override {
        if (target) {
            float k = 1.0f - std::exp(-followSpeed * float(dt));
            position.x += (target->position.x - position.x) * k;
            position.y += (target->position.y - position.y) * k;
        }
        Node2D::update(ctx, dt);
    }
    Vec2 worldToScreen(const Vec2& world, float screenW, float screenH) const {
        Vec2 s;
        s.x = (world.x - position.x) * zoom + screenW * 0.5f;
        s.y = (world.y - position.y) * zoom + screenH * 0.5f;
        return s;
    }
    std::unique_ptr<Node> cloneNode() const override {
        auto c = std::make_unique<Camera2D>();
        copyBase(*c); copyNode2D(*c);
        c->followName = followName; c->target = nullptr; c->zoom = zoom; c->followSpeed = followSpeed;
        return c;
    }
};

class Light2D : public Node2D {
public:
    float radius = 140.0f;
    float intensity = 1.0f;

    Light2D() { shape = "none"; color = 0xFFD700FF; w = 0; h = 0; }
    const char* typeName() const override { return "Light2D"; }
    std::string extra() const override {
        return Node2D::extra() + " radius=" + std::to_string((int)radius);
    }
    std::unique_ptr<Node> cloneNode() const override {
        auto c = std::make_unique<Light2D>();
        copyBase(*c); copyNode2D(*c);
        c->radius = radius; c->intensity = intensity;
        return c;
    }
};

} // namespace suka
