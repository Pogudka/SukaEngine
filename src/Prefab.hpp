#pragma once

#include <string>
#include <memory>
#include <vector>
#include "Scene.hpp"
#include "CommonTypes.hpp"

namespace suka {

// Forward — реализуется в SceneUtils/SceneLoader (см. список правок ниже).
bool loadPrefabTree(const std::string& projectRoot, const std::string& relPath,
                    std::unique_ptr<Node>& outRoot);

// Префаб: нода-контейнер, чьи дети создаются из JSON-файла.
// При сериализации сцены пишутся только базовые поля + sourcePath.
// Дети НЕ сериализуются — они всегда инстанциируются из source при загрузке.
class Prefab2D : public Node2D {
public:
    Prefab2D() { shape = "none"; color = 0x8E44ADFFu; w = 48; h = 48; }

    const char* typeName() const override { return "Prefab2D"; }

    // Относительный путь в проекте, например "prefabs/enemy.json".
    std::string sourcePath;

    // Загружает JSON из sourcePath и добавляет корневые ноды как своих детей.
    // Вызывается SceneLoader'ом после attach, и редактором после смены source.
    bool instantiate(const std::string& projectRoot) {
        // Удаляем старое инстансированное дерево.
        clearInstanceChildren();

        if (sourcePath.empty()) return false;

        std::unique_ptr<Node> loaded;
        if (!loadPrefabTree(projectRoot, sourcePath, loaded)) return false;
        if (!loaded) return false;

        // Переносим детей из загруженного корня в себя.
        auto kids = loaded->takeAllChildren();
        for (auto& k : kids) {
            if (k) addChild(std::move(k));
        }
        return true;
    }

    // Освобождает детей-инстанс.
    void clearInstanceChildren() {
        std::vector<std::string> names;
        for (const auto& c : getChildren()) names.push_back(c->name);
        for (const auto& nm : names) takeChild(nm);
    }

    // Сериализация: пишем только свои поля, без детей.
    void writeJson(std::ostream& os, int indent) const {
        std::string pad(indent, ' ');
        os << pad << "{\n";
        os << pad << "  \"type\": \"Prefab2D\",\n";
        os << pad << "  \"name\": \"" << jsonEscape(name) << "\",\n";
        os << pad << "  \"source\": \"" << jsonEscape(sourcePath) << "\",\n";
        os << pad << "  \"x\": " << position.x << ",\n";
        os << pad << "  \"y\": " << position.y << ",\n";
        os << pad << "  \"rotation\": " << rotation << ",\n";
        os << pad << "  \"scaleX\": " << scale.x << ",\n";
        os << pad << "  \"scaleY\": " << scale.y << ",\n";
        os << pad << "  \"alpha\": " << alpha << ",\n";
        os << pad << "  \"locked\": " << (locked ? "true" : "false") << "\n";
        os << pad << "}";
    }

    // Клонируем только "оболочку" префаба (без детей — они пересоздаются из source).
    std::unique_ptr<Node> cloneNode() const override {
        auto p = std::make_unique<Prefab2D>();
        p->name = name;
        p->sourcePath = sourcePath;
        p->position = position;
        p->rotation = rotation;
        p->scale = scale;
        p->alpha = alpha;
        p->locked = locked;
        p->color = color;
        p->w = w; p->h = h;
        return p;
    }

private:
    static std::string jsonEscape(const std::string& s) {
        std::string r;
        r.reserve(s.size());
        for (char c : s) {
            if (c == '\\' || c == '"') { r.push_back('\\'); r.push_back(c); }
            else if (c == '\n') r += "\\n";
            else if (c == '\r') r += "\\r";
            else if (c == '\t') r += "\\t";
            else r.push_back(c);
        }
        return r;
    }
};

} // namespace suka
