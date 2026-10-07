#pragma once

#include <string>
#include <vector>
#include <map>
#include <fstream>
#include <sstream>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <algorithm>
#include <utility>
#include <mutex>
#include <type_traits>
#include <dirent.h>

extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}

#include "Core.hpp"
#include "Scene.hpp"
#include "Particles.hpp"
#include "LuaGameApi.hpp"
#include "Physics.hpp"

namespace suka {

inline std::vector<std::string> g_luaLog;

using VarMap = std::remove_reference_t<decltype(std::declval<Context&>().vars)>;

static Context*      g_ctx   = nullptr;
static SceneManager* g_sm    = nullptr;
static Scene*        g_scene = nullptr;
static VarMap*       g_vars  = nullptr;

static void logLua(const std::string& s) {
    g_luaLog.push_back(s);
    if (g_luaLog.size() > 80) g_luaLog.erase(g_luaLog.begin());
}

static std::string luaValueToString(lua_State* L, int idx) {
    int t = lua_type(L, idx);
    if (t == LUA_TNIL) return "nil";
    if (t == LUA_TBOOLEAN) return lua_toboolean(L, idx) ? "true" : "false";
    if (t == LUA_TNUMBER) { char b[64]; std::snprintf(b, sizeof(b), "%g", lua_tonumber(L, idx)); return std::string(b); }
    if (t == LUA_TSTRING) { size_t n = 0; const char* s = lua_tolstring(L, idx, &n); return s ? std::string(s, n) : std::string(); }
    return std::string("<") + lua_typename(L, t) + ">";
}

static unsigned hexChunk(const std::string& h, size_t pos, size_t len) {
    return (unsigned)std::strtoul(h.substr(pos, len).c_str(), nullptr, 16);
}
static unsigned parseColorLocal(const std::string& raw) {
    std::string t = raw;
    size_t a = t.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return 0xFFFFFF;
    size_t b = t.find_last_not_of(" \t\r\n");
    t = t.substr(a, b - a + 1);
    if (!t.empty() && t[0] == '#') {
        std::string h = t.substr(1);
        if (h.size() == 8) h = h.substr(2);
        if (h.size() == 6) return hexChunk(h, 0, 6) & 0xFFFFFF;
        if (h.size() == 3) { unsigned r = hexChunk(h,0,1)*17, g = hexChunk(h,1,1)*17, bl = hexChunk(h,2,1)*17; return (r<<16)|(g<<8)|bl; }
        return 0xFFFFFF;
    }
    std::vector<std::string> tok; std::string cur;
    for (char c : t) { if (c==','||c==' '||c=='\t') { if(!cur.empty()){tok.push_back(cur);cur.clear();} } else cur += c; }
    if (!cur.empty()) tok.push_back(cur);
    if (tok.size() >= 3) {
        int r = atoi(tok[0].c_str()), g = atoi(tok[1].c_str()), bl = atoi(tok[2].c_str());
        return ((unsigned)(r&255)<<16)|((unsigned)(g&255)<<8)|(unsigned)(bl&255);
    }
    if (tok.size() == 1) return (unsigned)std::strtoul(tok[0].c_str(), nullptr, 0) & 0xFFFFFF;
    return 0xFFFFFF;
}

static float normAlpha(double v) { if (v > 1.0 && v <= 100.0) v /= 100.0; if (v < 0.0) v = 0.0; if (v > 1.0) v = 1.0; return (float)v; }
static float deg2rad(double d) { return (float)(d * 3.14159265358979323846 / 180.0); }
static float rad2deg(double r) { return (float)(r * 180.0 / 3.14159265358979323846); }

static Node2D* findNode2D(const std::string& nm) {
    if (!g_scene || !g_scene->root || nm.empty()) return nullptr;
    Node* n = g_scene->root->findNode(nm);
    return n ? dynamic_cast<Node2D*>(n) : nullptr;
}
static UiButton* findUi(const std::string& id) {
    if (!g_scene || id.empty()) return nullptr;
    for (auto& b : g_scene->ui) if (b.touch.id == id) return &b;
    return nullptr;
}

static int l_print(lua_State* L) { int n = lua_gettop(L); std::string out; for (int i=1;i<=n;++i){ if(i>1) out+=" "; out += luaValueToString(L,i);} logLua(out); return 0; }
static int l_log(lua_State* L) { return l_print(L); }

static int l_set_var(lua_State* L) { const char* k = luaL_checkstring(L,1); double v = luaL_checknumber(L,2); if (g_vars&&k) (*g_vars)[k]=v; return 0; }
static int l_get_var(lua_State* L) { const char* k = luaL_checkstring(L,1); if (g_vars&&k){ auto it=g_vars->find(k); if(it!=g_vars->end()){ lua_pushnumber(L,it->second); return 1; } } lua_pushnumber(L,0.0); return 1; }
static int l_add_var(lua_State* L) { const char* k = luaL_checkstring(L,1); double v = luaL_checknumber(L,2); if (g_vars&&k){ auto it=g_vars->find(k); if(it!=g_vars->end()) it->second+=v; else (*g_vars)[k]=v; } return 0; }

static int l_get_score(lua_State* L) { lua_pushinteger(L, g_ctx ? (lua_Integer)g_ctx->score : 0); return 1; }
static int l_set_score(lua_State* L) { double v = luaL_checknumber(L,1); if (g_ctx) g_ctx->score=(int)v; return 0; }
static int l_add_score(lua_State* L) { double v = luaL_checknumber(L,1); if (g_ctx) g_ctx->score+=(int)v; return 0; }

static int l_set_pos(lua_State* L) { Node2D* n = findNode2D(luaL_checkstring(L,1)); if (n){ n->position.x=(float)luaL_checknumber(L,2); n->position.y=(float)luaL_checknumber(L,3);} return 0; }
static int l_set_x(lua_State* L) { Node2D* n = findNode2D(luaL_checkstring(L,1)); if (n) n->position.x=(float)luaL_checknumber(L,2); return 0; }
static int l_set_y(lua_State* L) { Node2D* n = findNode2D(luaL_checkstring(L,1)); if (n) n->position.y=(float)luaL_checknumber(L,2); return 0; }
static int l_set_rot(lua_State* L) { Node2D* n = findNode2D(luaL_checkstring(L,1)); if (n) n->rotation=deg2rad(luaL_checknumber(L,2)); return 0; }
static int l_set_scale(lua_State* L) {
    Node2D* n = findNode2D(luaL_checkstring(L,1)); if (!n) return 0;
    if (lua_gettop(L) >= 3 && lua_isnumber(L,3)) { n->scale.x=(float)lua_tonumber(L,2); n->scale.y=(float)lua_tonumber(L,3); }
    else { float s=(float)luaL_checknumber(L,2); n->scale.x=s; n->scale.y=s; }
    return 0;
}
static int l_set_size(lua_State* L) { Node2D* n = findNode2D(luaL_checkstring(L,1)); if (n){ n->w=(float)luaL_checknumber(L,2); n->h=(float)luaL_checknumber(L,3);} return 0; }
static int l_set_alpha(lua_State* L) {
    float al = normAlpha(luaL_checknumber(L,2));
    Node2D* n = findNode2D(luaL_checkstring(L,1)); if (n){ n->alpha=al; return 0; }
    UiButton* b = findUi(lua_tostring(L,1)); if (b) b->alpha=al;
    return 0;
}
static int l_set_color(lua_State* L) {
    const char* nm = luaL_checkstring(L,1);
    int top = lua_gettop(L); unsigned col = 0xFFFFFF;
    if (top >= 2 && lua_isstring(L,2)) col = parseColorLocal(lua_tostring(L,2));
    else if (top >= 4 && lua_isnumber(L,2) && lua_isnumber(L,3) && lua_isnumber(L,4)) {
        int r=(int)lua_tonumber(L,2), g=(int)lua_tonumber(L,3), b=(int)lua_tonumber(L,4);
        col = ((unsigned)(r&255)<<16)|((unsigned)(g&255)<<8)|(unsigned)(b&255);
    } else if (top >= 2 && lua_isnumber(L,2)) col = (unsigned)lua_tointeger(L,2) & 0xFFFFFF;
    Node2D* n = findNode2D(nm); if (n){ n->color=col; return 0; }
    UiButton* b = findUi(nm); if (b) b->color=col;
    return 0;
}
static int l_set_texture(lua_State* L) {
    const char* nm = luaL_checkstring(L,1); const char* tx = luaL_checkstring(L,2);
    Node2D* n = findNode2D(nm); if (n){ n->texture = tx?tx:""; Sprite2D* sp = dynamic_cast<Sprite2D*>(n); if (sp) sp->texturePath = n->texture; if (n->texture.empty()) n->shape="square"; else n->shape="none"; return 0; }
    UiButton* b = findUi(nm); if (b) b->texture = tx?tx:"";
    return 0;
}
static int l_set_shape(lua_State* L) { Node2D* n = findNode2D(luaL_checkstring(L,1)); const char* s = luaL_checkstring(L,2); if (n&&s) n->shape=s; return 0; }
static int l_set_action(lua_State* L) {
    const char* nm = luaL_checkstring(L,1); const char* ac = luaL_checkstring(L,2);
    Node2D* n = findNode2D(nm); if (n){ n->action = ac?ac:""; return 0; }
    UiButton* b = findUi(nm); if (b) b->action = ac?ac:"";
    return 0;
}
static int l_set_text(lua_State* L) {
    const char* nm = luaL_checkstring(L,1); const char* tx = luaL_checkstring(L,2);
    Node2D* n = findNode2D(nm); if (n){ Label* lb = dynamic_cast<Label*>(n); if (lb){ lb->text = tx?tx:""; return 0; } }
    UiButton* b = findUi(nm); if (b) b->text = tx?tx:"";
    return 0;
}
static int l_set_zoom(lua_State* L) { Node2D* n = findNode2D(luaL_checkstring(L,1)); if (n){ Camera2D* c = dynamic_cast<Camera2D*>(n); if (c) c->zoom=(float)luaL_checknumber(L,2);} return 0; }
static int l_set_cam(lua_State* L) { Node2D* n = findNode2D(luaL_checkstring(L,1)); if (n){ Camera2D* c = dynamic_cast<Camera2D*>(n); if (c){ c->position.x=(float)luaL_checknumber(L,2); c->position.y=(float)luaL_checknumber(L,3);} } return 0; }

static int l_hide(lua_State* L) { Node2D* n = findNode2D(luaL_checkstring(L,1)); if (n) n->alpha=0.0f; return 0; }
static int l_show(lua_State* L) { Node2D* n = findNode2D(luaL_checkstring(L,1)); if (n) n->alpha=1.0f; return 0; }
static int l_exists(lua_State* L) { lua_pushboolean(L, findNode2D(luaL_checkstring(L,1)) != nullptr || findUi(lua_tostring(L,1)) != nullptr); return 1; }

static int l_get_button_pressed(lua_State* L) {
    const char* id = luaL_checkstring(L, 1);
    bool p = false;
    if (g_scene && id) {
        for (auto& b : g_scene->ui) {
            if (b.touch.id == id) { p = b.touch.pressed; break; }
        }
    }
    lua_pushboolean(L, p ? 1 : 0);
    return 1;
}

// === MOBEN GETTERS: чтение свойств нод и UI из Lua (пункт 5) ===

static int l_get_pos(lua_State* L) {
    Node2D* n = findNode2D(luaL_checkstring(L,1));
    lua_pushnumber(L, n ? n->position.x : 0.0);
    lua_pushnumber(L, n ? n->position.y : 0.0);
    return 2;
}
static int l_get_x(lua_State* L) {
    Node2D* n = findNode2D(luaL_checkstring(L,1));
    lua_pushnumber(L, n ? n->position.x : 0.0);
    return 1;
}
static int l_get_y(lua_State* L) {
    Node2D* n = findNode2D(luaL_checkstring(L,1));
    lua_pushnumber(L, n ? n->position.y : 0.0);
    return 1;
}
static int l_get_rotation(lua_State* L) {
    Node2D* n = findNode2D(luaL_checkstring(L,1));
    lua_pushnumber(L, n ? rad2deg(n->rotation) : 0.0);
    return 1;
}
static int l_get_scale(lua_State* L) {
    Node2D* n = findNode2D(luaL_checkstring(L,1));
    lua_pushnumber(L, n ? n->scale.x : 1.0);
    lua_pushnumber(L, n ? n->scale.y : 1.0);
    return 2;
}
static int l_get_scale_x(lua_State* L) {
    Node2D* n = findNode2D(luaL_checkstring(L,1));
    lua_pushnumber(L, n ? n->scale.x : 1.0);
    return 1;
}
static int l_get_scale_y(lua_State* L) {
    Node2D* n = findNode2D(luaL_checkstring(L,1));
    lua_pushnumber(L, n ? n->scale.y : 1.0);
    return 1;
}
static int l_get_size(lua_State* L) {
    Node2D* n = findNode2D(luaL_checkstring(L,1));
    lua_pushnumber(L, n ? n->w : 0.0);
    lua_pushnumber(L, n ? n->h : 0.0);
    return 2;
}
static int l_get_w(lua_State* L) {
    Node2D* n = findNode2D(luaL_checkstring(L,1));
    lua_pushnumber(L, n ? n->w : 0.0);
    return 1;
}
static int l_get_h(lua_State* L) {
    Node2D* n = findNode2D(luaL_checkstring(L,1));
    lua_pushnumber(L, n ? n->h : 0.0);
    return 1;
}
static int l_get_alpha(lua_State* L) {
    const char* nm = luaL_checkstring(L,1);
    Node2D* n = findNode2D(nm); if (n){ lua_pushnumber(L, n->alpha); return 1; }
    UiButton* b = findUi(nm); if (b){ lua_pushnumber(L, b->alpha); return 1; }
    lua_pushnumber(L, 1.0); return 1;
}
static int l_get_color(lua_State* L) {
    const char* nm = luaL_checkstring(L,1);
    Node2D* n = findNode2D(nm); if (n){ lua_pushinteger(L, (lua_Integer)n->color); return 1; }
    UiButton* b = findUi(nm); if (b){ lua_pushinteger(L, (lua_Integer)b->color); return 1; }
    lua_pushinteger(L, 0xFFFFFF); return 1;
}
static int l_get_shape(lua_State* L) {
    Node2D* n = findNode2D(luaL_checkstring(L,1));
    lua_pushstring(L, n ? n->shape.c_str() : "");
    return 1;
}
static int l_get_texture(lua_State* L) {
    const char* nm = luaL_checkstring(L,1);
    Node2D* n = findNode2D(nm); if (n){ lua_pushstring(L, n->texture.c_str()); return 1; }
    UiButton* b = findUi(nm); if (b){ lua_pushstring(L, b->texture.c_str()); return 1; }
    lua_pushstring(L, ""); return 1;
}
static int l_get_action(lua_State* L) {
    const char* nm = luaL_checkstring(L,1);
    Node2D* n = findNode2D(nm); if (n){ lua_pushstring(L, n->action.c_str()); return 1; }
    UiButton* b = findUi(nm); if (b){ lua_pushstring(L, b->action.c_str()); return 1; }
    lua_pushstring(L, ""); return 1;
}
static int l_get_text(lua_State* L) {
    const char* nm = luaL_checkstring(L,1);
    Node2D* n = findNode2D(nm);
    if (n){ Label* lb = dynamic_cast<Label*>(n); if (lb){ lua_pushstring(L, lb->text.c_str()); return 1; } }
    UiButton* b = findUi(nm); if (b){ lua_pushstring(L, b->text.c_str()); return 1; }
    lua_pushstring(L, ""); return 1;
}

// === конец MOBEN GETTERS ===

static int l_spawn(lua_State* L) {
    Node2D* n = findNode2D(luaL_checkstring(L,1)); if (!n) return 0;
    Particle2D* p = dynamic_cast<Particle2D*>(n); if (!p) return 0;
    int cnt = lua_gettop(L) >= 2 ? (int)lua_tonumber(L,2) : 1;
    p->burst = cnt > 0 ? cnt : 1; p->burstPending = true; p->emitting = true;
    return 0;
}
static int l_emit(lua_State* L) {
    Node2D* n = findNode2D(luaL_checkstring(L,1)); if (!n) return 0;
    Particle2D* p = dynamic_cast<Particle2D*>(n); if (!p) return 0;
    p->emitting = lua_gettop(L) >= 2 ? lua_toboolean(L,2) : true;
    if (!p->emitting) p->burstPending = false;
    return 0;
}

static void registerApi(lua_State* L) {
    if (!L) return;
    lua_register(L, "print", l_print);
    lua_register(L, "log", l_log);
    lua_register(L, "set_var", l_set_var);
    lua_register(L, "get_var", l_get_var);
    lua_register(L, "add_var", l_add_var);
    lua_register(L, "get_score", l_get_score);
    lua_register(L, "set_score", l_set_score);
    lua_register(L, "add_score", l_add_score);
    lua_register(L, "set_pos", l_set_pos);
    lua_register(L, "set_x", l_set_x);
    lua_register(L, "set_y", l_set_y);
    lua_register(L, "set_rot", l_set_rot);
    lua_register(L, "set_scale", l_set_scale);
    lua_register(L, "set_size", l_set_size);
    lua_register(L, "set_alpha", l_set_alpha);
    lua_register(L, "set_color", l_set_color);
    lua_register(L, "set_texture", l_set_texture);
    lua_register(L, "set_shape", l_set_shape);
    lua_register(L, "set_action", l_set_action);
    lua_register(L, "set_text", l_set_text);
    lua_register(L, "set_zoom", l_set_zoom);
    lua_register(L, "set_cam", l_set_cam);
    lua_register(L, "hide", l_hide);
    lua_register(L, "show", l_show);
    lua_register(L, "exists", l_exists);
    lua_register(L, "get_button_pressed", l_get_button_pressed);

    // === регистрация MOBEN GETTERS ===
    lua_register(L, "get_pos", l_get_pos);
    lua_register(L, "get_x", l_get_x);
    lua_register(L, "get_y", l_get_y);
    lua_register(L, "get_rotation", l_get_rotation);
    lua_register(L, "get_scale", l_get_scale);
    lua_register(L, "get_scale_x", l_get_scale_x);
    lua_register(L, "get_scale_y", l_get_scale_y);
    lua_register(L, "get_size", l_get_size);
    lua_register(L, "get_w", l_get_w);
    lua_register(L, "get_h", l_get_h);
    lua_register(L, "get_alpha", l_get_alpha);
    lua_register(L, "get_color", l_get_color);
    lua_register(L, "get_shape", l_get_shape);
    lua_register(L, "get_texture", l_get_texture);
    lua_register(L, "get_action", l_get_action);
    lua_register(L, "get_text", l_get_text);
    // === конец регистрации ===

    lua_register(L, "spawn", l_spawn);
    lua_register(L, "emit", l_emit);

    // ПОРЯДОК НЕ МЕНЯТЬ: registerGameLuaApi до registerPhysicsLuaApi.
    registerGameLuaApi(L);
    registerPhysicsLuaApi(L);
}

static bool endsWithStr(const std::string& s, const std::string& suf) {
    if (suf.size() > s.size()) return false;
    return s.compare(s.size() - suf.size(), suf.size(), suf) == 0;
}
static std::string readFileToString(const std::string& path) {
    std::ifstream f(path, std::ios::binary); if (!f.good()) return std::string();
    std::ostringstream ss; ss << f.rdbuf(); return ss.str();
}
static std::vector<std::string> listLuaFiles(const std::string& dir) {
    std::vector<std::string> files; DIR* d = opendir(dir.c_str()); if (!d) return files;
    struct dirent* ent;
    while ((ent = readdir(d)) != nullptr) { std::string nm = ent->d_name; if (nm=="."||nm=="..") continue; if (endsWithStr(nm,".lua")) files.push_back(nm); }
    closedir(d); std::sort(files.begin(), files.end()); return files;
}

class ScriptSystem {
public:
    lua_State* L = nullptr;

    Context* lastCtx = nullptr;
    SceneManager* lastSm = nullptr;
    Scene* lastScene = nullptr;
    VarMap* lastVars = nullptr;

    ~ScriptSystem() { if (L) { lua_close(L); L = nullptr; } }

    void load(const std::string& root) {
        if (L) { g_ctx = nullptr; g_sm = nullptr; g_scene = nullptr; g_vars = nullptr; lua_close(L); L = nullptr; }
        { std::lock_guard<std::mutex> lk(g_luaCmdMtx); g_luaCmd = LuaGameCmd{}; }
        { std::lock_guard<std::mutex> lk(g_soundMtx); g_soundQ.clear(); }
        { std::lock_guard<std::mutex> lk(g_bodiesMtx); g_bodies.clear(); }
        { std::lock_guard<std::mutex> lk(g_collideMtx); g_collideEvents.clear(); }

        L = luaL_newstate();
        if (!L) { logLua("lua: failed to create state"); return; }
        luaL_openlibs(L);
        registerApi(L);

        std::string dir = root + "/scripts";
        for (const std::string& f : listLuaFiles(dir)) {
            std::string path = dir + "/" + f;
            std::string src = readFileToString(path);
            if (src.empty()) continue;
            if (luaL_loadbuffer(L, src.c_str(), src.size(), path.c_str()) != 0) {
                const char* e = lua_tostring(L, -1); logLua(std::string("lua load error: ") + (e ? e : "?")); lua_pop(L, 1); continue;
            }
            if (lua_pcall(L, 0, 0, 0) != 0) {
                const char* e = lua_tostring(L, -1); logLua(std::string("lua run error: ") + (e ? e : "?")); lua_pop(L, 1);
            }
        }
        { std::lock_guard<std::mutex> lk(g_luaCmdMtx); g_luaCmd = LuaGameCmd{}; }
    }

    void update(Scene& scene, Context& ctx, float dt, SceneManager& sm, VarMap& vars) {
        if (!L) return;
        g_ctx = &ctx; g_sm = &sm; g_scene = &scene; g_vars = &vars;
        lastCtx = &ctx; lastSm = &sm; lastScene = &scene; lastVars = &vars;

        lua_getglobal(L, "on_update");
        if (lua_isfunction(L, -1)) {
            lua_pushnumber(L, dt);
            if (lua_pcall(L, 1, 0, 0) != 0) { const char* e = lua_tostring(L, -1); logLua(std::string("lua on_update error: ") + (e ? e : "?")); lua_pop(L, 1); }
        } else lua_pop(L, 1);

        g_ctx = nullptr; g_sm = nullptr; g_scene = nullptr; g_vars = nullptr;
    }

    void callGlobal(const std::string& name, Context& ctx, SceneManager& sm, VarMap& vars, Scene* scene) {
        if (!L || name.empty()) return;
        g_ctx = &ctx; g_sm = &sm; g_scene = scene; g_vars = &vars;
        lastCtx = &ctx; lastSm = &sm; lastScene = scene; lastVars = &vars;

        lua_getglobal(L, name.c_str());
        if (lua_isfunction(L, -1)) {
            if (lua_pcall(L, 0, 0, 0) != 0) { const char* e = lua_tostring(L, -1); logLua(std::string("lua call error ") + name + ": " + (e ? e : "?")); lua_pop(L, 1); }
        } else { lua_pop(L, 1); logLua("lua: missing function " + name); }

        g_ctx = nullptr; g_sm = nullptr; g_scene = nullptr; g_vars = nullptr;
    }

    void callCollide(const std::string& a, const std::string& b) {
        if (!L) return;
        g_ctx = lastCtx; g_sm = lastSm; g_scene = lastScene; g_vars = lastVars;

        lua_getglobal(L, "on_collide");
        if (lua_isfunction(L, -1)) {
            lua_pushstring(L, a.c_str());
            lua_pushstring(L, b.c_str());
            if (lua_pcall(L, 2, 0, 0) != 0) { const char* e = lua_tostring(L, -1); logLua(std::string("lua on_collide error: ") + (e ? e : "?")); lua_pop(L, 1); }
        } else lua_pop(L, 1);

        g_ctx = nullptr; g_sm = nullptr; g_scene = nullptr; g_vars = nullptr;
    }
};

} // namespace suka
