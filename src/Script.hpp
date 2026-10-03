#pragma once

#include <string>
#include <vector>
#include <map>
#include <cstdio>
#include <cstring>
#include <cmath>

#include "Core.hpp"
#include "Scene.hpp"
#include "Resources.hpp"
#include "Tween.hpp"

// -----------------------------------------------------------------------------
// Auto-detect Lua headers.
//
// If Lua headers are available, real Lua scripting is enabled.
// If not, ScriptSystem becomes a safe stub so the project still builds.
// -----------------------------------------------------------------------------

#ifndef SUKA_HAS_LUA
# if defined(__has_include)
#  if __has_include(<lua.h>) && __has_include(<lauxlib.h>) && __has_include(<lualib.h>)
#   define SUKA_HAS_LUA 1
#   define SUKA_LUA_MODE 1
#  elif __has_include("lua/lua.h") && __has_include("lua/lauxlib.h") && __has_include("lua/lualib.h")
#   define SUKA_HAS_LUA 1
#   define SUKA_LUA_MODE 2
#  elif __has_include("../third_party/lua/lua.h") && __has_include("../third_party/lua/lauxlib.h") && __has_include("../third_party/lua/lualib.h")
#   define SUKA_HAS_LUA 1
#   define SUKA_LUA_MODE 3
#  elif __has_include("third_party/lua/lua.h") && __has_include("third_party/lua/lauxlib.h") && __has_include("third_party/lua/lualib.h")
#   define SUKA_HAS_LUA 1
#   define SUKA_LUA_MODE 4
#  else
#   define SUKA_HAS_LUA 0
#  endif
# else
#  define SUKA_HAS_LUA 0
# endif
#endif

#if SUKA_HAS_LUA
# if !defined(SUKA_LUA_MODE)
#  define SUKA_LUA_MODE 1
# endif

extern "C" {
# if SUKA_LUA_MODE == 1
#  include <lua.h>
#  include <lauxlib.h>
#  include <lualib.h>
# elif SUKA_LUA_MODE == 2
#  include "lua/lua.h"
#  include "lua/lauxlib.h"
#  include "lua/lualib.h"
# elif SUKA_LUA_MODE == 3
#  include "../third_party/lua/lua.h"
#  include "../third_party/lua/lauxlib.h"
#  include "../third_party/lua/lualib.h"
# elif SUKA_LUA_MODE == 4
#  include "third_party/lua/lua.h"
#  include "third_party/lua/lauxlib.h"
#  include "third_party/lua/lualib.h"
# endif
}
#endif

namespace suka {

inline std::vector<std::string> g_luaLog;

#if SUKA_HAS_LUA

inline Context* g_scriptCtx = nullptr;
inline Scene* g_scriptScene = nullptr;
inline SceneManager* g_scriptMgr = nullptr;
inline std::map<std::string, double>* g_scriptVars = nullptr;

static Node2D* scriptFindNode(const char* name) {
    if (!g_scriptScene || !g_scriptScene->root || !name) {
        return nullptr;
    }

    Node* n = g_scriptScene->root->findNode(name);
    return dynamic_cast<Node2D*>(n);
}

static int lua_print(lua_State* L) {
    int n = lua_gettop(L);
    std::string line;

    for (int i = 1; i <= n; ++i) {
        size_t len = 0;
        const char* s = luaL_tolstring(L, i, &len);

        if (i > 1) {
            line += "\t";
        }

        line += s;
        lua_pop(L, 1);
    }

    g_luaLog.push_back(line);

    if (g_luaLog.size() > 64) {
        g_luaLog.erase(g_luaLog.begin());
    }

    return 0;
}

static int lua_get_var(lua_State* L) {
    const char* name = luaL_checkstring(L, 1);

    if (g_scriptVars) {
        auto it = g_scriptVars->find(name);

        if (it != g_scriptVars->end()) {
            lua_pushnumber(L, it->second);
            return 1;
        }
    }

    lua_pushnumber(L, 0);
    return 1;
}

static int lua_set_var(lua_State* L) {
    const char* name = luaL_checkstring(L, 1);
    double v = luaL_checknumber(L, 2);

    if (g_scriptVars) {
        (*g_scriptVars)[name] = v;
    }

    return 0;
}

static int lua_add_var(lua_State* L) {
    const char* name = luaL_checkstring(L, 1);
    double v = luaL_checknumber(L, 2);

    if (g_scriptVars) {
        (*g_scriptVars)[name] += v;
    }

    return 0;
}

static int lua_get_score(lua_State* L) {
    lua_pushinteger(L, g_scriptCtx ? g_scriptCtx->score : 0);
    return 1;
}

static int lua_add_score(lua_State* L) {
    int v = (int)luaL_checkinteger(L, 1);

    if (g_scriptCtx) {
        g_scriptCtx->score += v;
    }

    return 0;
}

static int lua_coin_collected(lua_State* L) {
    if (g_scriptCtx) {
        g_scriptCtx->coinCollectedThisFrame = true;
    }

    return 0;
}

static int lua_jump_pressed(lua_State* L) {
    if (g_scriptCtx) {
        g_scriptCtx->jumpPressedThisFrame = true;
    }

    return 0;
}

static int lua_node_exists(lua_State* L) {
    const char* name = luaL_checkstring(L, 1);

    lua_pushboolean(L, scriptFindNode(name) != nullptr);
    return 1;
}

static int lua_get_node_x(lua_State* L) {
    Node2D* n = scriptFindNode(luaL_checkstring(L, 1));

    lua_pushnumber(L, n ? n->position.x : 0);
    return 1;
}

static int lua_set_node_x(lua_State* L) {
    Node2D* n = scriptFindNode(luaL_checkstring(L, 1));

    if (n) {
        n->position.x = (float)luaL_checknumber(L, 2);
    }

    return 0;
}

static int lua_get_node_y(lua_State* L) {
    Node2D* n = scriptFindNode(luaL_checkstring(L, 1));

    lua_pushnumber(L, n ? n->position.y : 0);
    return 1;
}

static int lua_set_node_y(lua_State* L) {
    Node2D* n = scriptFindNode(luaL_checkstring(L, 1));

    if (n) {
        n->position.y = (float)luaL_checknumber(L, 2);
    }

    return 0;
}

static int lua_get_node_rotation(lua_State* L) {
    Node2D* n = scriptFindNode(luaL_checkstring(L, 1));

    lua_pushnumber(L, n ? n->rotation * 180.0 / TWEEN_PI : 0);
    return 1;
}

static int lua_set_node_rotation(lua_State* L) {
    Node2D* n = scriptFindNode(luaL_checkstring(L, 1));

    if (n) {
        n->rotation = (float)(luaL_checknumber(L, 2) * TWEEN_PI / 180.0);
    }

    return 0;
}

static int lua_get_node_scale(lua_State* L) {
    Node2D* n = scriptFindNode(luaL_checkstring(L, 1));

    lua_pushnumber(L, n ? (n->scale.x + n->scale.y) * 0.5 : 1);
    return 1;
}

static int lua_set_node_scale(lua_State* L) {
    Node2D* n = scriptFindNode(luaL_checkstring(L, 1));
    float s = (float)luaL_checknumber(L, 2);

    if (n) {
        n->scale.x = s;
        n->scale.y = s;
    }

    return 0;
}

static int lua_get_node_alpha(lua_State* L) {
    Node2D* n = scriptFindNode(luaL_checkstring(L, 1));

    lua_pushnumber(L, n ? n->alpha : 1);
    return 1;
}

static int lua_set_node_alpha(lua_State* L) {
    Node2D* n = scriptFindNode(luaL_checkstring(L, 1));

    if (n) {
        n->alpha = (float)luaL_checknumber(L, 2);
    }

    return 0;
}

static int lua_get_node_width(lua_State* L) {
    Node2D* n = scriptFindNode(luaL_checkstring(L, 1));

    lua_pushnumber(L, n ? n->w : 0);
    return 1;
}

static int lua_set_node_width(lua_State* L) {
    Node2D* n = scriptFindNode(luaL_checkstring(L, 1));

    if (n) {
        n->w = (float)luaL_checknumber(L, 2);
    }

    return 0;
}

static int lua_get_node_height(lua_State* L) {
    Node2D* n = scriptFindNode(luaL_checkstring(L, 1));

    lua_pushnumber(L, n ? n->h : 0);
    return 1;
}

static int lua_set_node_height(lua_State* L) {
    Node2D* n = scriptFindNode(luaL_checkstring(L, 1));

    if (n) {
        n->h = (float)luaL_checknumber(L, 2);
    }

    return 0;
}

static int lua_change_scene(lua_State* L) {
    const char* path = luaL_checkstring(L, 1);

    if (g_scriptMgr) {
        g_scriptMgr->requestChange(path, false);
    }

    return 0;
}

static int lua_restart_scene(lua_State* L) {
    const char* path = luaL_checkstring(L, 1);

    if (g_scriptMgr) {
        g_scriptMgr->requestChange(path, true);
    }

    return 0;
}

static int lua_tween_to(lua_State* L) {
    const char* node = luaL_checkstring(L, 1);
    const char* prop = luaL_checkstring(L, 2);

    double to = luaL_checknumber(L, 3);
    float dur = (float)luaL_checknumber(L, 4);

    const char* easing = "linear";
    bool loop = false;
    bool yoyo = false;
    const char* onComplete = "";

    int top = lua_gettop(L);

    if (top >= 5 && lua_isstring(L, 5)) {
        easing = lua_tostring(L, 5);
    }

    if (top >= 6) {
        loop = lua_toboolean(L, 6) != 0;
    }

    if (top >= 7) {
        yoyo = lua_toboolean(L, 7) != 0;
    }

    if (top >= 8 && lua_isstring(L, 8)) {
        onComplete = lua_tostring(L, 8);
    }

    int id = g_tweens.add(
        node ? node : "",
        prop ? prop : "",
        to,
        dur,
        easing ? easing : "linear",
        loop,
        yoyo,
        onComplete ? onComplete : ""
    );

    lua_pushinteger(L, id);
    return 1;
}

static int lua_tween_stop(lua_State* L) {
    g_tweens.stop((int)luaL_checkinteger(L, 1));
    return 0;
}

static int lua_tween_stop_node(lua_State* L) {
    g_tweens.stopNode(luaL_checkstring(L, 1));
    return 0;
}

static int lua_tween_clear(lua_State* L) {
    g_tweens.clear();
    return 0;
}

static int lua_tween_count(lua_State* L) {
    lua_pushinteger(L, (lua_Integer)g_tweens.count());
    return 1;
}

static int lua_tween_is_active(lua_State* L) {
    lua_pushboolean(L, g_tweens.isActive((int)luaL_checkinteger(L, 1)));
    return 1;
}

class ScriptSystem {
public:
    ~ScriptSystem() {
        if (L_) {
            lua_close(L_);
        }
    }

    void load(const std::string& root) {
        if (L_) {
            lua_close(L_);
            L_ = nullptr;
        }

        L_ = luaL_newstate();
        luaL_openlibs(L_);
        registerFunctions();

        std::string dir = root + "/scripts";
        auto entries = FileBrowser::list(dir);

        std::string combined;

        for (auto& e : entries) {
            if (e.isDir) {
                continue;
            }

            if (e.name.size() < 4 ||
                e.name.compare(e.name.size() - 4, 4, ".lua") != 0) {
                continue;
            }

            std::string path = dir + "/" + e.name;

            combined += "-- " + e.name + "\n";
            combined += readFile(path);
            combined += "\n\n";
        }

        if (!combined.empty()) {
            if (luaL_dostring(L_, combined.c_str()) != LUA_OK) {
                const char* err = lua_tostring(L_, -1);

                g_luaLog.push_back(
                    std::string("lua load error: ") + (err ? err : "?")
                );

                lua_pop(L_, 1);
            }
        }

        started_ = false;
    }

    void update(
        Scene& scene,
        Context& ctx,
        float dt,
        SceneManager& mgr,
        std::map<std::string, double>& vars
    ) {
        if (!L_) {
            return;
        }

        g_scriptCtx = &ctx;
        g_scriptScene = &scene;
        g_scriptMgr = &mgr;
        g_scriptVars = &vars;

        if (!started_) {
            callFunction("on_start");
            started_ = true;
        }

        callFunctionWithDt("on_update", dt);
    }

    void callGlobal(
        const std::string& fn,
        Context& ctx,
        SceneManager& mgr,
        std::map<std::string, double>& vars,
        Scene* scene = nullptr
    ) {
        if (!L_ || fn.empty()) {
            return;
        }

        g_scriptCtx = &ctx;
        g_scriptScene = scene;
        g_scriptMgr = &mgr;
        g_scriptVars = &vars;

        callFunction(fn);
    }

private:
    void registerFunctions() {
        lua_register(L_, "print", lua_print);

        lua_register(L_, "get_var", lua_get_var);
        lua_register(L_, "set_var", lua_set_var);
        lua_register(L_, "add_var", lua_add_var);

        lua_register(L_, "get_score", lua_get_score);
        lua_register(L_, "add_score", lua_add_score);

        lua_register(L_, "coin_collected", lua_coin_collected);
        lua_register(L_, "jump_pressed", lua_jump_pressed);

        lua_register(L_, "node_exists", lua_node_exists);

        lua_register(L_, "get_node_x", lua_get_node_x);
        lua_register(L_, "set_node_x", lua_set_node_x);

        lua_register(L_, "get_node_y", lua_get_node_y);
        lua_register(L_, "set_node_y", lua_set_node_y);

        lua_register(L_, "get_node_rotation", lua_get_node_rotation);
        lua_register(L_, "set_node_rotation", lua_set_node_rotation);

        lua_register(L_, "get_node_scale", lua_get_node_scale);
        lua_register(L_, "set_node_scale", lua_set_node_scale);

        lua_register(L_, "get_node_alpha", lua_get_node_alpha);
        lua_register(L_, "set_node_alpha", lua_set_node_alpha);

        lua_register(L_, "get_node_width", lua_get_node_width);
        lua_register(L_, "set_node_width", lua_set_node_width);

        lua_register(L_, "get_node_height", lua_get_node_height);
        lua_register(L_, "set_node_height", lua_set_node_height);

        lua_register(L_, "change_scene", lua_change_scene);
        lua_register(L_, "restart_scene", lua_restart_scene);

        lua_register(L_, "tween_to", lua_tween_to);
        lua_register(L_, "tween_stop", lua_tween_stop);
        lua_register(L_, "tween_stop_node", lua_tween_stop_node);
        lua_register(L_, "tween_clear", lua_tween_clear);
        lua_register(L_, "tween_count", lua_tween_count);
        lua_register(L_, "tween_is_active", lua_tween_is_active);
    }

    void callFunction(const std::string& fn) {
        lua_getglobal(L_, fn.c_str());

        if (lua_isfunction(L_, -1)) {
            if (lua_pcall(L_, 0, 0, 0) != LUA_OK) {
                const char* err = lua_tostring(L_, -1);

                g_luaLog.push_back(
                    fn + " error: " + (err ? err : "?")
                );

                lua_pop(L_, 1);
            }
        } else {
            lua_pop(L_, 1);
        }
    }

    void callFunctionWithDt(const std::string& fn, float dt) {
        lua_getglobal(L_, fn.c_str());

        if (lua_isfunction(L_, -1)) {
            lua_pushnumber(L_, dt);

            if (lua_pcall(L_, 1, 0, 0) != LUA_OK) {
                const char* err = lua_tostring(L_, -1);

                g_luaLog.push_back(
                    fn + " error: " + (err ? err : "?")
                );

                lua_pop(L_, 1);
            }
        } else {
            lua_pop(L_, 1);
        }
    }

    lua_State* L_ = nullptr;
    bool started_ = false;
};

#else

// Safe no-Lua fallback.
// The build succeeds, but .lua scripts are ignored.
class ScriptSystem {
public:
    void load(const std::string&) {}

    void update(
        Scene&,
        Context&,
        float,
        SceneManager&,
        std::map<std::string, double>&
    ) {}

    void callGlobal(
        const std::string&,
        Context&,
        SceneManager&,
        std::map<std::string, double>&,
        Scene* = nullptr
    ) {}
};

#endif

} // namespace suka
