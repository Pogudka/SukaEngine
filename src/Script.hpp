#pragma once

#include <string>
#include <map>
#include <memory>
#include <iostream>

#include "LuaVM.hpp"
#include "Json.hpp"
#include "Scene.hpp"
#include "Sound.hpp"

namespace suka {

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
        if (!fileExists(path)) {
            std::cout << "[Scripts] no scripts.json\n";
            return;
        }

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
            if (!ent.vm->load(src)) {
                std::cout << "[Scripts] parse error: " << script << "\n";
                continue;
            }
            registerNatives(*ent.vm);
            entries_[node] = std::move(ent);
            std::cout << "[Scripts] loaded: " << node << " -> " << script << "\n";
        }
    }

    void update(Scene& scene, Context& ctx, double dt,
                SceneManager& scenes, SoundManager& sound,
                std::map<std::string, double>& vars) {
        for (auto& kv : entries_) {
            Node* n = scene.root ? scene.root->findNode(kv.first) : nullptr;
            Node2D* n2 = n ? dynamic_cast<Node2D*>(n) : nullptr;
            if (!n2) continue;

            host_.node = n2;
            host_.ctx = &ctx;
            host_.scenes = &scenes;
            host_.sound = &sound;
            host_.vars = &vars;
            kv.second.vm->host = &host_;

            if (!kv.second.started) {
                kv.second.vm->call("on_start", {});
                kv.second.started = true;
            }
            kv.second.vm->call("on_update", { LuaValue::numV(dt) });
        }
    }

private:
    void registerNatives(LuaVM& vm) {
        vm.setNative("print", [](LuaVM& v, std::vector<LuaValue>& a) {
            std::string s;
            for (auto& x : a) s += x.toString();
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
            if (h && h->vars && !a.empty()) {
                auto it = h->vars->find(a[0].str);
                if (it != h->vars->end()) return LuaValue::numV(it->second);
            }
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
    }

    struct Entry {
        std::unique_ptr<LuaVM> vm;
        std::string path;
        bool started = false;
    };

    std::map<std::string, Entry> entries_;
    std::string root_;
    ScriptHost host_;
};

} // namespace suka