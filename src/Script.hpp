#pragma once

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <functional>
#include <iostream>
#include <cmath>

#include "LuaVM.hpp"
#include "Json.hpp"
#include "Scene.hpp"
#include "Sound.hpp"

namespace suka {

// LOG-FIX: экранный лог Lua (print и служебные сообщения)
inline std::vector<std::string> g_luaLog;
inline void luaLog(const std::string& s) {
    g_luaLog.push_back(s);
    if (g_luaLog.size() > 80) g_luaLog.erase(g_luaLog.begin());
}

struct ScriptHost {
    Node2D* node = nullptr;
    Context* ctx = nullptr;
    SceneManager* scenes = nullptr;
    SoundManager* sound = nullptr;
    std::map<std::string, double>* vars = nullptr;
};

class ScriptSystem {
public:
    void load(const std::string& projectRoot) {
        root_ = projectRoot;
        std::string path = projectRoot + "/scripts.json";
        if (!fileExists(path)) { std::cout << "[Scripts] no scripts.json\n"; return; }
        std::string json = readFile(path);
        size_t b = 0, e = 0;
        if (!jsonFindArray(json, "scripts", b, e)) return;
        for (const auto& obj : jsonSplitObjects(json.substr(b, e - b + 1))) {
            std::string node, script;
            jsonGetString(obj, "node", node);
            jsonGetString(obj, "path", script);
            if (node.empty() || script.empty()) continue;
            Entry ent;
            ent.vm = std::make_unique<LuaVM>();
            std::string src = readFile(projectRoot + "/" + script);
            if (!ent.vm->load(src)) { luaLog("parse error: " + script); continue; }
            registerNatives(*ent.vm);
            entries_[node] = std::move(ent);
            luaLog("loaded: " + node + " -> " + script);
        }
    }

    void update(Scene& scene, Context& ctx, double dt,
                SceneManager& scenes, SoundManager& sound,
                std::map<std::string, double>& vars) {
        updateImpl(scene, ctx, dt, scenes, &sound, vars);
    }
    void update(Scene& scene, Context& ctx, double dt,
                SceneManager& scenes,
                std::map<std::string, double>& vars) {
        updateImpl(scene, ctx, dt, scenes, nullptr, vars);
    }

    bool callGlobal(const std::string& fn, Context& ctx, SceneManager& scenes,
                    std::map<std::string, double>& vars) {
        host_.node = nullptr; host_.ctx = &ctx; host_.scenes = &scenes;
        host_.sound = nullptr; host_.vars = &vars;
        for (auto& kv : entries_) {
            if (kv.second.vm->has(fn)) {
                kv.second.vm->host = &host_;
                kv.second.vm->call(fn, {});
                return true;
            }
        }
        luaLog("call: no function '" + fn + "'");
        return false;
    }

private:
    void updateImpl(Scene& scene, Context& ctx, double dt,
                    SceneManager& scenes, SoundManager* sound,
                    std::map<std::string, double>& vars) {
        for (auto& kv : entries_) {
            Node* n = scene.root ? scene.root->findNode(kv.first) : nullptr;
            Node2D* n2 = n ? dynamic_cast<Node2D*>(n) : nullptr;
            if (!n2) continue;
            host_.node = n2; host_.ctx = &ctx; host_.scenes = &scenes;
            host_.sound = sound; host_.vars = &vars;
            kv.second.vm->host = &host_;
            if (!kv.second.started) { kv.second.vm->call("on_start", {}); kv.second.started = true; }
            kv.second.vm->call("on_update", { LuaValue::numV(dt) });
        }
    }

    static Node* findAny(LuaVM& v, const std::string& name) {
        auto* h = static_cast<ScriptHost*>(v.host);
        if (!h || !h->scenes) return nullptr;
        Scene* sc = h->scenes->current();
        if (!sc || !sc->root) return nullptr;
        return sc->root->findNode(name);
    }
    static Node2D* findAny2D(LuaVM& v, const std::string& name) {
        Node* n = findAny(v, name);
        return n ? dynamic_cast<Node2D*>(n) : nullptr;
    }
    static void collectNames(const Node* n, std::vector<std::string>& out) {
        if (!n) return;
        out.push_back(n->name);
        for (const auto& ch : n->getChildren()) collectNames(ch.get(), out);
    }

    void registerNatives(LuaVM& vm) {
        vm.setNative("print", [](LuaVM& v, std::vector<LuaValue>& a) {
            std::string s; for (auto& x : a) s += x.toString();
            luaLog(s);                       // LOG-FIX: на экран
            std::cout << "[Lua] " << s << "\n";
            return LuaValue();
        });
        vm.setNative("get_x", [](LuaVM& v, std::vector<LuaValue>& a) {
            auto* h = static_cast<ScriptHost*>(v.host);
            return LuaValue::numV(h && h->node ? h->node->position.x : 0);
        });
        vm.setNative("get_y", [](LuaVM& v, std::vector<LuaValue>& a) {
            auto* h = static_cast<ScriptHost*>(v.host);
            return LuaValue::numV(h && h->node ? h->node->position.y : 0);
        });
        vm.setNative("set_x", [](LuaVM& v, std::vector<LuaValue>& a) {
            auto* h = static_cast<ScriptHost*>(v.host);
            if (h && h->node && !a.empty()) h->node->position.x = (float)a[0].num;
            return LuaValue();
        });
        vm.setNative("set_y", [](LuaVM& v, std::vector<LuaValue>& a) {
            auto* h = static_cast<ScriptHost*>(v.host);
            if (h && h->node && !a.empty()) h->node->position.y = (float)a[0].num;
            return LuaValue();
        });
        vm.setNative("get_var", [](LuaVM& v, std::vector<LuaValue>& a) {
            auto* h = static_cast<ScriptHost*>(v.host);
            if (h && h->vars && !a.empty()) { auto it = h->vars->find(a[0].str); if (it != h->vars->end()) return LuaValue::numV(it->second); }
            return LuaValue::numV(0);
        });
        vm.setNative("set_var", [](LuaVM& v, std::vector<LuaValue>& a) {
            auto* h = static_cast<ScriptHost*>(v.host);
            if (h && h->vars && a.size() >= 2) (*h->vars)[a[0].str] = a[1].num;
            return LuaValue();
        });
        vm.setNative("add_var", [](LuaVM& v, std::vector<LuaValue>& a) {
            auto* h = static_cast<ScriptHost*>(v.host);
            if (h && h->vars && a.size() >= 2) (*h->vars)[a[0].str] += a[1].num;
            return LuaValue();
        });
        vm.setNative("input_joy_x", [](LuaVM& v, std::vector<LuaValue>& a) {
            auto* h = static_cast<ScriptHost*>(v.host);
            return LuaValue::numV(h && h->ctx ? h->ctx->input.joystickX : 0);
        });
        vm.setNative("input_joy_y", [](LuaVM& v, std::vector<LuaValue>& a) {
            auto* h = static_cast<ScriptHost*>(v.host);
            return LuaValue::numV(h && h->ctx ? h->ctx->input.joystickY : 0);
        });
        vm.setNative("input_pressed", [](LuaVM& v, std::vector<LuaValue>& a) {
            auto* h = static_cast<ScriptHost*>(v.host);
            if (!h || !h->ctx || a.empty()) return LuaValue::boolV(false);
            bool r = false;
            if (a[0].str == "jump") r = h->ctx->input.jumpPressed;
            if (a[0].str == "attack") r = h->ctx->input.attackPressed;
            return LuaValue::boolV(r);
        });
        vm.setNative("play_sound", [](LuaVM& v, std::vector<LuaValue>& a) {
            auto* h = static_cast<ScriptHost*>(v.host);
            if (h && h->sound && !a.empty()) h->sound->play(a[0].str);
            return LuaValue();
        });
        vm.setNative("change_scene", [](LuaVM& v, std::vector<LuaValue>& a) {
            auto* h = static_cast<ScriptHost*>(v.host);
            if (h && h->scenes && !a.empty()) h->scenes->requestChange(a[0].str, false);
            return LuaValue();
        });
        vm.setNative("restart_scene", [](LuaVM& v, std::vector<LuaValue>& a) {
            auto* h = static_cast<ScriptHost*>(v.host);
            if (h && h->scenes && !a.empty()) h->scenes->requestChange(a[0].str, true);
            return LuaValue();
        });
        vm.setNative("tostring", [](LuaVM& v, std::vector<LuaValue>& a) {
            return LuaValue::strV(a.empty() ? std::string("nil") : a[0].toString());
        });
        vm.setNative("tonumber", [](LuaVM& v, std::vector<LuaValue>& a) {
            if (a.empty()) return LuaValue::numV(0);
            if (a[0].type == LuaValue::Num) return a[0];
            if (a[0].type == LuaValue::Str) return LuaValue::numV(atof(a[0].str.c_str()));
            if (a[0].type == LuaValue::Bool) return LuaValue::numV(a[0].boolean ? 1 : 0);
            return LuaValue::numV(0);
        });
        vm.setNative("node_exists", [](LuaVM& v, std::vector<LuaValue>& a) {
            if (a.empty()) return LuaValue::boolV(false);
            return LuaValue::boolV(findAny(v, a[0].str) != nullptr);
        });
        vm.setNative("count_nodes", [](LuaVM& v, std::vector<LuaValue>& a) {
            auto* h = static_cast<ScriptHost*>(v.host);
            Scene* sc = (h && h->scenes) ? h->scenes->current() : nullptr;
            std::vector<std::string> names; if (sc && sc->root) collectNames(sc->root.get(), names);
            return LuaValue::numV((double)names.size());
        });
        vm.setNative("node_at", [](LuaVM& v, std::vector<LuaValue>& a) {
            auto* h = static_cast<ScriptHost*>(v.host);
            Scene* sc = (h && h->scenes) ? h->scenes->current() : nullptr;
            std::vector<std::string> names; if (sc && sc->root) collectNames(sc->root.get(), names);
            int i = a.empty() ? -1 : (int)a[0].num;
            if (i < 0 || i >= (int)names.size()) return LuaValue::strV("");
            return LuaValue::strV(names[i]);
        });
        vm.setNative("get_node_x", [](LuaVM& v, std::vector<LuaValue>& a) {
            Node2D* n = a.empty() ? nullptr : findAny2D(v, a[0].str);
            return LuaValue::numV(n ? n->position.x : 0);
        });
        vm.setNative("get_node_y", [](LuaVM& v, std::vector<LuaValue>& a) {
            Node2D* n = a.empty() ? nullptr : findAny2D(v, a[0].str);
            return LuaValue::numV(n ? n->position.y : 0);
        });
        vm.setNative("get_node_w", [](LuaVM& v, std::vector<LuaValue>& a) {
            Node2D* n = a.empty() ? nullptr : findAny2D(v, a[0].str);
            return LuaValue::numV(n ? n->w : 0);
        });
        vm.setNative("get_node_h", [](LuaVM& v, std::vector<LuaValue>& a) {
            Node2D* n = a.empty() ? nullptr : findAny2D(v, a[0].str);
            return LuaValue::numV(n ? n->h : 0);
        });
        vm.setNative("set_node_pos", [](LuaVM& v, std::vector<LuaValue>& a) {
            if (a.size() < 3) return LuaValue();
            Node2D* n = findAny2D(v, a[0].str);
            if (n) { n->position.x = (float)a[1].num; n->position.y = (float)a[2].num; }
            return LuaValue();
        });
        vm.setNative("move_node", [](LuaVM& v, std::vector<LuaValue>& a) {
            if (a.size() < 3) return LuaValue();
            Node2D* n = findAny2D(v, a[0].str);
            if (n) { n->position.x += (float)a[1].num; n->position.y += (float)a[2].num; }
            return LuaValue();
        });
        vm.setNative("set_scale", [](LuaVM& v, std::vector<LuaValue>& a) {
            if (a.size() < 3) return LuaValue();
            Node2D* n = findAny2D(v, a[0].str);
            if (n) { n->scale.x = (float)a[1].num; n->scale.y = (float)a[2].num; }
            return LuaValue();
        });
        vm.setNative("set_rot", [](LuaVM& v, std::vector<LuaValue>& a) {
            if (a.size() < 2) return LuaValue();
            Node2D* n = findAny2D(v, a[0].str);
            if (n) n->rotation = (float)(a[1].num * 3.14159265 / 180.0);
            return LuaValue();
        });
        vm.setNative("set_color", [](LuaVM& v, std::vector<LuaValue>& a) {
            if (a.size() < 2) return LuaValue();
            Node2D* n = findAny2D(v, a[0].str);
            if (n) n->color = parseColor(a[1].str);
            return LuaValue();
        });
        vm.setNative("set_shape", [](LuaVM& v, std::vector<LuaValue>& a) {
            if (a.size() < 2) return LuaValue();
            Node2D* n = findAny2D(v, a[0].str);
            if (n) n->shape = a[1].str;
            return LuaValue();
        });
        vm.setNative("set_text", [](LuaVM& v, std::vector<LuaValue>& a) {
            if (a.size() < 2) return LuaValue();
            Node* n = findAny(v, a[0].str);
            Label* l = n ? dynamic_cast<Label*>(n) : nullptr;
            if (l) l->text = a[1].str;
            return LuaValue();
        });
        vm.setNative("get_text", [](LuaVM& v, std::vector<LuaValue>& a) {
            if (a.empty()) return LuaValue::strV("");
            Node* n = findAny(v, a[0].str);
            Label* l = n ? dynamic_cast<Label*>(n) : nullptr;
            return LuaValue::strV(l ? l->text : std::string(""));
        });
        vm.setNative("destroy", [](LuaVM& v, std::vector<LuaValue>& a) {
            if (a.empty()) return LuaValue();
            Node* n = findAny(v, a[0].str);
            if (n) n->dead = true;
            return LuaValue();
        });
        vm.setNative("pair_destroy", [](LuaVM& v, std::vector<LuaValue>& a) {
            if (a.size() < 2) return LuaValue();
            Node* x = findAny(v, a[0].str); if (x) x->dead = true;
            Node* y = findAny(v, a[1].str); if (y) y->dead = true;
            return LuaValue();
        });
        vm.setNative("spawn", [](LuaVM& v, std::vector<LuaValue>& a) {
            auto* h = static_cast<ScriptHost*>(v.host);
            Scene* sc = (h && h->scenes) ? h->scenes->current() : nullptr;
            if (!sc || !sc->root || a.size() < 4) return LuaValue();
            std::string type = a[0].str, name = a[1].str;
            float x = (float)a[2].num, y = (float)a[3].num;
            std::unique_ptr<Node2D> nd;
            if      (type == "Label")    nd = std::make_unique<Label>();
            else if (type == "Sprite2D") nd = std::make_unique<Sprite2D>();
            else if (type == "Player")   nd = std::make_unique<Player>();
            else if (type == "Enemy")    nd = std::make_unique<Enemy>();
            else if (type == "Coin")     nd = std::make_unique<Coin>();
            else if (type == "Solid2D")  nd = std::make_unique<Solid2D>();
            else if (type == "Camera2D") nd = std::make_unique<Camera2D>();
            else if (type == "Light2D")  nd = std::make_unique<Light2D>();
            else                         nd = std::make_unique<Node2D>();
            nd->name = name; nd->position = Vec2{x, y};
            sc->root->addChild(std::move(nd));
            return LuaValue();
        });
        vm.setNative("distance", [](LuaVM& v, std::vector<LuaValue>& a) {
            if (a.size() < 2) return LuaValue::numV(0);
            Node2D* x = findAny2D(v, a[0].str); Node2D* y = findAny2D(v, a[1].str);
            if (!x || !y) return LuaValue::numV(0);
            double dx = x->position.x - y->position.x, dy = x->position.y - y->position.y;
            return LuaValue::numV(std::sqrt(dx*dx + dy*dy));
        });
        vm.setNative("overlaps", [](LuaVM& v, std::vector<LuaValue>& a) {
            if (a.size() < 2) return LuaValue::boolV(false);
            Node2D* x = findAny2D(v, a[0].str); Node2D* y = findAny2D(v, a[1].str);
            if (!x || !y) return LuaValue::boolV(false);
            float hx = x->w > 0 ? x->w/2 : 24, hy = x->h > 0 ? x->h/2 : 24;
            float gx = y->w > 0 ? y->w/2 : 24, gy = y->h > 0 ? y->h/2 : 24;
            bool o = (x->position.x - hx <= y->position.x + gx) && (x->position.x + hx >= y->position.x - gx) &&
                     (x->position.y - hy <= y->position.y + gy) && (x->position.y + hy >= y->position.y - gy);
            return LuaValue::boolV(o);
        });
        vm.setNative("attach", [](LuaVM& v, std::vector<LuaValue>& a) {
            if (a.size() < 2) return LuaValue::boolV(false);
            auto* h = static_cast<ScriptHost*>(v.host);
            Scene* sc = (h && h->scenes) ? h->scenes->current() : nullptr;
            if (!sc || !sc->root) return LuaValue::boolV(false);
            Node* root = sc->root.get();
            const std::string& child = a[0].str; const std::string& parent = a[1].str;
            if (child == parent) return LuaValue::boolV(false);
            Node* cn = root->findNode(child); Node* pn = root->findNode(parent);
            if (!cn || !pn) return LuaValue::boolV(false);
            if (cn->containsName(parent)) return LuaValue::boolV(false);
            Node* owner = root->findParentOf(child);
            if (!owner) return LuaValue::boolV(false);
            std::unique_ptr<Node> up = owner->takeChild(child);
            if (!up) return LuaValue::boolV(false);
            pn->addChild(std::move(up));
            return LuaValue::boolV(true);
        });
        vm.setNative("detach", [](LuaVM& v, std::vector<LuaValue>& a) {
            if (a.empty()) return LuaValue::boolV(false);
            auto* h = static_cast<ScriptHost*>(v.host);
            Scene* sc = (h && h->scenes) ? h->scenes->current() : nullptr;
            if (!sc || !sc->root) return LuaValue::boolV(false);
            Node* root = sc->root.get();
            Node* owner = root->findParentOf(a[0].str);
            if (!owner) return LuaValue::boolV(false);
            std::unique_ptr<Node> up = owner->takeChild(a[0].str);
            if (!up) return LuaValue::boolV(false);
            root->addChild(std::move(up));
            return LuaValue::boolV(true);
        });
        vm.setNative("parent_of", [](LuaVM& v, std::vector<LuaValue>& a) {
            if (a.empty()) return LuaValue::strV("");
            auto* h = static_cast<ScriptHost*>(v.host);
            Scene* sc = (h && h->scenes) ? h->scenes->current() : nullptr;
            if (!sc || !sc->root) return LuaValue::strV("");
            Node* p = sc->root->findParentOf(a[0].str);
            return LuaValue::strV(p ? p->name : std::string(""));
        });
        vm.setNative("children_count", [](LuaVM& v, std::vector<LuaValue>& a) {
            if (a.empty()) return LuaValue::numV(0);
            auto* h = static_cast<ScriptHost*>(v.host);
            Scene* sc = (h && h->scenes) ? h->scenes->current() : nullptr;
            if (!sc || !sc->root) return LuaValue::numV(0);
            Node* n = sc->root->findNode(a[0].str);
            return LuaValue::numV(n ? (double)n->childCount() : 0);
        });
        vm.setNative("child_at", [](LuaVM& v, std::vector<LuaValue>& a) {
            if (a.size() < 2) return LuaValue::strV("");
            auto* h = static_cast<ScriptHost*>(v.host);
            Scene* sc = (h && h->scenes) ? h->scenes->current() : nullptr;
            if (!sc || !sc->root) return LuaValue::strV("");
            Node* n = sc->root->findNode(a[0].str);
            if (!n) return LuaValue::strV("");
            int i = (int)a[1].num;
            const auto& ch = n->getChildren();
            if (i < 0 || i >= (int)ch.size()) return LuaValue::strV("");
            return LuaValue::strV(ch[i]->name);
        });
        vm.setNative("is_child_of", [](LuaVM& v, std::vector<LuaValue>& a) {
            if (a.size() < 2) return LuaValue::boolV(false);
            auto* h = static_cast<ScriptHost*>(v.host);
            Scene* sc = (h && h->scenes) ? h->scenes->current() : nullptr;
            if (!sc || !sc->root) return LuaValue::boolV(false);
            Node* p = sc->root->findParentOf(a[0].str);
            return LuaValue::boolV(p && p->name == a[1].str);
        });
    }

    struct Entry { std::unique_ptr<LuaVM> vm; std::string path; bool started = false; };
    std::map<std::string, Entry> entries_;
    std::string root_;
    ScriptHost host_;
};

} // namespace suka
