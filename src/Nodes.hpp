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

    Node* addChild(std::unique_ptr<Node> child) {
        Node* ptr = child.get();
        children.push_back(std::move(child));
        return ptr;
    }

    // HIER-FIX: перестроение дерева (нужно для attach/detach из Lua)
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

    const std::vector<std::unique_ptr<Node>>& getChildren() const {
        return children;
    }

private:
    std::vector<std::unique_ptr<Node>> children;
};

class Node2D : public Node {
public:
    Vec2 position;
    Vec2 scale{1.0f, 1.0f};
    float rotation = 0.0f;

    // внешний вид (v0.15.0)
    std::string shape = "none";   // none square circle diamond triangle
    unsigned int color = 0xFFFFFFFF;
    std::string texture;          // если задана — рисуем текстуру вместо формы
    float w = 32.0f, h = 32.0f;

    bool hasAppearance() const {
        return shape != "none" || !texture.empty();
    }

    const char* typeName() const override { return "Node2D"; }

    std::string extra() const override {
        return " pos=(" + std::to_string((int)position.x) + "," +
               std::to_string((int)position.y) + ")";
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
        return Node2D::extra() + " text='" + text +
               "' font=" + fontName + ":" + std::to_string(fontSize);
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
        return Node2D::extra() + " texture='" + texturePath + "' " +
               (textureLoaded ? "[loaded]" : "[missing]");
    }

    void loadResources(ResourceManager& rm, const std::string& projectRoot) override {
        if (!texturePath.empty() && texturePath[0] == '/') {
            resolvedPath = texturePath;
        } else {
            resolvedPath = projectRoot + "/" + texturePath;
        }
        textureLoaded = rm.loadTexture(resolvedPath);
        Node2D::loadResources(rm, projectRoot);
    }
};

class Solid2D : public Node2D {
public:
    Solid2D() {
        w = 64.0f;
        h = 64.0f;
        shape = "square";
        color = 0x808080FF;
    }

    const char* typeName() const override { return "Solid2D"; }

    std::string extra() const override {
        return Node2D::extra() + " size=(" + std::to_string((int)w) + "," +
               std::to_string((int)h) + ")";
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

    Player() {
        shape = "square";
        color = 0x40C040FF;
    }

    const char* typeName() const override { return "Player"; }

    std::string extra() const override {
        return Node2D::extra() + " mode=" + mode;
    }

    void update(Context& ctx, double dt) override {
        if (mode == "platformer") {
            ctx.playerPos = position;
            ctx.hasPlayer = true;
            Node2D::update(ctx, dt);
            return;
        }

        position.x += ctx.input.joystickX * speed * float(dt);
        position.y += ctx.input.joystickY * speed * float(dt);

        if (ctx.input.jumpPressed && !jumpHeld) {
            vy = -300.0f;
            jumpHeld = true;
            ctx.jumpPressedThisFrame = true;
            std::cout << "  Player: jump!\n";
        }
        if (!ctx.input.jumpPressed) jumpHeld = false;

        position.y += vy * float(dt);
        vy *= 0.9f;

        ctx.playerPos = position;
        ctx.hasPlayer = true;

        Node2D::update(ctx, dt);
    }
};

class Enemy : public Node2D {
public:
    float speed = 80.0f;

    Enemy() {
        shape = "square";
        color = 0xC04040FF;
    }

    const char* typeName() const override { return "Enemy"; }

    void update(Context& ctx, double dt) override {
        if (ctx.hasPlayer) {
            float dx = ctx.playerPos.x - position.x;
            float dy = ctx.playerPos.y - position.y;
            float len = std::sqrt(dx * dx + dy * dy);

            if (len > 1.0f) {
                position.x += dx / len * speed * float(dt);
                position.y += dy / len * speed * float(dt);
            }
        }
        Node2D::update(ctx, dt);
    }
};

class Coin : public Node2D {
public:
    float radius = 24.0f;

    Coin() {
        shape = "circle";
        color = 0xE0C040FF;
        w = 32.0f;
        h = 32.0f;
    }

    const char* typeName() const override { return "Coin"; }

    void update(Context& ctx, double dt) override {
        if (ctx.hasPlayer && !dead) {
            float dx = ctx.playerPos.x - position.x;
            float dy = ctx.playerPos.y - position.y;

            if (std::sqrt(dx * dx + dy * dy) < radius) {
                dead = true;
                ctx.score += 1;
                ctx.coinCollectedThisFrame = true;
                std::cout << "  Coin '" << name << "' collected! score=" << ctx.score << "\n";
            }
        }
        Node2D::update(ctx, dt);
    }
};

class Camera2D : public Node2D {
public:
    std::string followName;
    Node2D* target = nullptr;
    float zoom = 1.0f;
    float followSpeed = 5.0f;

    const char* typeName() const override { return "Camera2D"; }

    std::string extra() const override {
        return Node2D::extra() + " follow='" + followName + "'";
    }

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
};

} // namespace suka
