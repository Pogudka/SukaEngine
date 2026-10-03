#pragma once

#include <string>
#include <vector>
#include <map>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <dirent.h>

#include "Core.hpp"
#include "Scene.hpp"
#include "Resources.hpp"
#include "Tween.hpp"
#include "UiUtils.hpp"
#include "Particles.hpp"

// -----------------------------------------------------------------------------
// Auto-detect Lua headers.
//
// If Lua headers are available (they become available once CMake downloads and
// links Lua), real Lua scripting is enabled. If not, ScriptSystem is a safe
// stub so the project still builds.
// -----------------------------------------------------------------------------

#ifndef SUKA_HAS_LUA
# if defined(__has_include)
#  if __has_include(<lua.h>) && __has_include(<lauxlib.h>) && __has_include(<lualib.h>)
#   define SUKA_HAS_LUA 1
#   define SUKA_LUA_MODE 1
#  elif __has_include("lua/lua.h") && __has_include("lua/lauxlib.h") && __has_include("lua/lualib.h")
#   define SUKA_HAS_LUA 1
#   define SUKA_LUA_MODE 2
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

// List *.lua files in a directory without depending on any FileBrowser type.
static std::vector<std::string> listLuaFiles(const std::string& dir) {
    std::vector<std::string> out;

    DIR* d = opendir(dir.c_str());
    if (!d) {
        return out;
    }

    struct dirent* e;

    while ((e = readdir(d)) != nullptr) {
        std::string n = e->d_name;

        if (n == "." || n == "..") {
            continue;
        }

        if (n.size() >= 4 && n.compare(n.size() - 4, 4, ".lua") == 0) {
            out.push_back(n);
        }
    }

    closedir(d);

    std::sort(out.begin(), out.end());

    return out;
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

// FIX 1: integer when whole, so ".." prints "6" not "6.0".
static int lua_get_var(lua_State* L) {
    const char* name = luaL_checkstring(L, 1);

    if (g_scriptVars) {
        auto it = g_scriptVars->find(name);

        if (it != g_scriptVars->end()) {
            double v = it->second;

            if (v == std::floor(v) && std::fabs(v) < 1e15) {
                lua_pushinteger(L, (lua_Integer)v);
            } else {
                lua_pushnumber(L, v);
            }

            return 1;
        }
    }

    lua_pushinteger(L, 0);
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

// -----------------------------------------------------------------------------
// Short aliases + color, matching the API the demo main.lua was written against.
// Color goes through parseColor / parseRgb so the packing format is exactly the
// one the editor/renderer already understands (no hand-rolled bit shifts).
// -----------------------------------------------------------------------------

static int lua_set_pos(lua_State* L) {
    Node2D* n = scriptFindNode(luaL_checkstring(L, 1));

    if (n) {
        n->position.x = (float)luaL_checknumber(L, 2);
        n->position.y = (float)luaL_checknumber(L, 3);
    }

    return 0;
}

static int lua_get_pos(lua_State* L) {
    Node2D* n = scriptFindNode(luaL_checkstring(L, 1));

    lua_pushnumber(L, n ? n->position.x : 0);
    lua_pushnumber(L, n ? n->position.y : 0);
    return 2;
}

static int lua_set_rot(lua_State* L) {
    Node2D* n = scriptFindNode(luaL_checkstring(L, 1));

    if (n) {
        n->rotation = (float)(luaL_checknumber(L, 2) * TWEEN_PI / 180.0);
    }

    return 0;
}

static int lua_get_rot(lua_State* L) {
    Node2D* n = scriptFindNode(luaL_checkstring(L, 1));

    lua_pushnumber(L, n ? n->rotation * 180.0 / TWEEN_PI : 0);
    return 1;
}

// FIX 2: accept (node, sx, sy) like the demo calls it, fall back to uniform.
static int lua_set_scale(lua_State* L) {
    Node2D* n = scriptFindNode(luaL_checkstring(L, 1));
    if (!n) {
        return 0;
    }

    float sx = (float)luaL_checknumber(L, 2);

    int top = lua_gettop(L);
    float sy = (top >= 3 && lua_isnumber(L, 3))
        ? (float)lua_tonumber(L, 3)
        : sx;

    n->scale.x = sx;
    n->scale.y = sy;

    return 0;
}

// FIX 3: return real (sx, sy) instead of averaged scalar.
static int lua_get_scale(lua_State* L) {
    Node2D* n = scriptFindNode(luaL_checkstring(L, 1));

    lua_pushnumber(L, n ? n->scale.x : 1);
    lua_pushnumber(L, n ? n->scale.y : 1);
    return 2;
}

static int lua_set_alpha(lua_State* L) {
    Node2D* n = scriptFindNode(luaL_checkstring(L, 1));

    if (n) {
        n->alpha = (float)luaL_checknumber(L, 2);
    }

    return 0;
}

static int lua_get_alpha(lua_State* L) {
    Node2D* n = scriptFindNode(luaL_checkstring(L, 1));

    lua_pushnumber(L, n ? n->alpha : 1);
    return 1;
}

static int lua_set_size(lua_State* L) {
    Node2D* n = scriptFindNode(luaL_checkstring(L, 1));

    if (n) {
        n->w = (float)luaL_checknumber(L, 2);
        n->h = (float)luaL_checknumber(L, 3);
    }

    return 0;
}

static int lua_set_w(lua_State* L) {
    Node2D* n = scriptFindNode(luaL_checkstring(L, 1));

    if (n) {
        n->w = (float)luaL_checknumber(L, 2);
    }

    return 0;
}

static int lua_set_h(lua_State* L) {
    Node2D* n = scriptFindNode(luaL_checkstring(L, 1));

    if (n) {
        n->h = (float)luaL_checknumber(L, 2);
    }

    return 0;
}

static int lua_set_text(lua_State* L) {
    Node2D* n = scriptFindNode(luaL_checkstring(L, 1));

    if (n && std::string(n->typeName()) == "Label") {
        static_cast<Label*>(n)->text = luaL_checkstring(L, 2);
    }

    return 0;
}

static int lua_get_text(lua_State* L) {
    Node2D* n = scriptFindNode(luaL_checkstring(L, 1));

    if (n && std::string(n->typeName()) == "Label") {
        lua_pushstring(L, static_cast<Label*>(n)->text.c_str());
    } else {
        lua_pushstring(L, "");
    }

    return 1;
}

static int lua_set_texture(lua_State* L) {
    Node2D* n = scriptFindNode(luaL_checkstring(L, 1));

    if (n) {
        n->texture = luaL_checkstring(L, 2);
    }

    return 0;
}

static int lua_set_action(lua_State* L) {
    Node2D* n = scriptFindNode(luaL_checkstring(L, 1));

    if (n) {
        n->action = luaL_checkstring(L, 2);
    }

    return 0;
}

// Tolerant color setter:
//   set_color(node, "#RRGGBB")      -> parseColor
//   set_color(node, "r,g,b")        -> parseRgb
//   set_color(node, r, g, b)        -> normalized to 0..255, then parseRgb
//   set_color(node, packedNumber)   -> assigned as-is
static int lua_set_color(lua_State* L) {
    Node2D* n = scriptFindNode(luaL_checkstring(L, 1));
    if (!n) {
        return 0;
    }

    int top = lua_gettop(L);

    if (top >= 2 && lua_isstring(L, 2)) {
        std::string s = lua_tostring(L, 2);
        n->color = parseColor(s);
        return 0;
    }

    if (top >= 4 && lua_isnumber(L, 2) && lua_isnumber(L, 3) && lua_isnumber(L, 4)) {
        double r = lua_tonumber(L, 2);
        double g = lua_tonumber(L, 3);
        double b = lua_tonumber(L, 4);

        bool frac = (r != std::floor(r)) || (g != std::floor(g)) || (b != std::floor(b));
        bool unit = (r >= 0.0 && r <= 1.0 && g >= 0.0 && g <= 1.0 && b >= 0.0 && b <= 1.0);

        if (frac && unit) {
            r *= 255.0;
            g *= 255.0;
            b *= 255.0;
        }

        auto clamp255 = [](double v) -> int {
            int i = (int)(v + (v >= 0 ? 0.5 : -0.5));
            if (i < 0) i = 0;
            if (i > 255) i = 255;
            return i;
        };

        std::string rgb =
            std::to_string(clamp255(r)) + "," +
            std::to_string(clamp255(g)) + "," +
            std::to_string(clamp255(b));

        unsigned c = 0;
        if (parseRgb(rgb, c)) {
            n->color = c;
        }

        return 0;
    }

    if (top >= 2 && lua_isnumber(L, 2)) {
        n->color = (unsigned)lua_tonumber(L, 2);
        return 0;
    }

    return 0;
}

static int lua_get_color(lua_State* L) {
    Node2D* n = scriptFindNode(luaL_checkstring(L, 1));

    lua_pushnumber(L, n ? (double)n->color : 0);
    return 1;
}

// -----------------------------------------------------------------------------
// B6-lite: particles. opts is a Lua table (real Lua supports tables, the old
// hand-written VM did not). All fields optional; missing ones keep defaults.
// color may be a string ("#RRGGBB" or "r,g,b") or a table {r,g,b}.
// -----------------------------------------------------------------------------

static double optNum(lua_State* L, int tbl, const char* k, double def) {
    lua_getfield(L, tbl, k);
    double v = lua_isnumber(L, -1) ? lua_tonumber(L, -1) : def;
    lua_pop(L, 1);
    return v;
}

static bool optStr(lua_State* L, int tbl, const char* k, std::string& out) {
    lua_getfield(L, tbl, k);
    bool ok = lua_isstring(L, -1);
    if (ok) {
        out = lua_tostring(L, -1);
    }
    lua_pop(L, 1);
    return ok;
}

static void readOpts(lua_State* L, int oi, SpawnOpts& o) {
    if (oi <= 0 || !lua_istable(L, oi)) {
        return;
    }

    o.vx = (float)optNum(L, oi, "vx", o.vx);
    o.vy = (float)optNum(L, oi, "vy", o.vy);
    o.spread = (float)optNum(L, oi, "spread", o.spread);
    o.gravity = (float)optNum(L, oi, "gravity", o.gravity);
    o.life = (float)optNum(L, oi, "life", o.life);
    o.lifeSpread = (float)optNum(L, oi, "lifeSpread", o.lifeSpread);
    o.size = (float)optNum(L, oi, "size", o.size);
    o.sizeEnd = (float)optNum(L, oi, "sizeEnd", o.sizeEnd);
    o.drag = (float)optNum(L, oi, "drag", o.drag);

    // color
    lua_getfield(L, oi, "color");
    if (lua_isstring(L, -1)) {
        std::string c = lua_tostring(L, -1);
        unsigned u = 0;
        if (!c.empty() && c[0] == '#') {
            u = parseColor(c);
        } else if (!parseRgb(c, u)) {
            u = parseColor(c);
        }
        o.color = u;
    } else if (lua_istable(L, -1)) {
        double comp[3] = {255, 255, 255};
        for (int i = 1; i <= 3; ++i) {
            lua_rawgeti(L, -1, i);
            if (lua_isnumber(L, -1)) {
                comp[i - 1] = lua_tonumber(L, -1);
            }
            lua_pop(L, 1);
        }
        auto clamp255 = [](double v) -> int {
            int i = (int)(v + (v >= 0 ? 0.5 : -0.5));
            if (i < 0) i = 0;
            if (i > 255) i = 255;
            return i;
        };
        std::string rgb =
            std::to_string(clamp255(comp[0])) + "," +
            std::to_string(clamp255(comp[1])) + "," +
            std::to_string(clamp255(comp[2]));
        unsigned u = 0;
        if (parseRgb(rgb, u)) {
            o.color = u;
        }
    }
    lua_pop(L, 1);

    // glyph
    std::string g;
    if (optStr(L, oi, "glyph", g) && !g.empty()) {
        std::memset(o.glyph, 0, sizeof(o.glyph));
        std::strncpy(o.glyph, g.c_str(), 7);
        o.glyph[7] = 0;
    }
}

static int lua_emit(lua_State* L) {
    Node2D* n = scriptFindNode(luaL_checkstring(L, 1));
    if (!n) {
        lua_pushinteger(L, 0);
        return 1;
    }

    int count = (int)luaL_checkinteger(L, 2);

    SpawnOpts o;
    readOpts(L, 3, o);

    // Local position is used as world position here (fine for flat/demo scenes
    // whose nodes sit directly under an untransformed root). Nested emitters get
    // exact world math in B6-full once Scene.hpp is integrated.
    g_particles.spawn(n->position.x, n->position.y, count, o);

    lua_pushinteger(L, count);
    return 1;
}

static int lua_emit_at(lua_State* L) {
    float x = (float)luaL_checknumber(L, 1);
    float y = (float)luaL_checknumber(L, 2);
    int count = (int)luaL_checkinteger(L, 3);

    SpawnOpts o;
    readOpts(L, 4, o);

    g_particles.spawn(x, y, count, o);

    lua_pushinteger(L, count);
    return 1;
}

static int lua_particles_clear(lua_State* L) {
    g_particles.clear();
    return 0;
}

static int lua_particles_count(lua_State* L) {
    lua_pushinteger(L, (lua_Integer)g_particles.count());
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

        std::string combined;

        for (const auto& name : listLuaFiles(dir)) {
            std::string path = dir + "/" + name;

            combined += "-- " + name + "\n";
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

        // B6-lite: advance particles every frame (real Lua branch only).
        g_particles.update(dt);
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

        // Short aliases the demo expects.
        lua_register(L_, "set_pos", lua_set_pos);
        lua_register(L_, "get_pos", lua_get_pos);
        lua_register(L_, "set_rot", lua_set_rot);
        lua_register(L_, "get_rot", lua_get_rot);
        lua_register(L_, "set_scale", lua_set_scale);
        lua_register(L_, "get_scale", lua_get_scale);
        lua_register(L_, "set_alpha", lua_set_alpha);
        lua_register(L_, "get_alpha", lua_get_alpha);
        lua_register(L_, "set_size", lua_set_size);
        lua_register(L_, "set_w", lua_set_w);
        lua_register(L_, "set_h", lua_set_h);
        lua_register(L_, "set_text", lua_set_text);
        lua_register(L_, "get_text", lua_get_text);
        lua_register(L_, "set_texture", lua_set_texture);
        lua_register(L_, "set_action", lua_set_action);
        lua_register(L_, "set_color", lua_set_color);
        lua_register(L_, "get_color", lua_get_color);

        // B6-lite particles.
        lua_register(L_, "emit", lua_emit);
        lua_register(L_, "emit_at", lua_emit_at);
        lua_register(L_, "particles_clear", lua_particles_clear);
        lua_register(L_, "particles_count", lua_particles_count);
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
